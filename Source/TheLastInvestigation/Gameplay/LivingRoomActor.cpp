#include "LivingRoomActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "StormWindowActor.h"
#include "DustMotesComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"

namespace
{
	/** The dado and the picture rail, above the floor. */
	constexpr float LivingDado = 95.f;
	constexpr float LivingPictureRail = 330.f;

	/** Collision that only a walking man meets: the interaction trace and the camera pass through. */
	void LivingPawnOnly(UStaticMeshComponent* Part)
	{
		if (Part)
		{
			Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Part->SetCollisionResponseToAllChannels(ECR_Ignore);
			Part->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			Part->SetHiddenInGame(true);
			Part->SetCastShadow(false);
		}
	}

	/**
	 * Ear clipping, for the caps of a prism whose outline is not convex. A grand piano is the one
	 * shape in the house that is neither a box nor a turned thing: its bentside curves in and out
	 * again, and a fan from any one point either misses part of it or covers ground it should not.
	 */
	TArray<int32> LivingTriangulate(const TArray<FVector2D>& Points)
	{
		TArray<int32> Out;
		const int32 Count = Points.Num();
		if (Count < 3)
		{
			return Out;
		}
		float Area = 0.f;
		for (int32 i = 0; i < Count; ++i)
		{
			Area += FVector2D::CrossProduct(Points[i], Points[(i + 1) % Count]);
		}
		const float Sign = Area >= 0.f ? 1.f : -1.f;

		auto Inside = [&](const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
		{
			return FVector2D::CrossProduct(B - A, P - A) * Sign > 0.f
				&& FVector2D::CrossProduct(C - B, P - B) * Sign > 0.f
				&& FVector2D::CrossProduct(A - C, P - C) * Sign > 0.f;
		};

		TArray<int32> Ring;
		for (int32 i = 0; i < Count; ++i)
		{
			Ring.Add(i);
		}
		while (Ring.Num() > 3)
		{
			bool bClipped = false;
			for (int32 i = 0; i < Ring.Num(); ++i)
			{
				const int32 IA = Ring[(i + Ring.Num() - 1) % Ring.Num()];
				const int32 IB = Ring[i];
				const int32 IC = Ring[(i + 1) % Ring.Num()];
				const FVector2D& A = Points[IA];
				const FVector2D& B = Points[IB];
				const FVector2D& C = Points[IC];
				if (FVector2D::CrossProduct(B - A, C - B) * Sign <= 0.f)
				{
					continue; // reflex, or flat
				}
				bool bEar = true;
				for (const int32 J : Ring)
				{
					if (J != IA && J != IB && J != IC && Inside(Points[J], A, B, C))
					{
						bEar = false;
						break;
					}
				}
				if (bEar)
				{
					Out.Append({ IA, IB, IC });
					Ring.RemoveAt(i);
					bClipped = true;
					break;
				}
			}
			if (!bClipped)
			{
				break; // degenerate outline: better a hole in the lid than a hang at BeginPlay
			}
		}
		if (Ring.Num() == 3)
		{
			Out.Append({ Ring[0], Ring[1], Ring[2] });
		}
		return Out;
	}

	/**
	 * An outline stood up between Z0 and Z1 in the parent's own XY: walls round it and a cap on
	 * each end. Neighbouring walls that meet at a shallow angle share a normal, so a curve built
	 * from short straight runs shades as a curve — on black lacquer the facets are the first thing
	 * the lantern would find. Every face is emitted both ways round with its normal forced (the
	 * Pane rule); the inside of a solid is never seen, so nothing is lit wrongly by it.
	 */
	UProceduralMeshComponent* LivingPrism(AActor* Owner, USceneComponent* Parent, const TArray<FVector2D>& Outline,
		float Z0, float Z1, float TexelCm, UMaterialInterface* Mat)
	{
		const int32 Count = Outline.Num();
		if (Count < 3 || !Mat)
		{
			return nullptr;
		}
		float Area = 0.f;
		for (int32 i = 0; i < Count; ++i)
		{
			Area += FVector2D::CrossProduct(Outline[i], Outline[(i + 1) % Count]);
		}
		const float Sign = Area >= 0.f ? 1.f : -1.f;
		auto EdgeNormal = [&](int32 i)
		{
			const FVector2D D = (Outline[(i + 1) % Count] - Outline[i]).GetSafeNormal();
			return FVector(D.Y * Sign, -D.X * Sign, 0.f);
		};
		const float Smooth = FMath::Cos(FMath::DegreesToRadians(32.f));

		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Tris;
		auto Both = [&](int32 A, int32 B, int32 C)
		{
			Tris.Append({ A, B, C, A, C, B });
		};

		float Run = 0.f;
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 j = (i + 1) % Count;
			const FVector N = EdgeNormal(i);
			const FVector NPrev = EdgeNormal((i + Count - 1) % Count);
			const FVector NNext = EdgeNormal(j);
			const FVector NA = FVector::DotProduct(N, NPrev) > Smooth ? (N + NPrev).GetSafeNormal() : N;
			const FVector NB = FVector::DotProduct(N, NNext) > Smooth ? (N + NNext).GetSafeNormal() : N;
			const float Length = FVector2D::Distance(Outline[i], Outline[j]);
			const FVector Along = FVector(Outline[j] - Outline[i], 0.f).GetSafeNormal();

			const int32 First = Verts.Num();
			Verts.Append({ FVector(Outline[i], Z0), FVector(Outline[j], Z0), FVector(Outline[j], Z1), FVector(Outline[i], Z1) });
			Normals.Append({ NA, NB, NB, NA });
			UVs.Append({ FVector2D(Run / TexelCm, -Z0 / TexelCm), FVector2D((Run + Length) / TexelCm, -Z0 / TexelCm),
				FVector2D((Run + Length) / TexelCm, -Z1 / TexelCm), FVector2D(Run / TexelCm, -Z1 / TexelCm) });
			for (int32 k = 0; k < 4; ++k)
			{
				Tangents.Add(FProcMeshTangent(Along, false));
			}
			Both(First, First + 1, First + 2);
			Both(First, First + 2, First + 3);
			Run += Length;
		}

		const TArray<int32> Cap = LivingTriangulate(Outline);
		for (const float Z : { Z0, Z1 })
		{
			const int32 First = Verts.Num();
			const FVector N(0.f, 0.f, Z == Z1 ? 1.f : -1.f);
			for (const FVector2D& P : Outline)
			{
				Verts.Add(FVector(P, Z));
				Normals.Add(N);
				UVs.Add(P / TexelCm);
				Tangents.Add(FProcMeshTangent(FVector(1.f, 0.f, 0.f), false));
			}
			for (int32 t = 0; t + 2 < Cap.Num(); t += 3)
			{
				Both(First + Cap[t], First + Cap[t + 1], First + Cap[t + 2]);
			}
		}

		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("Prism")));
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		Mesh->bUseAsyncCooking = false;
		Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, TArray<FLinearColor>(), Tangents, /*bCreateCollision*/ false);
		Mesh->SetMaterial(0, Mat);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->RegisterComponent();
		Owner->AddInstanceComponent(Mesh);
		return Mesh;
	}

	/**
	 * Rectangles laid in one vertical plane with one continuous set of UVs, in centimetres over
	 * TexelCm: Origin is where U and Z are both zero, Along the direction of U, Face the way the
	 * front looks. For stonework, whose courses have to run on unbroken across the pieces a wall
	 * is cut into round a hole — a box per piece tiles and crops each one on its own, and the
	 * joints step at every cut.
	 */
	UProceduralMeshComponent* LivingFacade(AActor* Owner, USceneComponent* Parent, const TArray<FBox2D>& Pieces, const FVector& Origin,
		const FVector& Along, const FVector& Face, float TexelCm, UMaterialInterface* Mat)
	{
		if (Pieces.Num() == 0 || !Mat)
		{
			return nullptr;
		}
		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Tris;
		for (const FBox2D& Piece : Pieces)
		{
			const int32 First = Verts.Num();
			const FVector2D Corners[4] = { Piece.Min, FVector2D(Piece.Max.X, Piece.Min.Y), Piece.Max, FVector2D(Piece.Min.X, Piece.Max.Y) };
			for (const FVector2D& C : Corners)
			{
				Verts.Add(Origin + Along * C.X + FVector(0.f, 0.f, C.Y));
				Normals.Add(Face);
				UVs.Add(FVector2D(C.X / TexelCm, -C.Y / TexelCm));
				Tangents.Add(FProcMeshTangent(Along, false));
			}
			Tris.Append({ First, First + 1, First + 2, First, First + 2, First + 1, First, First + 2, First + 3, First, First + 3, First + 2 });
		}
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("Facade")));
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		Mesh->bUseAsyncCooking = false;
		Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, TArray<FLinearColor>(), Tangents, /*bCreateCollision*/ false);
		Mesh->SetMaterial(0, Mat);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->RegisterComponent();
		Owner->AddInstanceComponent(Mesh);
		return Mesh;
	}

	/**
	 * Upholstery takes its roughness from the neutral ARM every prop without a packed map gets (see
	 * Tools/build_art.py), which is 0.55: a satin, and under the lantern the velvet came out with a
	 * sheen like leather. Cloth that has sat in the dark for sixty years is as matt as anything in the
	 * room. Call after TintSlots, whose dynamic instances this adjusts.
	 */
	void LivingMatte(UStaticMeshComponent* Mesh, float RoughnessScale)
	{
		if (!Mesh)
		{
			return;
		}
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot)))
			{
				Instance->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
			}
		}
	}

	/** A child scene component to build a group of parts in its own frame: the piano, the coat stand. */
	USceneComponent* LivingPivot(AActor* Owner, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation, const TCHAR* Name)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(Owner, MakeUniqueObjectName(Owner, USceneComponent::StaticClass(), Name));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		Pivot->SetRelativeLocationAndRotation(Location, Rotation);
		Pivot->RegisterComponent();
		Owner->AddInstanceComponent(Pivot);
		return Pivot;
	}
}

ALivingRoomActor::ALivingRoomActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);
	RoomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void ALivingRoomActor::Configure(const FLivingRoomSetup& InSetup, AStormWindowActor* InLeadStorm)
{
	Setup = InSetup;
	LeadStorm = InLeadStorm;

	// Here and not in BeginPlay: the motes are scattered in the component's own BeginPlay, which
	// runs before the owner's (see ARoomDressingActor::Configure).
	DustMotes->ConfigureVolume(
		FVector((EastX() - WestX()) * 0.48f, RoomDepth * 0.48f, RoomHeight * 0.48f),
		FVector(MidX(), MidY(), FloorZ() + RoomHeight * 0.5f));

	Openings = {
		// The door from the hall.
		{ EWall::North, Setup.DoorX, Setup.DoorHalf, FloorZ(), FloorZ() + Setup.DoorHeight },
		// The three windows.
		{ EWall::South, WindowX(0), WindowWidth * 0.5f, FloorZ() + WindowSill, FloorZ() + WindowTop },
		{ EWall::South, WindowX(1), WindowWidth * 0.5f, FloorZ() + WindowSill, FloorZ() + WindowTop },
		{ EWall::South, WindowX(2), WindowWidth * 0.5f, FloorZ() + WindowSill, FloorZ() + WindowTop },
		// The chimney breast stands against the west wall rather than through it: the wall's own
		// finish goes round it, the shell does not.
		{ EWall::West, HearthY(), BreastWidth * 0.5f, FloorZ(), CeilingZ(), /*bThroughWall*/ false },
	};
}

void ALivingRoomActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning this room never relays the hall, the corridor or the bedroom.
	Random.Initialize(19640927);

	FRoomBuilder Build(this, RoomRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildFloor(Build);
	BuildWallFinish(Build);
	BuildCeiling(Build);
	BuildFireplace(Build);
	BuildTelevision(Build);
	BuildSeating(Build);
	BuildPiano(Build);
	BuildWallFurniture(Build);
	BuildChandelier(Build);
	BuildDamage(Build);
	BuildDebris(Build);

	SpawnWindows();
	BuildClues();
}

void ALivingRoomActor::CacheMaterials(FRoomBuilder& Build)
{
	// Everything the hall has too is at the hall's values, so walking through the door does not
	// change the house. The new surfaces are held to the same rule — a tenth reflectance or under,
	// and warm, because the only fill in here is the cold light through three windows.
	MatPlaster = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f));
	MatWallpaper = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.160f, 0.132f, 0.108f));
	MatWallpaperDark = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.120f, 0.098f, 0.080f));
	MatWainscot = Build.Surface(RoomSurfaces::Wainscot, FLinearColor(2.1f, 3.3f, 3.5f));
	MatOak = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.290f, 0.272f, 0.250f));
	MatOakDark = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.190f, 0.180f, 0.170f));
	MatBoards = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.44f, 0.41f, 0.38f));
	// herringbone_parquet is photographed honey-coloured (linear 0.294, 0.163, 0.075): brought down
	// and pulled off orange to a dark oak at about the old boards' value, (0.050, 0.029, 0.016).
	// And six times rougher than photographed: it was shot freshly lacquered (roughness 0.1), and at
	// that the whole floor was a wet mirror of the three windows. Sixty years of dust takes the
	// shine off a floor; this lands it at about 0.6, the old boards' figure.
	MatParquet = Build.Surface(RoomSurfaces::Parquet, FLinearColor(0.17f, 0.18f, 0.21f), 6.2f);
	MatParquetWorn = Build.Surface(RoomSurfaces::Parquet, FLinearColor(0.13f, 0.14f, 0.16f), 7.f);
	// floral_jacquard is a dark neutral grey (linear 0.054, 0.052, 0.060) with the pattern woven in
	// light and shade, so a tint takes it anywhere: a green gone dull with sixty years of feet and
	// sun, (0.033, 0.055, 0.035). The border is a darker, browner green, as the edge of a carpet is.
	MatCarpet = Build.Surface(RoomSurfaces::Carpet, FLinearColor(0.62f, 1.05f, 0.58f));
	MatCarpetBorder = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.062f, 0.071f, 0.036f), 1.2f);
	// medieval_blocks_03 is a warm grey stone at linear (0.187, 0.147, 0.099); the chimney breast
	// is at about the plaster's value and a shade cooler, so it reads as a different material from
	// across the room and not as a lighter wall.
	MatStone = Build.Surface(RoomSurfaces::Stone, FLinearColor(0.48f, 0.54f, 0.66f));
	MatStoneDark = Build.Surface(RoomSurfaces::Stone, FLinearColor(0.30f, 0.33f, 0.40f));
	// The same stone for the breast's facade, which carries its own UVs in repeats: an instance of
	// its own with the tiling at one (the spandrels' rule). A hair off the tint, so the builder's
	// cache hands back a separate instance rather than the one Add() tiles from.
	MatStoneSheet = Build.Surface(RoomSurfaces::Stone, FLinearColor(0.48f, 0.54f, 0.661f));
	if (MatStoneSheet)
	{
		MatStoneSheet->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
		MatStoneSheet->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	MatSoot = Build.Surface(RoomSurfaces::Substrate, FLinearColor(0.05f, 0.045f, 0.042f));
	MatMarble = Build.Surface(RoomSurfaces::Marble, FLinearColor(0.34f, 0.34f, 0.36f), 0.7f);
	// Darker than the hall's: this ceiling is a metre and a half above the tops of three windows,
	// and their sky lights it from below far more than the hall's one window lights its own.
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.20f, 0.19f, 0.18f));
	MatRubble = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.145f, 0.127f, 0.110f));
	MatPaper = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.464f, 0.245f, 0.108f));
	MatPaperDamp = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.260f, 0.142f, 0.065f));
	// Newsprint yellows further and goes greyer than writing paper.
	MatNewsprint = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.40f, 0.25f, 0.13f));
	MatDustSheet = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.30f, 0.21f, 0.14f));
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatBrass = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.28f, 0.06f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);
	MatWax = Build.Flat(FLinearColor(0.34f, 0.30f, 0.22f), 0.55f);
	MatShadow = Build.Flat(FLinearColor(0.002f, 0.002f, 0.002f), 1.f);
	MatCharred = Build.Flat(FLinearColor(0.011f, 0.009f, 0.008f), 1.f);
	MatAsh = Build.Flat(FLinearColor(0.090f, 0.088f, 0.085f), 1.f);
	// Black lacquer, polished once and dusted since: glossy enough to throw the lantern back as a
	// long highlight along the case, which is what makes a black piano visible in the dark at all.
	MatLacquer = Build.Flat(FLinearColor(0.010f, 0.0095f, 0.009f), 0.24f);
	// Ivory goes yellow, and the keys that lost their tops show the wood under them.
	MatIvory = Build.Flat(FLinearColor(0.27f, 0.235f, 0.17f), 0.42f);
	MatIvoryDark = Build.Flat(FLinearColor(0.11f, 0.08f, 0.05f), 0.8f);
	MatEbony = Build.Flat(FLinearColor(0.007f, 0.0065f, 0.0065f), 0.35f);
	MatFelt = Build.Flat(FLinearColor(0.020f, 0.034f, 0.022f), 0.95f);
	// The television: a screen that is a black mirror, and a bezel of the matt black plastic every
	// set of the last twenty years is made of. Nothing else in the house is this black or this new.
	MatScreen = Build.Flat(FLinearColor(0.003f, 0.003f, 0.0035f), 0.05f);
	MatPlastic = Build.Flat(FLinearColor(0.009f, 0.009f, 0.010f), 0.55f);
	MatPhoto = Build.Flat(RoomPalette::Photo, 0.6f);
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers, the hall's: U runs along a wall (X on north/south, Y on east/west), Z is
// world height, and a wall's normal points into the room.
// ---------------------------------------------------------------------------------------------

float ALivingRoomActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector ALivingRoomActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector ALivingRoomActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

float ALivingRoomActor::FacingYaw(EWall Wall)
{
	// A yaw of A turns local +Y to (-sin A, cos A).
	switch (Wall)
	{
	case EWall::North: return 0.f;
	case EWall::South: return 180.f;
	case EWall::East:  return 90.f;
	default:           return -90.f;
	}
}

bool ALivingRoomActor::IsOnOpening(EWall Wall, float U, float Z, float HalfU, float HalfZ) const
{
	for (const FOpening& Opening : Openings)
	{
		if (Opening.Wall == Wall
			&& FMath::Abs(U - Opening.CenterU) < Opening.HalfU + HalfU
			&& Z > Opening.BottomZ - HalfZ && Z < Opening.TopZ + HalfZ)
		{
			return true;
		}
	}
	return false;
}

TArray<FBox2D> ALivingRoomActor::CutAround(EWall Wall, float U0, float U1, float Z0, float Z1, bool bThroughWallOnly) const
{
	TArray<FBox2D> Holes;
	for (const FOpening& Opening : Openings)
	{
		if (Opening.Wall == Wall && (Opening.bThroughWall || !bThroughWallOnly))
		{
			Holes.Add(FBox2D(FVector2D(Opening.CenterU - Opening.HalfU, Opening.BottomZ), FVector2D(Opening.CenterU + Opening.HalfU, Opening.TopZ)));
		}
	}
	return RoomWalls::CutAround(Holes, U0, U1, Z0, Z1);
}

void ALivingRoomActor::WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud)
{
	// The hall's rotations, which carry the east/west fix: local X along the wall, local Z out of it.
	for (const FBox2D& Piece : CutAround(Wall, U0, U1, Z0, Z1))
	{
		const FVector2D C = Piece.GetCenter();
		const FVector2D S = Piece.GetSize();
		if (S.X < 0.5f || S.Y < 0.5f)
		{
			continue;
		}
		const FVector At = WallPoint(Wall, C.X, C.Y, Proud);
		switch (Wall)
		{
		case EWall::North: Build.Mark(At, FRotator(0.f, 0.f, 90.f), FVector2D(S.X, S.Y), Mat); break;
		case EWall::South: Build.Mark(At, FRotator(0.f, 0.f, -90.f), FVector2D(S.X, S.Y), Mat); break;
		case EWall::East:  Build.Mark(At, FRotator(0.f, 90.f, 90.f), FVector2D(S.X, S.Y), Mat); break;
		default:           Build.Mark(At, FRotator(0.f, 90.f, -90.f), FVector2D(S.X, S.Y), Mat); break;
		}
	}
}

void ALivingRoomActor::WallBox(FRoomBuilder& Build, EWall Wall, float U, float Z, float SizeU, float SizeZ, float Depth, float ProudBase, UMaterialInterface* Mat)
{
	const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
	Build.Box(WallPoint(Wall, U, Z, ProudBase + Depth * 0.5f), FRotator::ZeroRotator,
		bAlongX ? FVector(SizeU, Depth, SizeZ) : FVector(Depth, SizeU, SizeZ), Mat, false);
}

void ALivingRoomActor::AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const
{
	// A decal projects along its own +X, so this is the direction *into* the wall.
	OutLocation = WallPoint(Wall, U, Z, 2.f);
	switch (Wall)
	{
	case EWall::North: OutRotation = FRotator(0.f, -90.f, Roll); break;
	case EWall::South: OutRotation = FRotator(0.f, 90.f, Roll); break;
	case EWall::East:  OutRotation = FRotator(0.f, 0.f, Roll); break;
	default:           OutRotation = FRotator(0.f, 180.f, Roll); break;
	}
}

bool ALivingRoomActor::IsFloorSpotClear(float X, float Y, float Radius) const
{
	if (X < WestX() + Radius || X > EastX() - Radius || Y < NorthY() + Radius || Y > SouthY() - Radius)
	{
		return false;
	}
	const FBox2D Spot(FVector2D(X - Radius, Y - Radius), FVector2D(X + Radius, Y + Radius));
	for (const FBox2D& Footprint : Footprints)
	{
		if (Footprint.Intersect(Spot))
		{
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------

void ALivingRoomActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float Z0 = FloorZ() - 20.f;
	const float Z1 = CeilingZ() + 10.f;

	// Solid, colliding and never seen, as in the hall. The north wall is the hall's south wall and
	// the hall has already built it, door and all.
	auto Run = [&](EWall Wall, float U0, float U1, float Line)
	{
		for (const FBox2D& Piece : CutAround(Wall, U0, U1, Z0, Z1, /*bThroughWallOnly*/ true))
		{
			const FVector2D C = Piece.GetCenter();
			const FVector2D S = Piece.GetSize();
			if (Wall == EWall::North || Wall == EWall::South)
			{
				Build.Box(FVector(C.X, Line, C.Y), FRotator::ZeroRotator, FVector(S.X, T, S.Y), MatShell);
			}
			else
			{
				Build.Box(FVector(Line, C.X, C.Y), FRotator::ZeroRotator, FVector(T, S.X, S.Y), MatShell);
			}
		}
	};
	Run(EWall::South, WestX() - T, EastX() + T, SouthY() + T * 0.5f);
	Run(EWall::East, NorthY() - T, SouthY() + T, EastX() + T * 0.5f);
	Run(EWall::West, NorthY() - T, SouthY() + T, WestX() - T * 0.5f);

	const FVector Span(EastX() - WestX() + T * 2.f, RoomDepth + T * 2.f, 20.f);
	Build.Box(FVector(MidX(), MidY(), FloorZ() - 14.f), FRotator::ZeroRotator, Span, MatShell);
	Build.Box(FVector(MidX(), MidY(), CeilingZ() + 10.f), FRotator::ZeroRotator, Span, MatShell);
}

void ALivingRoomActor::BuildFloor(FRoomBuilder& Build)
{
	// Parquet, laid herringbone, wall to wall: the one floor in the house laid to be shown off. It
	// collides; everything laid on it does not.
	Build.Box(FVector(MidX(), MidY(), FloorZ() - 2.f), FRotator::ZeroRotator, FVector(EastX() - WestX(), RoomDepth, 4.f), MatParquet);

	// A border of straight boards round the edge, which is how a parquet floor was finished — the
	// herringbone stops a hand's width short of the wall and the boards take up the difference.
	const float Border = 22.f;
	const float W = EastX() - WestX();
	Build.Box(FVector(MidX(), NorthY() + Border * 0.5f, FloorZ() + 0.1f), FRotator::ZeroRotator, FVector(W, Border, 0.4f), MatBoards, false);
	Build.Box(FVector(MidX(), SouthY() - Border * 0.5f, FloorZ() + 0.1f), FRotator::ZeroRotator, FVector(W, Border, 0.4f), MatBoards, false);
	Build.Box(FVector(WestX() + Border * 0.5f, MidY(), FloorZ() + 0.1f), FRotator::ZeroRotator, FVector(Border, RoomDepth - Border * 2.f, 0.4f), MatBoards, false);
	Build.Box(FVector(EastX() - Border * 0.5f, MidY(), FloorZ() + 0.1f), FRotator::ZeroRotator, FVector(Border, RoomDepth - Border * 2.f, 0.4f), MatBoards, false);

	// Under the middle window the rain has been coming in for years: blocks lifted, some gone, and
	// the dark of the subfloor where they were. Small blocks, so they are the planks' grain rather
	// than a crop of the herringbone photograph.
	FRandomStream Blocks(3107);
	for (int32 i = 0; i < 22; ++i)
	{
		const FVector2D Spot(WindowX(1) + Blocks.FRandRange(-110.f, 110.f), SouthY() - Border - Blocks.FRandRange(8.f, 120.f));
		const float Yaw = (i % 2 ? 45.f : -45.f) + Blocks.FRandRange(-4.f, 4.f);
		if (Blocks.FRand() < 0.4f)
		{
			Build.Mark(FVector(Spot.X, Spot.Y, FloorZ() - 0.3f), FRotator(0.f, Yaw, 0.f), FVector2D(28.f, 7.f), MatShell);
		}
		else
		{
			Build.Box(FVector(Spot.X, Spot.Y, FloorZ() + 0.9f), FRotator(Blocks.FRandRange(-4.f, 4.f), Yaw, Blocks.FRandRange(-6.f, 6.f)),
				FVector(28.f, 7.f, 1.6f), MatOakDark, false);
		}
	}
}

void ALivingRoomActor::BuildWallFinish(FRoomBuilder& Build)
{
	const float Z0 = FloorZ();
	const float Z1 = CeilingZ();
	const float Dado = Z0 + LivingDado;
	const float Rail = Z0 + LivingPictureRail;

	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Walls[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), SouthY() },
	};

	for (const FRun& R : Walls)
	{
		// Plaster over every face, floor to ceiling. Everything else goes on top of it.
		WallFill(Build, R.Wall, R.U0, R.U1, Z0, Z1, MatPlaster);

		// Panelling to the dado, with a skirting and a dado rail: the hall's joiner, and the hall's
		// oak.
		WallFill(Build, R.Wall, R.U0, R.U1, Z0, Dado, MatWainscot, 0.3f);
		for (const FBox2D& Piece : CutAround(R.Wall, R.U0, R.U1, Z0, Dado + 4.f))
		{
			const FVector2D C = Piece.GetCenter();
			const FVector2D S = Piece.GetSize();
			if (Piece.Min.Y <= Z0 + 1.f && S.X > 1.f)
			{
				WallBox(Build, R.Wall, C.X, Z0 + 11.f, S.X, 22.f, 2.2f, 0.3f, MatOakDark);
			}
			if (Piece.Max.Y >= Dado && S.X > 1.f)
			{
				WallBox(Build, R.Wall, C.X, Dado, S.X, 7.f, 3.6f, 0.3f, MatOak);
			}
		}

		// Paper from the dado to the picture rail, where the paper has held: in runs of strips
		// with bare plaster between them, never in stripes (the bedroom's 09-18 note).
		const float StripWidth = 53.f;
		const int32 Strips = FMath::FloorToInt((R.U1 - R.U0) / StripWidth);
		const int32 Parity = static_cast<int32>(R.Wall) % 2;
		for (int32 Strip = 0; Strip < Strips; ++Strip)
		{
			if ((Strip / 3) % 2 == Parity)
			{
				continue;
			}
			const float A = R.U0 + Strip * StripWidth;
			const float Bottom = Dado + 4.f + Random.FRandRange(0.f, 24.f);
			const float Top = Rail - 3.f - Random.FRandRange(0.f, 30.f);
			WallFill(Build, R.Wall, A, A + StripWidth - 0.3f, Bottom, Top, (Strip % 2) ? MatWallpaper.Get() : MatWallpaperDark.Get(), 0.5f);
		}

		// The picture rail and the cornice. The cornice on the east wall has lost a length, and the
		// length is on the floor (BuildDebris): the plaster behind it is where it tore away.
		auto Band = [&](float ZA, float ZB, float Depth, float GapU0, float GapU1)
		{
			for (const FBox2D& Piece : CutAround(R.Wall, R.U0, R.U1, ZA, ZB))
			{
				const float PieceU0 = Piece.Min.X;
				const float PieceU1 = Piece.Max.X;
				const float Spans[2][2] = { { PieceU0, FMath::Min(PieceU1, GapU0) }, { FMath::Max(PieceU0, GapU1), PieceU1 } };
				for (const auto& Span : Spans)
				{
					if (Span[1] - Span[0] > 1.f)
					{
						WallBox(Build, R.Wall, (Span[0] + Span[1]) * 0.5f, Piece.GetCenter().Y, Span[1] - Span[0], Piece.GetSize().Y, Depth, 0.f, MatOak);
					}
				}
			}
		};
		const bool bEast = R.Wall == EWall::East;
		const float Gap0 = bEast ? CorniceGapU() - 47.f : 1.e6f;
		const float Gap1 = bEast ? CorniceGapU() + 47.f : 1.e6f;
		Band(Rail - 2.f, Rail + 2.f, 2.5f, 1.e6f, 1.e6f);
		Band(Z1 - 16.f, Z1, 14.f, Gap0, Gap1);
		Band(Z1 - 26.f, Z1 - 16.f, 7.f, Gap0, Gap1);
	}

	// Moulded panels between the dado and the picture rail on the two walls with nothing in front of
	// them: the thin applied frames a room like this was divided up with. The oval portrait over
	// the commode hangs in the middle of one; the covered portrait on the east wall in another.
	auto Panel = [&](EWall Wall, float U0, float U1, float ZA, float ZB)
	{
		const float Frame = 5.f;
		WallBox(Build, Wall, (U0 + U1) * 0.5f, ZA + Frame * 0.5f, U1 - U0, Frame, 2.4f, 0.f, MatOak);
		WallBox(Build, Wall, (U0 + U1) * 0.5f, ZB - Frame * 0.5f, U1 - U0, Frame, 2.4f, 0.f, MatOak);
		WallBox(Build, Wall, U0 + Frame * 0.5f, (ZA + ZB) * 0.5f, Frame, ZB - ZA, 2.4f, 0.f, MatOak);
		WallBox(Build, Wall, U1 - Frame * 0.5f, (ZA + ZB) * 0.5f, Frame, ZB - ZA, 2.4f, 0.f, MatOak);
	};
	Panel(EWall::North, WestX() + 80.f, WestX() + 420.f, Dado + 40.f, Rail - 30.f);
	Panel(EWall::East, CoveredPortraitY() - 180.f, CoveredPortraitY() + 180.f, Dado + 40.f, Rail - 30.f);
	Panel(EWall::East, CoveredPortraitY() + 230.f, SouthY() - 90.f, Dado + 40.f, Rail - 30.f);

	// The door from the hall, cased on this side too.
	{
		const float U = Setup.DoorX;
		const float H = Z0 + Setup.DoorHeight;
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, EWall::North, U + S * (Setup.DoorHalf + 6.f), (Z0 + H + 12.f) * 0.5f, 12.f, H + 12.f - Z0, 3.f, 0.3f, MatOak);
		}
		WallBox(Build, EWall::North, U, H + 6.f, Setup.DoorHalf * 2.f + 24.f, 12.f, 3.f, 0.3f, MatOak);
		WallBox(Build, EWall::North, U, H + 15.f, Setup.DoorHalf * 2.f + 34.f, 6.f, 5.f, 0.3f, MatOak);
	}

	// An architrave round each window, and a panelled apron under the sill down to the skirting.
	for (int32 i = 0; i < 3; ++i)
	{
		const float U = WindowX(i);
		const float Sill = Z0 + WindowSill;
		const float Top = Z0 + WindowTop;
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, EWall::South, U + S * (WindowWidth * 0.5f + 7.f), (Sill + Top + 14.f) * 0.5f, 14.f, Top + 14.f - Sill, 3.2f, 0.f, MatOak);
		}
		WallBox(Build, EWall::South, U, Top + 7.f, WindowWidth + 28.f, 14.f, 3.2f, 0.f, MatOak);
		WallBox(Build, EWall::South, U, Top + 17.f, WindowWidth + 40.f, 6.f, 5.5f, 0.f, MatOak);
	}
}

void ALivingRoomActor::BuildCeiling(FRoomBuilder& Build)
{
	const float C = CeilingZ();
	Build.Mark(FVector(MidX(), MidY(), C), FRotator(0.f, 0.f, 180.f), FVector2D(EastX() - WestX(), RoomDepth), MatCeiling);

	// A plaster rose over the carpet, where the chandelier hangs: three stepped rings and a boss.
	const FVector Rose(LoungeX(), HearthY(), C);
	Build.Cyl(Rose - FVector(0.f, 0.f, 1.5f), FRotator::ZeroRotator, FVector(76.f, 76.f, 3.f), MatCeiling, false);
	Build.Cyl(Rose - FVector(0.f, 0.f, 4.f), FRotator::ZeroRotator, FVector(54.f, 54.f, 3.f), MatCeiling, false);
	Build.Cyl(Rose - FVector(0.f, 0.f, 7.f), FRotator::ZeroRotator, FVector(30.f, 30.f, 4.f), MatCeiling, false);
	Build.Sph(Rose - FVector(0.f, 0.f, 9.f), 14.f, MatCeiling);

	// Water through the ceiling: brown blooms spreading from the window wall, and one big stain
	// over the corner by the piano where the roof has been letting the storm in longest.
	for (int32 i = 0; i < 9; ++i)
	{
		const float Y = (i < 4) ? SouthY() - Random.FRandRange(30.f, 260.f) : Random.FRandRange(NorthY() + 60.f, SouthY() - 60.f);
		Build.Stain(RoomSurfaces::Damp, FVector(Random.FRandRange(WestX() + 60.f, EastX() - 60.f), Y, C - 2.f), FRotator(90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(110.f, 260.f), Random.FRandRange(80.f, 190.f)), FLinearColor(0.32f, 0.27f, 0.21f), Random.FRandRange(0.35f, 0.6f), 1.2f);
	}
	Build.Stain(RoomSurfaces::Damp, FVector(EastX() - 200.f, SouthY() - 170.f, C - 2.f), FRotator(90.f, 0.f, 30.f), FVector2D(320.f, 240.f),
		FLinearColor(0.20f, 0.16f, 0.12f), 0.7f, 1.2f);
	Build.Crack(FVector(MidX() + 60.f, MidY() - 90.f, C - 2.f), FRotator(90.f, 0.f, 20.f), FVector2D(380.f, 200.f), 0.8f, 16.f);

	// Where the plaster has come down, over by the east wall: the lath shows through, and what fell
	// is on the parquet under it (BuildDebris).
	Build.Stain(RoomSurfaces::Substrate, FVector(EastX() - 150.f, NorthY() + 330.f, C - 2.f), FRotator(90.f, 0.f, 15.f), FVector2D(90.f, 70.f),
		FLinearColor(0.20f, 0.17f, 0.14f), 1.f, 0.9f);
	for (int32 i = 0; i < 6; ++i)
	{
		Build.Box(FVector(EastX() - 150.f + Random.FRandRange(-30.f, 30.f), NorthY() + 330.f + (i - 2.5f) * 9.f, C - 1.5f), FRotator(0.f, 15.f + Random.FRandRange(-3.f, 3.f), 0.f),
			FVector(Random.FRandRange(50.f, 80.f), 3.f, 1.2f), MatOakDark, false);
	}

	// Cobwebs in the four upper corners, across the corner, and along the cornice.
	const FVector2D Corners[4] = { { WestX(), NorthY() }, { EastX(), NorthY() }, { WestX(), SouthY() }, { EastX(), SouthY() } };
	for (const FVector2D& Corner : Corners)
	{
		const float SX = Corner.X < MidX() ? 1.f : -1.f;
		const float SY = Corner.Y < MidY() ? 1.f : -1.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Inset = 26.f + i * 22.f;
			Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SX * Inset, Corner.Y + SY * Inset, C - 30.f - i * 8.f),
				FRotator(0.f, SX * SY > 0.f ? 45.f : -45.f, 180.f), FVector(Inset * 1.7f, Inset * 1.7f, 1.f), MatWeb, false);
		}
	}
	for (int32 i = 0; i < 8; ++i)
	{
		const bool bAlongX = i % 2 == 0;
		const FVector At = bAlongX
			? FVector(Random.FRandRange(WestX() + 80.f, EastX() - 80.f), i % 4 == 0 ? NorthY() + 12.f : SouthY() - 12.f, C - 26.f)
			: FVector(i % 4 == 1 ? WestX() + 12.f : EastX() - 12.f, Random.FRandRange(NorthY() + 80.f, SouthY() - 80.f), C - 26.f);
		Build.Add(FRoomShapes::Plane(), At, FRotator(Random.FRandRange(55.f, 75.f), bAlongX ? 90.f : 0.f, 0.f), FVector(Random.FRandRange(40.f, 80.f), 30.f, 1.f), MatWeb, false);
	}
}

void ALivingRoomActor::BuildFireplace(FRoomBuilder& Build)
{
	// A chimney breast of dressed stone, floor to ceiling, standing sixty centimetres into the room
	// in the middle of the west wall: the thing the room is arranged round, and the first thing in
	// it that is plainly older than the house around it.
	const float F = FloorZ();
	const float C = CeilingZ();
	const float H = HearthY();
	const float Face = BreastX();
	const float HalfW = BreastWidth * 0.5f;
	const float HalfBox = FireboxWidth * 0.5f;
	const float BoxTop = F + FireboxHeight;
	const float BoxBack = Face - FireboxDepth;

	// The masonry: two piers, the stack over the opening, and the back of the firebox. These
	// collide and are what the lantern sees on the sides of the breast.
	const float MidDepthX = (WestX() + Face) * 0.5f;
	Build.Box(FVector(MidDepthX, H - (HalfW + HalfBox) * 0.5f, (F + C) * 0.5f), FRotator::ZeroRotator, FVector(BreastDepth, HalfW - HalfBox, C - F), MatStone);
	Build.Box(FVector(MidDepthX, H + (HalfW + HalfBox) * 0.5f, (F + C) * 0.5f), FRotator::ZeroRotator, FVector(BreastDepth, HalfW - HalfBox, C - F), MatStone);
	Build.Box(FVector(MidDepthX, H, (BoxTop + C) * 0.5f), FRotator::ZeroRotator, FVector(BreastDepth, FireboxWidth, C - BoxTop), MatStone);
	Build.Box(FVector((WestX() + BoxBack) * 0.5f, H, (F + BoxTop) * 0.5f), FRotator::ZeroRotator, FVector(BoxBack - WestX(), FireboxWidth, FireboxHeight), MatSoot);

	// The face of it, as one sheet with the courses running on across the opening.
	const FVector Origin(Face + 0.3f, H + HalfW, 0.f);
	const TArray<FBox2D> Pieces = {
		FBox2D(FVector2D(0.f, F), FVector2D(HalfW - HalfBox, C)),
		FBox2D(FVector2D(HalfW + HalfBox, F), FVector2D(BreastWidth, C)),
		FBox2D(FVector2D(HalfW - HalfBox, BoxTop), FVector2D(HalfW + HalfBox, C)),
	};
	LivingFacade(this, RoomRoot, Pieces, Origin, FVector(0.f, -1.f, 0.f), FVector(1.f, 0.f, 0.f), RoomSurfaces::Stone.TexelSizeCm, MatStoneSheet);

	// Inside the firebox: sooted cheeks, a sooted soffit, a dark throat at the back of it, and the
	// inner hearth. Soot is black at the back and brown towards the mouth.
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector((BoxBack + Face) * 0.5f, H + S * (HalfBox - 1.f), (F + BoxTop) * 0.5f), FRotator::ZeroRotator, FVector(FireboxDepth, 2.f, FireboxHeight), MatSoot, false);
	}
	Build.Box(FVector((BoxBack + Face) * 0.5f, H, BoxTop - 1.f), FRotator::ZeroRotator, FVector(FireboxDepth, FireboxWidth, 2.f), MatSoot, false);
	Build.Box(FVector(BoxBack + 6.f, H, BoxTop - 6.f), FRotator::ZeroRotator, FVector(10.f, FireboxWidth - 16.f, 8.f), MatShadow, false);
	Build.Box(FVector((BoxBack + Face) * 0.5f, H, F + 1.f), FRotator::ZeroRotator, FVector(FireboxDepth, FireboxWidth, 2.f), MatSoot, false);

	// The surround: plinths, jambs, a lintel with a keystone, a frieze, and the mantel shelf on it.
	// Every piece its own stone, standing proud of the breast.
	for (const float S : { -1.f, 1.f })
	{
		const float Y = H + S * (HalfBox + 16.f);
		Build.Box(FVector(Face + 8.f, Y, F + 9.f), FRotator::ZeroRotator, FVector(16.f, 36.f, 18.f), MatStone);
		Build.Box(FVector(Face + 7.f, Y, (F + 18.f + BoxTop) * 0.5f), FRotator::ZeroRotator, FVector(14.f, 32.f, BoxTop - F - 18.f), MatStone);
		// A sunk panel down each jamb.
		Build.Box(FVector(Face + 14.2f, Y, (F + 28.f + BoxTop - 10.f) * 0.5f), FRotator::ZeroRotator, FVector(0.6f, 18.f, BoxTop - F - 38.f), MatStoneDark, false);
	}
	Build.Box(FVector(Face + 7.f, H, BoxTop + 14.f), FRotator::ZeroRotator, FVector(14.f, FireboxWidth + 64.f, 28.f), MatStone);
	Build.Box(FVector(Face + 9.f, H, BoxTop + 14.f), FRotator::ZeroRotator, FVector(16.f, 22.f, 32.f), MatStone, false);
	Build.Box(FVector(Face + 11.f, H, F + MantelHeight - 8.f), FRotator::ZeroRotator, FVector(22.f, FireboxWidth + 104.f, 4.f), MatStone, false);
	Build.Box(FVector(Face + 14.f, H, F + MantelHeight - 3.f), FRotator::ZeroRotator, FVector(28.f, FireboxWidth + 114.f, 6.f), MatStone);
	for (const float S : { -1.f, 1.f })
	{
		// Corbels under the ends of the shelf.
		Build.Box(FVector(Face + 9.f, H + S * (HalfBox + 16.f), F + MantelHeight - 13.f), FRotator::ZeroRotator, FVector(18.f, 26.f, 8.f), MatStone, false);
	}

	// The hearth: a marble slab in front of the opening, cracked across, with a brass fender.
	Build.Box(FVector(Face + 30.f, H, F + 2.f), FRotator::ZeroRotator, FVector(60.f, FireboxWidth + 100.f, 4.f), MatMarble);
	Build.Crack(FVector(Face + 30.f, H + 20.f, F + 10.f), FRotator(-90.f, 0.f, 70.f), FVector2D(70.f, 40.f), 0.9f, 30.f);
	Build.Box(FVector(Face + 58.f, H, F + 12.f), FRotator::ZeroRotator, FVector(2.f, FireboxWidth + 84.f, 2.4f), MatBrass, false);
	Build.Box(FVector(Face + 58.f, H, F + 6.f), FRotator::ZeroRotator, FVector(1.f, FireboxWidth + 84.f, 6.f), MatBrass, false);
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(Face + 32.f, H + S * (FireboxWidth * 0.5f + 42.f), F + 8.f), FRotator::ZeroRotator, FVector(52.f, 1.f, 8.f), MatBrass, false);
		Build.Sph(FVector(Face + 58.f, H + S * (FireboxWidth * 0.5f + 42.f), F + 13.f), 4.f, MatBrass);
	}

	// The grate, and in it the last fire: three logs burnt through to charcoal and never raked out,
	// and a drift of ash. It is cold. It has been cold for a long time.
	const FVector Grate(BoxBack + 22.f, H, F);
	for (int32 i = 0; i < 6; ++i)
	{
		const float Y = -32.f + i * 12.8f;
		Build.Cyl(Grate + FVector(12.f, Y, 16.f), FRotator::ZeroRotator, FVector(1.6f, 1.6f, 22.f), MatIron, false);
	}
	Build.Box(Grate + FVector(12.f, 0.f, 25.f), FRotator::ZeroRotator, FVector(2.f, 70.f, 2.f), MatIron, false);
	Build.Box(Grate + FVector(0.f, 0.f, 9.f), FRotator::ZeroRotator, FVector(26.f, 66.f, 1.5f), MatIron, false);
	for (const FVector2D& Leg : { FVector2D(-10.f, -30.f), FVector2D(-10.f, 30.f), FVector2D(12.f, -32.f), FVector2D(12.f, 32.f) })
	{
		Build.Cyl(Grate + FVector(Leg.X, Leg.Y, 4.5f), FRotator::ZeroRotator, FVector(2.f, 2.f, 9.f), MatIron, false);
	}
	struct FLog { float X; float Y; float Z; float Yaw; float Roll; float Length; float Girth; };
	const FLog Logs[] = {
		{ -2.f, -4.f, 15.f, 8.f, 0.f, 52.f, 10.f },
		{ 5.f, 10.f, 21.f, -14.f, 6.f, 44.f, 8.5f },
		{ -6.f, 16.f, 12.f, 70.f, 0.f, 30.f, 7.f },
	};
	for (const FLog& L : Logs)
	{
		Build.Cyl(Grate + FVector(L.X, L.Y, L.Z), FRotator(0.f, L.Yaw, 90.f + L.Roll), FVector(L.Girth, L.Girth, L.Length), MatCharred, false);
	}
	FRandomStream Ash(2911);
	for (int32 i = 0; i < 14; ++i)
	{
		Build.Add(FRoomShapes::Sphere(), Grate + FVector(Ash.FRandRange(-16.f, 18.f), Ash.FRandRange(-34.f, 34.f), 2.5f), FRotator(0.f, Ash.FRandRange(0.f, 180.f), 0.f),
			FVector(Ash.FRandRange(10.f, 22.f), Ash.FRandRange(8.f, 16.f), Ash.FRandRange(2.f, 5.f)), MatAsh, false);
	}
	// Ash walked out across the hearth, and soot licked up the stone over the opening.
	Build.Stain(RoomSurfaces::Damp, FVector(Face + 24.f, H, F + 10.f), FRotator(-90.f, 0.f, 0.f), FVector2D(150.f, 70.f), FLinearColor(0.42f, 0.40f, 0.38f), 0.45f, 1.3f);
	Build.Stain(RoomSurfaces::Damp, FVector(Face + 6.f, H, BoxTop + 30.f), FRotator(0.f, 180.f, 0.f), FVector2D(110.f, 90.f), FLinearColor(0.05f, 0.045f, 0.04f), 0.75f, 1.2f);

	// The fire irons on their stand by the south jamb, one of them gone: the poker is leaning on the
	// breast, where it was put down.
	const FVector Stand(Face + 20.f, H + HalfW - 18.f, F);
	Build.Cyl(Stand + FVector(0.f, 0.f, 1.f), FRotator::ZeroRotator, FVector(20.f, 20.f, 2.f), MatIron, false);
	Build.Cyl(Stand + FVector(0.f, 0.f, 37.f), FRotator::ZeroRotator, FVector(2.2f, 2.2f, 72.f), MatIron, false);
	Build.Sph(Stand + FVector(0.f, 0.f, 75.f), 4.5f, MatBrass);
	Build.Box(Stand + FVector(0.f, 0.f, 68.f), FRotator::ZeroRotator, FVector(1.5f, 18.f, 1.5f), MatIron, false);
	for (const float S : { -1.f, 1.f })
	{
		// A shovel on one hook, a brush on the other.
		const FVector Hook = Stand + FVector(0.f, S * 8.f, 66.f);
		Build.Cyl(Hook - FVector(0.f, 0.f, 26.f), FRotator::ZeroRotator, FVector(1.4f, 1.4f, 52.f), MatIron, false);
		Build.Sph(Hook + FVector(0.f, 0.f, 1.f), 3.f, MatBrass);
		if (S < 0.f)
		{
			Build.Box(Hook - FVector(0.f, 0.f, 58.f), FRotator(0.f, 0.f, 0.f), FVector(1.f, 10.f, 13.f), MatIron, false);
		}
		else
		{
			Build.Cyl(Hook - FVector(0.f, 0.f, 58.f), FRotator::ZeroRotator, FVector(6.f, 6.f, 12.f), MatShadow, false);
		}
	}
	Build.Cyl(FVector(Face + 3.f, H + HalfW - 50.f, F + 34.f), FRotator(0.f, 0.f, -8.f), FVector(1.6f, 1.6f, 70.f), MatIron, false);
	Build.Sph(FVector(Face + 3.f, H + HalfW - 45.f, F + 69.f), 3.4f, MatBrass);
	// The tongs, dropped on the hearth.
	Build.Cyl(FVector(Face + 40.f, H - 50.f, F + 5.f), FRotator(90.f, 64.f, 0.f), FVector(1.3f, 1.3f, 50.f), MatIron, false);
	Build.Cyl(FVector(Face + 42.f, H - 47.f, F + 5.f), FRotator(90.f, 58.f, 0.f), FVector(1.3f, 1.3f, 48.f), MatIron, false);

	// A log basket by the north jamb, still with its logs: somebody meant to light another fire.
	const FVector Basket(Face + 26.f, H - HalfW + 12.f, F);
	Build.Box(Basket + FVector(0.f, 0.f, 2.f), FRotator::ZeroRotator, FVector(34.f, 44.f, 2.f), MatIron, false);
	for (const FVector2D& Post : { FVector2D(-16.f, -21.f), FVector2D(16.f, -21.f), FVector2D(-16.f, 21.f), FVector2D(16.f, 21.f) })
	{
		Build.Cyl(Basket + FVector(Post.X, Post.Y, 14.f), FRotator::ZeroRotator, FVector(1.6f, 1.6f, 28.f), MatIron, false);
	}
	for (const float Z : { 14.f, 27.f })
	{
		for (const float S : { -1.f, 1.f })
		{
			Build.Box(Basket + FVector(S * 16.f, 0.f, Z), FRotator::ZeroRotator, FVector(1.2f, 44.f, 1.2f), MatIron, false);
			Build.Box(Basket + FVector(0.f, S * 21.f, Z), FRotator::ZeroRotator, FVector(34.f, 1.2f, 1.2f), MatIron, false);
		}
	}
	for (int32 i = 0; i < 5; ++i)
	{
		Build.Cyl(Basket + FVector(-9.f + (i % 3) * 9.f, 0.f, 9.f + (i / 3) * 8.f), FRotator(0.f, Random.FRandRange(-6.f, 6.f), 90.f),
			FVector(8.f, 8.f, 50.f), MatOakDark, false);
	}

	// On the mantel: the brass candlesticks with their candles burnt out, a vase with nothing in
	// it, and the clean oblong in the dust where the clock stood (the clock is on the hearth — see
	// BuildClues). brass_candleholders is a set of three pieces, a metre of mantel on its own: one
	// set, towards the south end. Two made a shop window of it.
	const float Top = F + MantelHeight;
	{
		if (UStaticMeshComponent* Candles = Build.PropSeated(RoomProps::Candelabra, FVector(Face + 13.f, H + 52.f, Top), FRotator(0.f, 90.f, 0.f), 0.f, false))
		{
			// Modelled lit, like the one in the hall: the flames go black, which is a wick.
			Candles->SetMaterial(1, MatShadow);
			FRoomShapes::TintSlots(Candles, FLinearColor(0.45f, 0.40f, 0.34f), 0);
			FRoomShapes::TintSlots(Candles, FLinearColor(0.45f, 0.40f, 0.34f), 2);
			FRoomShapes::TintSlots(Candles, FLinearColor(0.50f, 0.46f, 0.38f), 3);
			FRoomShapes::TintSlots(Candles, FLinearColor(0.45f, 0.40f, 0.34f), 4);
		}
	}
	if (UStaticMeshComponent* Vase = Build.PropSeated(RoomProps::Vase, FVector(Face + 14.f, H - 62.f, Top), FRotator(0.f, 30.f, 0.f), 30.f, false))
	{
		FRoomShapes::TintSlots(Vase, FLinearColor(0.42f, 0.40f, 0.37f));
	}
	// Aimed down (pitch -90), a decal's first size is along world Y and its second along world X.
	Build.Stain(RoomSurfaces::Damp, FVector(Face + 14.f, H, Top + 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(230.f, 24.f), FLinearColor(0.40f, 0.38f, 0.35f), 0.4f, 1.1f);
	Build.Mark(FVector(Face + 14.f, H + 8.f, Top - 0.5f), FRotator(0.f, 90.f, 0.f), FVector2D(34.f, 12.f), MatStone);

	// Behind the fire irons and the basket, the breast's sides are stone to the ceiling too.
	Footprints.Add(FBox2D(FVector2D(WestX(), H - HalfW - 10.f), FVector2D(Face + 64.f, H + HalfW + 10.f)));
}

void ALivingRoomActor::BuildTelevision(FRoomBuilder& Build)
{
	// A seventy-five inch set on a wall bracket over the mantel: a black glass rectangle a metre and
	// two-thirds across, on a stone chimney breast older than anything else in the house. Off. The
	// finale is when it comes on; until then it is the one thing in the room that has not aged,
	// and the lantern finds itself in the screen before it finds anything else on this wall.
	const float Face = BreastX();
	const float H = HearthY();
	const float CentreZ = FloorZ() + 235.f;
	const float Width = 168.f;
	const float Height = 96.f;

	// The bracket and the box of electronics behind the panel, then the panel, then the glass —
	// set a centimetre and a half high in its bezel, because every set has the thicker chin.
	Build.Box(FVector(Face + 3.f, H, CentreZ), FRotator::ZeroRotator, FVector(6.f, 64.f, 40.f), MatPlastic, false);
	Build.Box(FVector(Face + 7.2f, H, CentreZ), FRotator::ZeroRotator, FVector(2.4f, Width, Height), MatPlastic, false);
	Build.Box(FVector(Face + 8.6f, H, CentreZ + 1.4f), FRotator::ZeroRotator, FVector(0.4f, Width - 2.4f, Height - 5.2f), MatScreen, false);

	// Its cable, which nobody hid: down onto the mantel shelf, along it past the end, over the edge,
	// down the stone to the floor, round the corner of the breast and into a socket on the wall.
	const float Bottom = CentreZ - Height * 0.5f;
	const float Shelf = FloorZ() + MantelHeight + 0.4f;
	const float CableX = Face + 5.f;
	const float Corner = H - BreastWidth * 0.5f - 3.f;
	const FVector Route[] = {
		FVector(CableX, H - 70.f, Bottom + 1.f),
		FVector(CableX, H - 70.f, Shelf),
		FVector(CableX, H - 116.f, Shelf),
		FVector(Face + 1.2f, H - 122.f, Shelf - 12.f),
		FVector(Face + 0.9f, H - 123.f, FloorZ() + 0.5f),
		FVector(Face + 0.9f, Corner, FloorZ() + 0.5f),
		FVector(WestX() + 1.2f, Corner, FloorZ() + 0.5f),
		FVector(WestX() + 1.2f, Corner, FloorZ() + 26.f),
	};
	for (int32 i = 0; i + 1 < UE_ARRAY_COUNT(Route); ++i)
	{
		const FVector Step = Route[i + 1] - Route[i];
		Build.Cyl((Route[i] + Route[i + 1]) * 0.5f, FRotationMatrix::MakeFromZ(Step.GetSafeNormal()).Rotator(), FVector(0.8f, 0.8f, Step.Size() + 0.6f), MatPlastic, false);
	}
	const FVector Socket = WallPoint(EWall::West, Corner, FloorZ() + 30.f, 0.f);
	Build.Box(Socket + FVector(0.5f, 0.f, 0.f), FRotator::ZeroRotator, FVector(1.f, 8.f, 8.f), MatWax, false);
	Build.Box(Socket + FVector(2.f, 0.f, -2.f), FRotator::ZeroRotator, FVector(3.f, 4.f, 5.f), MatPlastic, false);
}

void ALivingRoomActor::BuildSeating(FRoomBuilder& Build)
{
	// The fireside: a carpet in front of the hearth, the big sofa facing the fire across it, a
	// smaller one along its south side, a green armchair either side of the hearth turned in to the
	// room, and the coffee table in the middle of the four. How a family sat, in a room like this,
	// before there was anything over the fireplace to look at.
	const float F = FloorZ();
	const float H = HearthY();
	const float LX = LoungeX();

	// The carpet: a woven field on a darker border, faded, worn through here and there to the
	// backing, and flat — the one thing in the room the damp has not lifted.
	Build.Cloth(FVector(LX, H, F + 0.8f), FRotator(0.f, 0.6f, 0.f), FVector2D(470.f, 410.f), 0.25f, 0.f, 6101, MatCarpetBorder, 40.f);
	Build.Cloth(FVector(LX, H, F + 2.f), FRotator(0.f, 0.6f, 0.f), FVector2D(426.f, 366.f), 0.25f, 0.f, 6102, MatCarpet, RoomSurfaces::Carpet.TexelSizeCm);
	Footprints.Add(FBox2D(FVector2D(LX - 235.f, H - 205.f), FVector2D(LX + 235.f, H + 205.f)));

	// The sofa, facing the fire. sofa_03 is a carved Victorian settee in a gold brocade; a green
	// that dark on a pattern that busy is what a velvet looks like after sixty years in the dark.
	// The upholstery is far paler than the atlas's average (the dark wood pulls the mean down), so
	// the tint is set from how it came out, not from the mean: a third of the first value.
	if (UStaticMeshComponent* Sofa = Build.PropSeated(RoomProps::Sofa, FVector(LX + 235.f, H, F), FRotator(0.f, 90.f, 0.f), 100.f))
	{
		FRoomShapes::TintSlots(Sofa, FLinearColor(0.075f, 0.27f, 0.30f), 0);
		FRoomShapes::TintSlots(Sofa, FLinearColor(0.07f, 0.21f, 0.24f), 1);
		LivingMatte(Sofa, 1.7f);
	}
	Footprints.Add(FBox2D(FVector2D(LX + 190.f, H - 125.f), FVector2D(LX + 280.f, H + 125.f)));

	// The smaller sofa, its back to the windows. Sofa_01 ships in a pale linen; the same green.
	if (UStaticMeshComponent* Sofa = Build.PropSeated(RoomProps::Settee, FVector(LX + 20.f, H + 215.f, F), FRotator(0.f, 181.f, 0.f), 85.f))
	{
		FRoomShapes::TintSlots(Sofa, FLinearColor(0.12f, 0.26f, 0.19f));
		LivingMatte(Sofa, 1.7f);
	}
	Footprints.Add(FBox2D(FVector2D(LX - 65.f, H + 178.f), FVector2D(LX + 105.f, H + 252.f)));

	// The two armchairs, one either side of the hearth, turned in towards the room.
	for (const float S : { -1.f, 1.f })
	{
		const FVector Seat(LX - 135.f, H + S * 185.f, F);
		if (UStaticMeshComponent* Chair = Build.PropSeated(RoomProps::GreenChair, Seat, FRotator(0.f, S < 0.f ? -35.f : 215.f, 0.f), 0.f))
		{
			FRoomShapes::TintSlots(Chair, FLinearColor(0.71f, 1.43f, 1.30f));
			LivingMatte(Chair, 1.7f);
		}
		Footprints.Add(FBox2D(FVector2D(Seat.X - 48.f, Seat.Y - 48.f), FVector2D(Seat.X + 48.f, Seat.Y + 48.f)));
	}

	// The coffee table, and on it a cup somebody left, the tea dried to a ring in the bottom of it,
	// and three drops that have fallen off the chandelier over it.
	if (UStaticMeshComponent* Table = Build.PropSeated(RoomProps::CoffeeTable, FVector(LX, H, F), FRotator(0.f, 3.f, 0.f), 46.f))
	{
		FRoomShapes::TintSlots(Table, FLinearColor(1.1f, 1.05f, 1.f));
	}
	const float TableTop = F + 46.f;
	UMaterialInterface* China = Build.Flat(FLinearColor(0.20f, 0.19f, 0.17f), 0.35f);
	const FVector Cup(LX - 24.f, H + 28.f, TableTop);
	Build.Cyl(Cup + FVector(0.f, 0.f, 0.5f), FRotator::ZeroRotator, FVector(14.f, 14.f, 1.f), China, false);
	Build.Cyl(Cup + FVector(0.f, 0.f, 4.5f), FRotator::ZeroRotator, FVector(8.f, 8.f, 7.f), China, false);
	Build.Cyl(Cup + FVector(0.f, 0.f, 8.05f), FRotator::ZeroRotator, FVector(7.f, 7.f, 0.1f), MatCharred, false);
	Build.Box(Cup + FVector(5.f, 0.f, 5.f), FRotator::ZeroRotator, FVector(2.f, 0.8f, 4.f), China, false);
	for (int32 i = 0; i < 3; ++i)
	{
		Build.Add(FRoomShapes::Cone(), FVector(LX + 10.f + i * 17.f, H - 20.f + i * 9.f, TableTop + 1.2f), FRotator(90.f, 40.f + i * 70.f, 0.f), FVector(2.4f, 2.4f, 6.f), MatGlass, false);
	}
	Footprints.Add(FBox2D(FVector2D(LX - 62.f, H - 62.f), FVector2D(LX + 62.f, H + 62.f)));

	// The lamp table at the end of the sofa, and the oil lamp on it, empty and never lit again.
	const FVector LampTable = LampTableSeat();
	// round_wooden_table_01 is a pedestal table as wide as it is tall; at seventy it was a dining
	// table standing at the end of the sofa.
	if (UStaticMeshComponent* Table = Build.PropSeated(RoomProps::LampTable, LampTable, FRotator::ZeroRotator, LampTableHeight))
	{
		FRoomShapes::TintSlots(Table, FLinearColor(0.62f, 0.95f, 1.2f));
	}
	if (UStaticMeshComponent* Lamp = Build.PropSeated(RoomProps::OilLamp, LampTable + FVector(-10.f, -6.f, LampTableHeight), FRotator(0.f, 20.f, 0.f), 50.f, false))
	{
		// Modelled lit. The flame goes to glass, which is nearly nothing: a lamp this long out has
		// no flame in it, not a black one.
		Lamp->SetMaterial(2, MatGlass);
		FRoomShapes::TintSlots(Lamp, FLinearColor(0.50f, 0.45f, 0.38f), 1);
	}
	Footprints.Add(FBox2D(FVector2D(LampTable.X - 44.f, LampTable.Y - 44.f), FVector2D(LampTable.X + 44.f, LampTable.Y + 44.f)));
}

void ALivingRoomActor::BuildPiano(FRoomBuilder& Build)
{
	// A black grand in the corner by the windows, its tail into the corner and its keyboard to the
	// room: the storm is behind it, so every strike stands it out as a silhouette before the lantern
	// has found it. The lid is down and furred with dust; the front flap is folded back over it and
	// the music desk is up, because the last thing anybody did at it was play.
	//
	// Nothing on Poly Haven is a piano, and primitives cannot be one — the case is the one curve in
	// the house that is neither a circle nor a straight line — so the case and the lid are extruded
	// from an outline (LivingPrism) and everything else is the usual boxes.
	//
	// Local frame: +X from the keyboard towards the tail, +Y to the right of whoever is sitting at
	// it, so the long straight bass side is on -Y. The origin is on the floor under the front edge.
	USceneComponent* Root = LivingPivot(this, RoomRoot, PianoOrigin(), FRotator(0.f, PianoYaw, 0.f), TEXT("Piano"));
	FRoomBuilder P(this, Root);

	// The outline: bass side, round the tail, the bentside's S back to the treble cheek, across the
	// front. Written out rather than sampled, because the proportions are the whole of the shape.
	TArray<FVector2D> Outline;
	Outline.Add(FVector2D(0.f, -75.f));
	Outline.Add(FVector2D(175.f, -75.f));
	for (int32 i = 1; i <= 10; ++i)
	{
		const float A = FMath::DegreesToRadians(-90.f + 180.f * i / 10.f);
		Outline.Add(FVector2D(175.f + 28.f * FMath::Cos(A), -47.f + 28.f * FMath::Sin(A)));
	}
	const FVector2D B0(175.f, -19.f);
	const FVector2D B1(132.f, -19.f);
	const FVector2D B2(92.f, 75.f);
	const FVector2D B3(50.f, 75.f);
	for (int32 i = 1; i <= 16; ++i)
	{
		const float T = i / 16.f;
		const float U = 1.f - T;
		Outline.Add(B0 * (U * U * U) + B1 * (3.f * U * U * T) + B2 * (3.f * U * T * T) + B3 * (T * T * T));
	}
	Outline.Add(FVector2D(0.f, 75.f));

	const float CaseBottom = 62.f;
	const float LidTop = PianoCaseTop + 2.5f;
	LivingPrism(this, Root, Outline, CaseBottom, PianoCaseTop, 60.f, MatLacquer);

	// The lid over everything behind the front flap, and the flap folded back on top of it.
	TArray<FVector2D> Lid;
	for (const FVector2D& Point : Outline)
	{
		if (Point.X >= 30.f)
		{
			Lid.Add(Point);
		}
	}
	Lid.Insert(FVector2D(30.f, -75.f), 0);
	Lid.Add(FVector2D(30.f, 75.f));
	LivingPrism(this, Root, Lid, PianoCaseTop, LidTop, 60.f, MatLacquer);
	P.Box(FVector(45.f, 0.f, LidTop + 1.25f), FRotator::ZeroRotator, FVector(30.f, 148.f, 2.5f), MatLacquer, false);
	for (const float Y : { -52.f, 0.f, 52.f })
	{
		P.Box(FVector(30.f, Y, LidTop + 0.4f), FRotator::ZeroRotator, FVector(2.f, 9.f, 1.f), MatBrass, false);
	}
	for (const float X : { 80.f, 150.f })
	{
		P.Box(FVector(X, -75.5f, PianoCaseTop + 0.5f), FRotator::ZeroRotator, FVector(10.f, 1.2f, 3.f), MatBrass, false);
	}

	// The dust on it, and the rings where two things stood and were taken away.
	P.Stain(RoomSurfaces::Damp, FVector(120.f, -28.f, LidTop + 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(160.f, 110.f), FLinearColor(0.40f, 0.38f, 0.35f), 0.42f, 1.3f);
	P.Stain(RoomSurfaces::Damp, FVector(46.f, 0.f, LidTop + 8.f), FRotator(-90.f, 0.f, 90.f), FVector2D(140.f, 26.f), FLinearColor(0.40f, 0.38f, 0.35f), 0.35f, 1.2f);

	// The keyboard: a key bed standing out in front of the case between two cheek blocks, fifty-two
	// naturals and thirty-six sharps. Two naturals are gone and show the dark of the key bed, three
	// have gone down and not come back up, and a few have lost their ivory.
	const int32 Naturals = 52;
	const float KeyPitch = 2.3f;
	const float KeyLeft = -Naturals * KeyPitch * 0.5f;
	P.Box(FVector(-9.f, 0.f, 66.f), FRotator::ZeroRotator, FVector(18.f, Naturals * KeyPitch + 2.f, 8.f), MatLacquer, false);
	for (const float S : { -1.f, 1.f })
	{
		P.Box(FVector(-8.f, S * (Naturals * KeyPitch * 0.5f + 5.5f), 71.f), FRotator::ZeroRotator, FVector(20.f, 11.f, 18.f), MatLacquer, false);
	}
	P.Box(FVector(0.8f, 0.f, 77.f), FRotator::ZeroRotator, FVector(1.6f, Naturals * KeyPitch, 8.f), MatLacquer, false);
	P.Box(FVector(0.3f, 0.f, 73.2f), FRotator::ZeroRotator, FVector(0.6f, Naturals * KeyPitch, 1.4f), MatShadow, false);
	// Counting naturals from the bottom A, which of them have a sharp above: A, C, D, F and G.
	static const bool HasSharp[7] = { true, false, true, true, false, true, true };
	for (int32 i = 0; i < Naturals; ++i)
	{
		const float Y = KeyLeft + KeyPitch * (i + 0.5f);
		if (HasSharp[i % 7] && i + 1 < Naturals && i != 25)
		{
			P.Box(FVector(-4.75f, Y + KeyPitch * 0.5f, 73.3f), FRotator::ZeroRotator, FVector(9.5f, 1.25f, 1.7f), MatEbony, false);
		}
		if (i == 17 || i == 18)
		{
			continue;
		}
		const bool bDown = i == 9 || i == 33 || i == 34;
		const bool bChipped = i == 5 || i == 22 || i == 40 || i == 47;
		P.Box(FVector(-7.5f, Y, 71.35f - (bDown ? 0.8f : 0.f)), FRotator(bDown ? -1.5f : 0.f, 0.f, 0.f), FVector(15.f, KeyPitch - 0.14f, 2.3f),
			bChipped ? MatIvoryDark.Get() : MatIvory.Get(), false);
	}

	// The music desk, up, on its ledge, leaning back. The sheet music on it is a clue (BuildClues).
	P.Box(FVector(PianoDeskX - 2.5f, 0.f, PianoCaseTop + 1.2f), FRotator::ZeroRotator, FVector(6.f, 82.f, 2.4f), MatLacquer, false);
	P.Box(PianoDeskCentre(), FRotator(-PianoDeskLean, 0.f, 0.f), FVector(1.5f, 78.f, 32.f), MatLacquer, false);

	// Legs: three, each a block under the case, a turned shaft and a brass castor, and a web from
	// each up into the angle under the case.
	for (const FVector2D& Leg : { FVector2D(24.f, -62.f), FVector2D(24.f, 62.f), FVector2D(172.f, -52.f) })
	{
		P.Box(FVector(Leg.X, Leg.Y, CaseBottom - 3.f), FRotator::ZeroRotator, FVector(15.f, 15.f, 6.f), MatLacquer, false);
		P.Cyl(FVector(Leg.X, Leg.Y, 31.f), FRotator::ZeroRotator, FVector(10.f, 10.f, 52.f), MatLacquer, false);
		P.Cyl(FVector(Leg.X, Leg.Y, 50.f), FRotator::ZeroRotator, FVector(12.5f, 12.5f, 3.f), MatLacquer, false);
		P.Cyl(FVector(Leg.X, Leg.Y, 5.5f), FRotator::ZeroRotator, FVector(7.f, 7.f, 2.f), MatBrass, false);
		P.Sph(FVector(Leg.X, Leg.Y, 2.5f), 5.f, MatBrass);
		P.Add(FRoomShapes::Plane(), FVector(Leg.X + 12.f, Leg.Y, 44.f), FRotator(0.f, 0.f, 90.f), FVector(24.f, 34.f, 1.f), MatWeb, false);
	}

	// The pedal lyre: two rods down from the case to a box, and three brass pedals out of it.
	for (const float S : { -1.f, 1.f })
	{
		P.Cyl(FVector(18.f, S * 9.f, 37.f), FRotator::ZeroRotator, FVector(2.4f, 2.4f, 50.f), MatLacquer, false);
	}
	P.Box(FVector(18.f, 0.f, 8.f), FRotator::ZeroRotator, FVector(10.f, 26.f, 8.f), MatLacquer, false);
	for (const float Y : { -6.f, 0.f, 6.f })
	{
		P.Box(FVector(9.f, Y, 6.f), FRotator(Y == 0.f ? -3.f : 0.f, 0.f, 0.f), FVector(11.f, 2.6f, 1.2f), MatBrass, false);
	}
	P.Add(FRoomShapes::Plane(), FVector(18.f, 0.f, 30.f), FRotator(0.f, 0.f, 90.f), FVector(16.f, 30.f, 1.f), MatWeb, false);

	// The stool, pushed back a little and askew, its top in the same green as the room.
	const FVector Stool(-48.f, 5.f, 0.f);
	const FRotator StoolTurn(0.f, 4.f, 0.f);
	P.Box(Stool + FVector(0.f, 0.f, 48.f), StoolTurn, FVector(36.f, 88.f, 8.f), MatLacquer);
	P.Box(Stool + FVector(0.f, 0.f, 53.5f), StoolTurn, FVector(33.f, 84.f, 3.f), MatFelt, false);
	for (const FVector2D& Leg : { FVector2D(-14.f, -38.f), FVector2D(14.f, -38.f), FVector2D(-14.f, 38.f), FVector2D(14.f, 38.f) })
	{
		P.Cyl(Stool + StoolTurn.RotateVector(FVector(Leg.X, Leg.Y, 0.f)) + FVector(0.f, 0.f, 22.f), FRotator::ZeroRotator, FVector(4.5f, 4.5f, 44.f), MatLacquer, false);
	}

	// What stops a man walking into it: the case as two boxes, since the bentside cuts a corner out
	// of the rectangle it would otherwise be.
	LivingPawnOnly(P.Box(FVector(48.f, 0.f, 47.f), FRotator::ZeroRotator, FVector(114.f, 152.f, 94.f), MatVoid));
	LivingPawnOnly(P.Box(FVector(150.f, -45.f, 47.f), FRotator::ZeroRotator, FVector(112.f, 62.f, 94.f), MatVoid));

	// A plan footprint for the debris, generous enough to cover the turned case and the stool.
	const FVector O = PianoOrigin();
	Footprints.Add(FBox2D(FVector2D(O.X - 110.f, O.Y - 90.f), FVector2D(O.X + 200.f, O.Y + 150.f)));
}

void ALivingRoomActor::BuildWallFurniture(FRoomBuilder& Build)
{
	const float F = FloorZ();
	const float H = HearthY();

	// The commode against the north wall: the family's photographs are on it (BuildClues), with a
	// porcelain horse at one end and a vase with nothing in it at the other.
	const FVector Commode = CommodeSeat();
	if (UStaticMeshComponent* Mesh = Build.PropSeated(RoomProps::Commode, Commode, FRotator::ZeroRotator, 0.f))
	{
		FRoomShapes::TintSlots(Mesh, FLinearColor(0.9f, 1.1f, 1.3f));
	}
	const float CommodeTop = F + CommodeHeight;
	if (UStaticMeshComponent* Horse = Build.PropSeated(RoomProps::PorcelainHorse, FVector(Commode.X - 46.f, Commode.Y - 4.f, CommodeTop), FRotator(0.f, 25.f, 0.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Horse, FLinearColor(0.50f, 0.48f, 0.45f));
	}
	if (UStaticMeshComponent* Vase = Build.PropSeated(RoomProps::Vase, FVector(Commode.X + 45.f, Commode.Y - 6.f, CommodeTop), FRotator(0.f, 70.f, 0.f), 34.f, false))
	{
		FRoomShapes::TintSlots(Vase, FLinearColor(0.44f, 0.42f, 0.40f));
	}
	Build.Stain(RoomSurfaces::Damp, FVector(Commode.X, Commode.Y, CommodeTop + 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(50.f, 116.f), FLinearColor(0.40f, 0.38f, 0.35f), 0.4f, 1.2f);
	Footprints.Add(FBox2D(FVector2D(Commode.X - 64.f, NorthY()), FVector2D(Commode.X + 64.f, NorthY() + 62.f)));

	// The oval portrait over it, in the middle of the moulded panel, hung two degrees out of true.
	// hanging_picture_frame_03 has its back at local Y = 0 and faces +Y, so it goes on the wall face.
	if (UStaticMeshComponent* Oval = Build.Prop(RoomProps::OvalFrame, WallPoint(EWall::North, Commode.X, F + 218.f, 0.2f), FRotator(-2.f, FacingYaw(EWall::North), 0.f), 72.f, false))
	{
		FRoomShapes::TintSlots(Oval, FLinearColor(0.50f, 0.42f, 0.34f), 0);
		FRoomShapes::TintSlots(Oval, FLinearColor(0.34f, 0.27f, 0.21f), 1);
		Oval->SetMaterial(2, MatGlass);
	}
	Build.Sph(WallPoint(EWall::North, Commode.X, F + 262.f, 1.f), 1.8f, MatIron);

	// A painting either side of the chimney breast, each gone brown under its varnish and each hung
	// crooked by its own amount. fancy_picture_frame_01 is 60 x 2 x 46, back at Y = 0, face +Y;
	// crooked is pitch, which turns a frame in its own plane.
	for (const float S : { -1.f, 1.f })
	{
		const FVector At = WallPoint(EWall::West, H + S * 285.f, F + 215.f, 0.2f);
		if (UStaticMeshComponent* Frame = Build.Prop(RoomProps::LandscapeFrame, At, FRotator(S < 0.f ? 2.5f : -1.8f, FacingYaw(EWall::West), 0.f), 82.f, false))
		{
			FRoomShapes::TintSlots(Frame, FLinearColor(0.45f, 0.38f, 0.28f), 0);
			FRoomShapes::TintSlots(Frame, FLinearColor(0.30f, 0.24f, 0.18f), 1);
		}
		Build.Sph(WallPoint(EWall::West, H + S * 285.f, F + 262.f, 1.f), 1.8f, MatIron);
	}

	// Under the covered portrait on the east wall (BuildClues), a tall side table and a brass vase.
	const FVector SideTable(EastX() - 32.f, CoveredPortraitY(), F);
	if (UStaticMeshComponent* Table = Build.PropSeated(RoomProps::SideTable, SideTable, FRotator(0.f, 90.f, 0.f), 0.f))
	{
		FRoomShapes::TintSlots(Table, FLinearColor(0.52f, 0.49f, 0.45f));
	}
	if (UStaticMeshComponent* Vase = Build.PropSeated(RoomProps::BrassVase, SideTable + FVector(0.f, 4.f, 76.f), FRotator::ZeroRotator, 44.f, false))
	{
		FRoomShapes::TintSlots(Vase, FLinearColor(0.40f, 0.34f, 0.26f));
	}
	Footprints.Add(FBox2D(FVector2D(SideTable.X - 30.f, SideTable.Y - 30.f), FVector2D(EastX(), SideTable.Y + 30.f)));

	// A bentwood coat stand inside the door, with a man's hat still on one of its hooks and a scarf
	// on another: whoever came in last took his things off here and did not go out again.
	const FVector Stand(Setup.DoorX + 105.f, NorthY() + 42.f, F);
	USceneComponent* StandRoot = LivingPivot(this, RoomRoot, Stand, FRotator(0.f, 20.f, 0.f), TEXT("CoatStand"));
	FRoomBuilder C(this, StandRoot);
	for (int32 i = 0; i < 3; ++i)
	{
		const FRotator Foot(0.f, i * 120.f, 0.f);
		C.Box(Foot.RotateVector(FVector(15.f, 0.f, 3.f)), Foot, FVector(30.f, 3.f, 3.f), MatOakDark, false);
		C.Sph(Foot.RotateVector(FVector(29.f, 0.f, 3.f)), 4.f, MatOakDark);
	}
	C.Cyl(FVector(0.f, 0.f, 92.f), FRotator::ZeroRotator, FVector(4.6f, 4.6f, 180.f), MatOakDark, false);
	C.Cyl(FVector(0.f, 0.f, 62.f), FRotator::ZeroRotator, FVector(9.f, 9.f, 3.f), MatOakDark, false);
	C.Sph(FVector(0.f, 0.f, 184.f), 7.f, MatOakDark);
	for (int32 i = 0; i < 6; ++i)
	{
		// Hooks up and out at thirty-five degrees: a cylinder stands along its own Z, so the arm's
		// direction is made the cylinder's axis.
		const FVector Out = FRotator(35.f, i * 60.f, 0.f).Vector();
		const FVector Base(0.f, 0.f, i % 2 ? 158.f : 172.f);
		C.Cyl(Base + Out * 7.f, FRotationMatrix::MakeFromZ(Out).Rotator(), FVector(2.f, 2.f, 14.f), MatOakDark, false);
		C.Sph(Base + Out * 14.f, 3.f, MatOakDark);
	}
	UMaterialInterface* HatFelt = C.Flat(FLinearColor(0.012f, 0.011f, 0.011f), 0.95f);
	const FVector HatAt = FVector(0.f, 0.f, 172.f) + FRotator(35.f, 0.f, 0.f).Vector() * 14.f + FVector(3.f, 0.f, -2.f);
	C.Cyl(HatAt, FRotator(-14.f, 0.f, 0.f), FVector(30.f, 27.f, 1.2f), HatFelt, false);
	C.Cyl(HatAt + FVector(-1.f, 0.f, 6.f), FRotator(-14.f, 0.f, 0.f), FVector(18.f, 16.f, 11.f), HatFelt, false);
	C.Cyl(HatAt + FVector(-0.6f, 0.f, 2.f), FRotator(-14.f, 0.f, 0.f), FVector(18.5f, 16.5f, 2.2f), MatShadow, false);
	UMaterialInterface* Wool = C.Surface(RoomSurfaces::Drapery, FLinearColor(0.20f, 0.05f, 0.04f));
	const FVector Scarf = FVector(0.f, 0.f, 158.f) + FRotator(35.f, 180.f, 0.f).Vector() * 14.f;
	for (const float S : { -1.f, 1.f })
	{
		C.Box(Scarf + FVector(S * 3.5f, 0.f, -36.f - S * 6.f), FRotator(0.f, 0.f, S * 3.f), FVector(1.f, 17.f, 72.f + S * 12.f), Wool, false);
	}
	LivingPawnOnly(C.Cyl(FVector(0.f, 0.f, 90.f), FRotator::ZeroRotator, FVector(34.f, 34.f, 180.f), MatVoid));
	Footprints.Add(FBox2D(FVector2D(Stand.X - 34.f, Stand.Y - 34.f), FVector2D(Stand.X + 34.f, Stand.Y + 34.f)));

	// The doorway, and the swing of the door, stay clear of debris.
	Footprints.Add(FBox2D(FVector2D(Setup.DoorX - Setup.DoorHalf - 10.f, NorthY()), FVector2D(Setup.DoorX + Setup.DoorHalf + 10.f, NorthY() + Setup.DoorHalf * 2.f + 20.f)));
}

void ALivingRoomActor::BuildChandelier(FRoomBuilder& Build)
{
	// Chandelier_03 again, smaller than the hall's: the same maker, the same dust. It hangs from the
	// rose over the carpet on a short chain, and the draught through the broken panes turns it.
	const FVector Hook(LoungeX(), HearthY(), CeilingZ() - 9.f);
	ChandelierPivot = LivingPivot(this, RoomRoot, Hook, FRotator::ZeroRotator, TEXT("ChandelierPivot"));
	FRoomBuilder Hang(this, ChandelierPivot);
	const float Chain = 50.f;
	for (int32 Link = 0; Link * 6.f < Chain; ++Link)
	{
		Hang.Box(FVector(0.f, 0.f, -3.f - Link * 6.f), FRotator(0.f, (Link % 2) * 90.f, 0.f), FVector(1.f, 3.2f, 7.f), MatIron, false);
	}
	if (UStaticMeshComponent* Body = Hang.Prop(RoomProps::Chandelier, FVector(0.f, 0.f, -Chain), FRotator(0.f, 40.f, 0.f), 120.f, false))
	{
		FRoomShapes::TintSlots(Body, FLinearColor(0.24f, 0.21f, 0.17f), 0);
		FRoomShapes::TintSlots(Body, FLinearColor(0.55f, 0.55f, 0.56f), 1);
	}
	FRandomStream Drops(4177);
	for (int32 i = 0; i < 5; ++i)
	{
		const float A = Drops.FRandRange(0.f, 2.f * PI);
		const float R = Drops.FRandRange(28.f, 42.f);
		const float Hangs = Drops.FRandRange(6.f, 22.f);
		const FVector Arm(FMath::Cos(A) * R, FMath::Sin(A) * R, -Chain - 80.f);
		Hang.Box(Arm - FVector(0.f, 0.f, Hangs * 0.5f), FRotator::ZeroRotator, FVector(0.25f, 0.25f, Hangs), MatIron, false);
		Hang.Add(FRoomShapes::Cone(), Arm - FVector(0.f, 0.f, Hangs + 3.f), FRotator(180.f, 0.f, 0.f), FVector(2.8f, 2.8f, 7.f), MatGlass, false);
	}
	Hang.Add(FRoomShapes::Plane(), FVector(0.f, 0.f, -Chain - 40.f), FRotator(0.f, 30.f, 90.f), FVector(50.f, 40.f, 1.f), MatWeb, false);
}

void ALivingRoomActor::BuildDamage(FRoomBuilder& Build)
{
	const float F = FloorZ();
	const float C = CeilingZ();
	const FLinearColor DampTint(0.46f, 0.38f, 0.30f);
	const FLinearColor SubstrateTint(0.30f, 0.26f, 0.22f);
	const FLinearColor MouldTint(0.20f, 0.19f, 0.13f);

	auto Stain = [&](EWall Wall, float U, float Z, float SU, float SZ, const FRoomSurface& Set, const FLinearColor& Tint, float Opacity, float Roll, float Edge)
	{
		FVector Location;
		FRotator Rotation;
		AimAt(Wall, U, Z, Roll, Location, Rotation);
		Build.Stain(Set, Location, Rotation, FVector2D(SU, SZ), Tint, Opacity, Edge);
	};

	for (const EWall Wall : { EWall::North, EWall::South, EWall::East, EWall::West })
	{
		const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
		const float U0 = bAlongX ? WestX() : NorthY();
		const float U1 = bAlongX ? EastX() : SouthY();

		for (int32 i = 0; i < 10; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float Z = Random.FRandRange(F + 40.f, C - 30.f);
			if (!IsOnOpening(Wall, U, Z, 40.f, 40.f))
			{
				Stain(Wall, U, Z, Random.FRandRange(110.f, 240.f), Random.FRandRange(90.f, 200.f), RoomSurfaces::Damp, DampTint * 0.85f, Random.FRandRange(0.14f, 0.28f), Random.FRandRange(0.f, 360.f), 1.15f);
			}
		}
		// Water down from the ceiling line, in long runs.
		for (int32 i = 0; i < 4; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float Drop = Random.FRandRange(100.f, 260.f);
			if (!IsOnOpening(Wall, U, C - Drop * 0.4f, 40.f, Drop * 0.4f))
			{
				Stain(Wall, U, C - Drop * 0.4f, Random.FRandRange(70.f, 150.f), Drop, RoomSurfaces::Damp, DampTint * 0.72f, Random.FRandRange(0.4f, 0.6f), 0.f, 1.25f);
			}
		}
		// Cracks, in clusters.
		for (int32 i = 0; i < 6; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float Z = Random.FRandRange(F + 130.f, C - 50.f);
			if (!IsOnOpening(Wall, U, Z, 55.f, 55.f))
			{
				FVector Location;
				FRotator Rotation;
				AimAt(Wall, U, Z, 0.f, Location, Rotation);
				Build.Crack(Location, Rotation, FVector2D(Random.FRandRange(90.f, 200.f), Random.FRandRange(110.f, 240.f)), Random.FRandRange(0.65f, 1.f), Random.FRandRange(14.f, 26.f));
			}
		}
		// Plaster off to the brick, a few big patches, high up over the picture rail.
		for (int32 i = 0; i < 3; ++i)
		{
			const float U = Random.FRandRange(U0 + 60.f, U1 - 60.f);
			const float Z = Random.FRandRange(F + LivingPictureRail - 40.f, C - 60.f);
			if (!IsOnOpening(Wall, U, Z, 60.f, 60.f))
			{
				Stain(Wall, U, Z, Random.FRandRange(60.f, 130.f), Random.FRandRange(60.f, 120.f), RoomSurfaces::Substrate, SubstrateTint, Random.FRandRange(0.85f, 1.f), Random.FRandRange(0.f, 360.f), 0.9f);
			}
		}
		// Mould in the corners, low and high.
		for (const float Corner : { U0, U1 })
		{
			const float U = Corner + (Corner == U0 ? 1.f : -1.f) * Random.FRandRange(10.f, 50.f);
			Stain(Wall, U, F + 40.f, Random.FRandRange(70.f, 140.f), Random.FRandRange(80.f, 150.f), RoomSurfaces::Damp, MouldTint, 0.72f, 0.f, 1.1f);
			Stain(Wall, U, C - 40.f, Random.FRandRange(80.f, 150.f), Random.FRandRange(70.f, 130.f), RoomSurfaces::Damp, MouldTint, 0.62f, 0.f, 1.1f);
		}
	}

	// Mould round every window, worst under the sills where the rain runs down the inside of the
	// wall, and blooming up both sides of the reveal.
	for (int32 i = 0; i < 3; ++i)
	{
		const float U = WindowX(i);
		Stain(EWall::South, U, F + 24.f, WindowWidth + 30.f, 46.f, RoomSurfaces::Damp, MouldTint, 0.8f, 0.f, 1.2f);
		for (const float S : { -1.f, 1.f })
		{
			Stain(EWall::South, U + S * (WindowWidth * 0.5f + 24.f), F + Random.FRandRange(120.f, 300.f), Random.FRandRange(34.f, 56.f), Random.FRandRange(120.f, 240.f),
				RoomSurfaces::Damp, MouldTint, Random.FRandRange(0.6f, 0.8f), 0.f, 1.15f);
		}
		// And on the parquet under each, standing water gone to a dark stain, wettest under the
		// middle window, where the blocks have lifted (BuildFloor).
		Build.Stain(RoomSurfaces::Damp, FVector(U, SouthY() - 70.f, F + 10.f), FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(150.f, 210.f), Random.FRandRange(80.f, 130.f)), FLinearColor(0.12f, 0.10f, 0.08f), i == 1 ? 0.85f : 0.6f, 1.2f, 0.25f);
	}

	// Dust along the foot of every wall, and the scratches the big sofa left in the parquet when it
	// was dragged round to face the fire.
	const FLinearColor DustTint(0.42f, 0.40f, 0.36f);
	for (int32 i = 0; i < 20; ++i)
	{
		FVector At;
		FRotator Rot(-90.f, 0.f, 0.f);
		switch (i % 4)
		{
		case 0: At = FVector(Random.FRandRange(WestX() + 30.f, EastX() - 30.f), NorthY() + Random.FRandRange(12.f, 34.f), F + 10.f); break;
		case 1: At = FVector(Random.FRandRange(WestX() + 30.f, EastX() - 30.f), SouthY() - Random.FRandRange(12.f, 34.f), F + 10.f); break;
		case 2: At = FVector(EastX() - Random.FRandRange(12.f, 34.f), Random.FRandRange(NorthY() + 30.f, SouthY() - 30.f), F + 10.f); Rot.Roll = 90.f; break;
		default: At = FVector(WestX() + Random.FRandRange(12.f, 34.f), Random.FRandRange(NorthY() + 30.f, SouthY() - 30.f), F + 10.f); Rot.Roll = 90.f; break;
		}
		Build.Stain(RoomSurfaces::Damp, At, Rot, FVector2D(Random.FRandRange(60.f, 150.f), Random.FRandRange(24.f, 44.f)), DustTint, Random.FRandRange(0.22f, 0.36f), 1.35f);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		const float Y = HearthY() - 90.f + i * 58.f + Random.FRandRange(-4.f, 4.f);
		Build.Crack(FVector(LoungeX() + 285.f, Y, F + 8.f), FRotator(-90.f, 0.f, 12.f + Random.FRandRange(-3.f, 3.f)),
			FVector2D(Random.FRandRange(10.f, 14.f), Random.FRandRange(80.f, 130.f)), 1.f, Random.FRandRange(44.f, 56.f));
	}
}

void ALivingRoomActor::BuildDebris(FRoomBuilder& Build)
{
	const float F = FloorZ();

	// Rubble along the foot of the walls, as everywhere in the house, kept out of the furniture.
	for (int32 i = 0; i < 90; ++i)
	{
		const float Along = Random.FRand();
		const float In = Random.FRandRange(6.f, 30.f);
		FVector2D Spot;
		switch (i % 4)
		{
		case 0: Spot = FVector2D(FMath::Lerp(WestX(), EastX(), Along), NorthY() + In); break;
		case 1: Spot = FVector2D(FMath::Lerp(WestX(), EastX(), Along), SouthY() - In); break;
		case 2: Spot = FVector2D(EastX() - In, FMath::Lerp(NorthY(), SouthY(), Along)); break;
		default: Spot = FVector2D(WestX() + In, FMath::Lerp(NorthY(), SouthY(), Along)); break;
		}
		const float Size = Random.FRandRange(1.5f, 7.f);
		if (!IsFloorSpotClear(Spot.X, Spot.Y, Size))
		{
			continue;
		}
		Build.Box(FVector(Spot.X, Spot.Y, F + Size * 0.35f), FRotator(Random.FRandRange(-30.f, 30.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-30.f, 30.f)),
			FVector(Size * Random.FRandRange(0.8f, 1.8f), Size * Random.FRandRange(0.8f, 1.5f), Size * 0.6f), MatRubble, false);
	}

	// Under the hole in the ceiling: the plaster that came down, in lumps and dust, and its lath.
	const FVector Heap(EastX() - 150.f, NorthY() + 330.f, F);
	for (int32 i = 0; i < 22; ++i)
	{
		const float R = Random.FRandRange(0.f, 56.f);
		const float A = Random.FRandRange(0.f, 2.f * PI);
		const float Size = FMath::Lerp(12.f, 2.f, R / 56.f) * Random.FRandRange(0.7f, 1.3f);
		Build.Box(Heap + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Size * 0.3f), FRotator(Random.FRandRange(-25.f, 25.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-25.f, 25.f)),
			FVector(Size * 1.4f, Size, Size * 0.5f), MatRubble, false);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		Build.Box(Heap + FVector(Random.FRandRange(-40.f, 40.f), Random.FRandRange(-40.f, 40.f), 2.f + i * 0.9f), FRotator(Random.FRandRange(-5.f, 5.f), Random.FRandRange(0.f, 180.f), 0.f),
			FVector(Random.FRandRange(50.f, 90.f), 3.f, 0.9f), MatOakDark, false);
	}
	Build.Stain(RoomSurfaces::Damp, Heap + FVector(0.f, 0.f, 10.f), FRotator(-90.f, 0.f, 20.f), FVector2D(170.f, 140.f), FLinearColor(0.44f, 0.42f, 0.38f), 0.5f, 1.3f);

	// The length of cornice that came off the east wall, on the parquet under the gap in two
	// pieces, and the plaster behind the gap where it tore away (see BuildWallFinish).
	const float GapU = CorniceGapU();
	Build.Box(FVector(EastX() - 34.f, GapU - 20.f, F + 8.f), FRotator(0.f, 84.f, 12.f), FVector(58.f, 14.f, 16.f), MatOak, false);
	Build.Box(FVector(EastX() - 52.f, GapU + 26.f, F + 7.f), FRotator(0.f, 64.f, -8.f), FVector(36.f, 14.f, 14.f), MatOak, false);
	{
		FVector Location;
		FRotator Rotation;
		AimAt(EWall::East, GapU, CeilingZ() - 18.f, 0.f, Location, Rotation);
		Build.Stain(RoomSurfaces::Substrate, Location, Rotation, FVector2D(110.f, 40.f), FLinearColor(0.30f, 0.26f, 0.22f), 1.f, 0.8f);
	}

	// Sheet music, fallen off the piano and scattered on the floor round the stool, and a key that
	// came off the keyboard.
	const FVector O = PianoOrigin();
	const FRotator PianoTurn(0.f, PianoYaw, 0.f);
	for (int32 i = 0; i < 5; ++i)
	{
		const FVector At = O + PianoTurn.RotateVector(FVector(Random.FRandRange(-110.f, -60.f), Random.FRandRange(-80.f, 70.f), 0.2f));
		Build.Mark(At, FRotator(Random.FRandRange(-2.f, 2.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-2.f, 2.f)), FVector2D(22.f, 30.f),
			Random.FRand() < 0.5f ? MatPaper.Get() : MatPaperDamp.Get());
	}
	Build.Box(O + PianoTurn.RotateVector(FVector(-76.f, 40.f, 1.2f)), FRotator(0.f, PianoYaw + 30.f, 0.f), FVector(15.f, 2.2f, 2.3f), MatIvory, false);

	// Letters and loose pages elsewhere, and dead leaves blown in under the windows.
	for (int32 i = 0; i < 10; ++i)
	{
		const FVector2D Spot(Random.FRandRange(WestX() + 60.f, EastX() - 60.f), Random.FRandRange(NorthY() + 60.f, SouthY() - 60.f));
		if (!IsFloorSpotClear(Spot.X, Spot.Y, 20.f))
		{
			continue;
		}
		Build.Mark(FVector(Spot.X, Spot.Y, F + 0.2f), FRotator(Random.FRandRange(-2.f, 2.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-2.f, 2.f)),
			FVector2D(Random.FRandRange(19.f, 23.f), Random.FRandRange(26.f, 31.f)), Random.FRand() < 0.5f ? MatPaper.Get() : MatPaperDamp.Get());
	}
	UMaterialInterface* Leaf = Build.Flat(FLinearColor(0.035f, 0.020f, 0.009f), 0.9f);
	for (int32 i = 0; i < 26; ++i)
	{
		const float X = WindowX(i % 3) + Random.FRandRange(-90.f, 90.f);
		const float Y = SouthY() - Random.FRandRange(20.f, 140.f);
		if (!IsFloorSpotClear(X, Y, 4.f))
		{
			continue;
		}
		Build.Box(FVector(X, Y, F + 0.5f), FRotator(Random.FRandRange(-8.f, 8.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-8.f, 8.f)),
			FVector(Random.FRandRange(4.f, 7.f), Random.FRandRange(2.5f, 4.f), 0.3f), Leaf, false);
	}
}

void ALivingRoomActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ElapsedTime += DeltaTime;

	const float Gust = LeadStorm ? LeadStorm->GetWindGust() : 0.f;

	// The chandelier: a short chain, so quicker than the hall's and smaller, pushed by the draught
	// through the broken panes. Two swings at periods that never line up.
	if (ChandelierPivot)
	{
		const float Amp = FMath::Lerp(0.15f, 0.9f, Gust);
		const float Pitch = Amp * FMath::Sin(ElapsedTime * 2.f * PI / 2.3f);
		const float Roll = Amp * 0.6f * FMath::Sin(ElapsedTime * 2.f * PI / 3.1f + 0.7f);
		const float Turn = 4.f * FMath::Sin(ElapsedTime * 2.f * PI / 19.f);
		ChandelierPivot->SetRelativeRotation(FRotator(Pitch, Turn, Roll));
	}

	if (DustMotes)
	{
		DustMotes->SetWindStrength(Gust);
	}
}

// ---------------------------------------------------------------------------------------------

void ALivingRoomActor::SpawnWindows()
{
	// Three followers of the bedroom's storm. Only the middle one brings the world outside and the
	// strike's shadows with it (see FStormWindowSetup::bOwnView): its sky, treeline and rain are
	// wide enough to fill the other two, and three of everything would stand three sets of trees
	// through each other and flash three shadow-casting lights at every strike.
	for (int32 i = 0; i < 3; ++i)
	{
		const FTransform Transform(FRotator(0.f, 90.f, 0.f),
			GetActorTransform().TransformPosition(FVector(WindowX(i), SouthY() + Setup.WallThickness * 0.5f, FloorZ())));
		AStormWindowActor* Window = GetWorld()->SpawnActorDeferred<AStormWindowActor>(AStormWindowActor::StaticClass(), Transform, this);
		if (!Window)
		{
			continue;
		}
		FStormWindowSetup WindowSetup;
		WindowSetup.OpeningWidth = WindowWidth;
		WindowSetup.SillHeight = WindowSill;
		WindowSetup.TopHeight = WindowTop;
		WindowSetup.WallThickness = Setup.WallThickness;
		WindowSetup.bOwnView = (i == 1);
		// Three tall windows are a great deal more sky than the bedroom's one, and this room is
		// meant to be mostly dark.
		WindowSetup.PortalScale = 0.42f;
		// Every window its own seed, or its curtains are torn and its panes broken where the next
		// one's are.
		WindowSetup.Seed = 19640927 + i * 101;
		Window->Configure(WindowSetup);
		Window->SetLead(LeadStorm);
		Window->FinishSpawning(Transform);
		Windows.Add(Window);
	}
}

AClueActor* ALivingRoomActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AClueActor* Clue = GetWorld()->SpawnActor<AClueActor>(AClueActor::StaticClass(), GetActorTransform().TransformPosition(LocalLocation), Rotation, SpawnParams);
	if (Clue)
	{
		Clue->Configure(FText::FromString(ShortName), FText::FromString(Description));
		Clues.Add(Clue);
	}
	return Clue;
}

void ALivingRoomActor::BuildClues()
{
	auto HitVolume = [&](FRoomBuilder& B, const FVector& At, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		if (UStaticMeshComponent* Hit = B.Box(At, Rotation, Size, MatVoid))
		{
			Hit->SetHiddenInGame(true);
			Hit->SetCastShadow(false);
			Hit->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	};
	const float F = FloorZ();
	const float H = HearthY();

	// The television. The finale is when it speaks; until then, the one thing it says is that
	// somebody has been keeping it clean.
	if (AClueActor* Set = SpawnClue(FVector(BreastX() + 9.f, H, F + 235.f), FRotator::ZeroRotator,
		TEXT("Examine the television"),
		TEXT("A flat screen, the newest thing in the house by forty years. It is switched off at the wall — and its glass is the one surface in this room with no dust on it.")))
	{
		FRoomBuilder B(Set, Set->GetRootScene());
		HitVolume(B, FVector::ZeroVector, FVector(4.f, 170.f, 98.f));
	}

	// The photographs on the commode: every one of them a happy day.
	const FVector Commode = CommodeSeat();
	if (AClueActor* Photos = SpawnClue(FVector(Commode.X, Commode.Y, F + CommodeHeight), FRotator::ZeroRotator,
		TEXT("Examine the photographs"),
		TEXT("A wedding. A baby asleep on a rug. A little girl on a man's shoulders at the seaside, both of them laughing. Every one of them is a happy day.")))
	{
		FRoomBuilder B(Photos, Photos->GetRootScene());
		// Both frames face their local +Y, which from the north wall is the room.
		if (UStaticMeshComponent* Frame = B.PropSeated(RoomProps::PhotoFrame, FVector(-14.f, 4.f, 0.f), FRotator(0.f, -10.f, 0.f), 26.f, false))
		{
			Frame->SetMaterial(0, MatGlass);
			FRoomShapes::TintSlots(Frame, FLinearColor(0.62f, 0.50f, 0.38f), 1);
			FRoomShapes::TintSlots(Frame, FLinearColor(0.35f, 0.32f, 0.30f), 2);
		}
		if (UStaticMeshComponent* Frame = B.PropSeated(RoomProps::PhotoFrameWhite, FVector(16.f, -2.f, 0.f), FRotator(0.f, 12.f, 0.f), 29.f, false))
		{
			FRoomShapes::TintSlots(Frame, FLinearColor(0.62f, 0.50f, 0.38f), 0);
			FRoomShapes::TintSlots(Frame, FLinearColor(0.42f, 0.40f, 0.37f), 1);
			Frame->SetMaterial(2, MatGlass);
		}
		// And a small one gone over on its face: roll 90 turns its face, +Y, to the commode's top.
		if (UStaticMeshComponent* Frame = B.PropSeated(RoomProps::PhotoFrame, FVector(-2.f, -8.f, 0.f), FRotator(0.f, 70.f, 90.f), 18.f, false))
		{
			Frame->SetMaterial(0, MatGlass);
			FRoomShapes::TintSlots(Frame, FLinearColor(0.35f, 0.32f, 0.30f), 2);
		}
		HitVolume(B, FVector(0.f, 0.f, 14.f), FVector(60.f, 30.f, 30.f));
	}

	// The covered portrait on the east wall.
	if (AClueActor* Portrait = SpawnClue(WallPoint(EWall::East, CoveredPortraitY(), F + 205.f, 0.f), FRotator::ZeroRotator,
		TEXT("Examine the portrait"),
		TEXT("A portrait, with a dust sheet thrown over it and tied off behind the frame. Everything else in this room was left to the dust. This was covered on purpose.")))
	{
		FRoomBuilder B(Portrait, Portrait->GetRootScene());
		// fancy_picture_frame_02: back at local Y = 0, face +Y; yaw 90 turns that to -X, off the
		// east wall into the room. What is under the sheet is never seen.
		if (UStaticMeshComponent* Frame = B.Prop(RoomProps::GiltFrame, FVector(-0.2f, 0.f, 0.f), FRotator(0.f, FacingYaw(EWall::East), 0.f), 118.f, false))
		{
			FRoomShapes::TintSlots(Frame, FLinearColor(0.45f, 0.38f, 0.28f), 0);
			FRoomShapes::TintSlots(Frame, FLinearColor(0.16f, 0.12f, 0.08f), 1);
		}
		// The sheet stands off the wall and falls back to it at its edges: pitch 90 puts the
		// cloth's own up (+Z) along -X, out of the wall, and its length (+X) up the wall.
		// Rumpled hard: the frame's moulding and the cord behind it hold a sheet off in folds, and at
		// a small rumple it hung as a flat grey board.
		B.Cloth(FVector(-13.f, 0.f, -14.f), FRotator(90.f, 0.f, 0.f), FVector2D(158.f, 128.f), 7.f, 12.f, 7303, MatDustSheet, 34.f);
		B.Sph(FVector(-0.8f, 0.f, 66.f), 1.8f, MatIron);
		HitVolume(B, FVector(-8.f, 0.f, -10.f), FVector(16.f, 126.f, 150.f));
	}

	// The sheet music on the piano's desk.
	{
		const FRotator Turn(0.f, PianoYaw, 0.f);
		const FVector Desk = PianoOrigin() + Turn.RotateVector(PianoDeskCentre());
		if (AClueActor* Music = SpawnClue(Desk, Turn,
			TEXT("Examine the sheet music"),
			TEXT("A child's piece, in big round notes. Over every one of them the fingering is pencilled in, in an adult's careful hand.")))
		{
			FRoomBuilder B(Music, Music->GetRootScene());
			// Open like a book on the desk: two pages, each turned a few degrees off the desk's face
			// so there is a crease down the middle, standing just in front of it.
			const FRotator Lean(-PianoDeskLean, 0.f, 0.f);
			const FVector Front = Lean.RotateVector(FVector(-1.f, 0.f, 0.f));
			for (const float S : { -1.f, 1.f })
			{
				B.Box(Front * 1.1f + Lean.RotateVector(FVector(0.f, S * 11.2f, 1.f)), Lean + FRotator(0.f, S * 4.f, 0.f), FVector(0.15f, 22.f, 29.f),
					S < 0.f ? MatPaper.Get() : MatPaperDamp.Get(), false);
			}
			HitVolume(B, Front * 2.f, FVector(4.f, 48.f, 32.f), Lean);
		}
	}

	// The mantel clock, face down on the hearth where it fell.
	if (AClueActor* Clock = SpawnClue(FVector(BreastX() + 36.f, H + 34.f, F + 4.f), FRotator::ZeroRotator,
		TEXT("Examine the clock"),
		TEXT("The mantel clock, face down on the hearth where it fell. Whatever time it stopped at, it is keeping to itself.")))
	{
		FRoomBuilder B(Clock, Clock->GetRootScene());
		// mantel_clock_01 faces its local +Y; roll 90 puts that face on the marble.
		if (UStaticMeshComponent* Mesh = B.PropSeated(RoomProps::MantelClock, FVector::ZeroVector, FRotator(0.f, 70.f, 90.f), 0.f))
		{
			FRoomShapes::TintSlots(Mesh, FLinearColor(0.55f, 0.50f, 0.45f), 0);
			Mesh->SetMaterial(1, MatGlass);
		}
		FRandomStream Shards(1104);
		for (int32 i = 0; i < 8; ++i)
		{
			B.Box(FVector(Shards.FRandRange(-26.f, 26.f), Shards.FRandRange(-26.f, 26.f), 0.3f), FRotator(0.f, Shards.FRandRange(0.f, 360.f), 0.f),
				FVector(Shards.FRandRange(1.5f, 5.f), Shards.FRandRange(1.f, 3.5f), 0.4f), MatGlass, false);
		}
		HitVolume(B, FVector(0.f, 0.f, 7.f), FVector(40.f, 40.f, 14.f));
	}

	// The keys, on the lamp table beside the lamp.
	const FVector LampTable = LampTableSeat();
	if (AClueActor* Keys = SpawnClue(LampTable + FVector(14.f, 14.f, LampTableHeight), FRotator(0.f, 30.f, 0.f),
		TEXT("Examine the keys"),
		TEXT("A ring of keys, dropped beside the lamp: the front door, something small and brass, and a car key, its rubber worn through where a thumb pressed it.")))
	{
		FRoomBuilder B(Keys, Keys->GetRootScene());
		for (int32 i = 0; i < 12; ++i)
		{
			const float A0 = 2.f * PI * i / 12.f;
			const float A1 = 2.f * PI * (i + 1) / 12.f;
			const FVector P0(FMath::Cos(A0) * 1.8f, FMath::Sin(A0) * 1.8f, 0.25f);
			const FVector P1(FMath::Cos(A1) * 1.8f, FMath::Sin(A1) * 1.8f, 0.25f);
			B.Cyl((P0 + P1) * 0.5f, FRotationMatrix::MakeFromZ((P1 - P0).GetSafeNormal()).Rotator(), FVector(0.3f, 0.3f, (P1 - P0).Size() + 0.1f), MatIron, false);
		}
		// The house key: a round bow and a long blade.
		B.Cyl(FVector(3.6f, 0.f, 0.2f), FRotator::ZeroRotator, FVector(2.2f, 2.2f, 0.3f), MatIron, false);
		B.Box(FVector(7.2f, 0.f, 0.2f), FRotator::ZeroRotator, FVector(5.2f, 0.7f, 0.3f), MatIron, false);
		// The small brass one.
		B.Cyl(FVector(-1.5f, 3.2f, 0.25f), FRotator::ZeroRotator, FVector(1.4f, 1.4f, 0.25f), MatBrass, false);
		B.Box(FVector(-2.2f, 5.3f, 0.25f), FRotator(0.f, 110.f, 0.f), FVector(3.f, 0.5f, 0.25f), MatBrass, false);
		// The car key: a black plastic head and a steel blade.
		B.Box(FVector(-3.4f, -2.6f, 0.6f), FRotator(0.f, -35.f, 0.f), FVector(3.4f, 2.2f, 1.1f), MatPlastic, false);
		B.Box(FVector(-6.4f, -4.7f, 0.35f), FRotator(0.f, -35.f, 0.f), FVector(4.f, 0.8f, 0.3f), MatIron, false);
		HitVolume(B, FVector(0.f, 0.f, 1.f), FVector(20.f, 18.f, 4.f));
	}

	// The newspapers, stacked on the floor at the end of the small sofa.
	if (AClueActor* Papers = SpawnClue(FVector(LoungeX() + 165.f, H + 160.f, F), FRotator(0.f, 8.f, 0.f),
		TEXT("Examine the newspapers"),
		TEXT("Weeks of newspapers, never unfolded. The top one has had a column cut out of its front page — neatly, with scissors.")))
	{
		FRoomBuilder B(Papers, Papers->GetRootScene());
		FRandomStream Stack(1955);
		const int32 Count = 14;
		for (int32 i = 0; i < Count; ++i)
		{
			B.Box(FVector(Stack.FRandRange(-1.5f, 1.5f), Stack.FRandRange(-1.5f, 1.5f), 0.45f + i * 0.9f), FRotator(0.f, Stack.FRandRange(-5.f, 5.f), 0.f),
				FVector(30.f, 40.f, 0.9f), i % 3 == 0 ? MatPaperDamp.Get() : MatNewsprint.Get(), false);
		}
		const float Top = Count * 0.9f;
		// Twine round the stack, both ways.
		B.Box(FVector(0.f, 0.f, Top * 0.5f), FRotator::ZeroRotator, FVector(30.6f, 0.4f, Top + 0.4f), MatOakDark, false);
		B.Box(FVector(0.f, 0.f, Top * 0.5f), FRotator::ZeroRotator, FVector(0.4f, 40.6f, Top + 0.4f), MatOakDark, false);
		// The hole in the front page shows the page under it, darker.
		B.Mark(FVector(7.f, -6.f, Top - 0.55f), FRotator::ZeroRotator, FVector2D(6.f, 14.f), MatPaperDamp);
		HitVolume(B, FVector(0.f, 0.f, Top * 0.5f), FVector(34.f, 44.f, Top + 2.f));
	}
}
