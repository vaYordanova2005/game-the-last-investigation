#include "GrandStaircaseActor.h"
#include "RoomBuildLibrary.h"
#include "HallDoorActor.h"
#include "ClueActor.h"
#include "StormWindowActor.h"
#include "DustMotesComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

namespace
{
	/** Height of a handrail above the floor or the pitch line of a flight. */
	constexpr float RailHeight = 92.f;
	/** Height of the invisible walls behind every banister. Over a man's hip, under his eyes. */
	constexpr float BlockerHeight = 118.f;
	/** The picture rail and the dado on the gallery walls, above the gallery floor. */
	constexpr float GalleryPictureRail = 262.f;

	/** Collision that only a walking man meets: the interaction trace and the camera pass through. */
	void PawnOnly(UStaticMeshComponent* Part)
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
	 * The rectangle [U0,U1] x [Z0,Z1] with every opening on the wall cut out of it.
	 *
	 * Not the corridor's version, which walks the openings left to right and so assumes no two of
	 * them share any stretch of wall. The hall's east wall has the archway from the corridor
	 * directly over the front door, and that version filled in the wall "above the front door" all
	 * the way up through the archway: plaster across the way in, and a solid slab behind it. This
	 * one cuts the wall at every opening's edges, and in each strip removes every opening that
	 * covers it, one over another if need be.
	 */
	template <typename TOpening, typename TWall>
	TArray<FBox2D> CutAroundAll(const TArray<TOpening>& Openings, TWall Wall, float U0, float U1, float Z0, float Z1)
	{
		TArray<const TOpening*> Hits;
		TArray<float> Cuts = { U0, U1 };
		for (const TOpening& Opening : Openings)
		{
			if (Opening.Wall == Wall
				&& Opening.CenterU + Opening.HalfU > U0 && Opening.CenterU - Opening.HalfU < U1
				&& Opening.TopZ > Z0 && Opening.BottomZ < Z1)
			{
				Hits.Add(&Opening);
				Cuts.Add(FMath::Clamp(Opening.CenterU - Opening.HalfU, U0, U1));
				Cuts.Add(FMath::Clamp(Opening.CenterU + Opening.HalfU, U0, U1));
			}
		}
		Cuts.Sort();

		TArray<FBox2D> Pieces;
		for (int32 i = 0; i + 1 < Cuts.Num(); ++i)
		{
			const float A = Cuts[i];
			const float B = Cuts[i + 1];
			if (B - A < 0.01f)
			{
				continue;
			}
			const float Mid = (A + B) * 0.5f;

			// The holes over this strip, bottom to top, and the wall left between them.
			TArray<FVector2D> Holes;
			for (const TOpening* Opening : Hits)
			{
				if (FMath::Abs(Mid - Opening->CenterU) < Opening->HalfU)
				{
					Holes.Add(FVector2D(FMath::Max(Z0, Opening->BottomZ), FMath::Min(Z1, Opening->TopZ)));
				}
			}
			Holes.Sort([](const FVector2D& L, const FVector2D& R) { return L.X < R.X; });
			float Cursor = Z0;
			for (const FVector2D& Hole : Holes)
			{
				if (Hole.X > Cursor)
				{
					Pieces.Add(FBox2D(FVector2D(A, Cursor), FVector2D(B, Hole.X)));
				}
				Cursor = FMath::Max(Cursor, Hole.Y);
			}
			if (Cursor < Z1)
			{
				Pieces.Add(FBox2D(FVector2D(A, Cursor), FVector2D(B, Z1)));
			}
		}
		return Pieces;
	}

	/**
	 * A flat rectangle with its own UVs, one winding, for a two-sided material: the carrier for
	 * the stained glass and its lead. A basic-shape Plane would do the same job if anybody knew
	 * which corner of it UV (0,0) is at after a rotation, and the whole design is laid out against
	 * the mullions to the centimetre — so the corners are written down here instead.
	 *
	 * The sheet lies in the YZ plane at local X = 0, facing +X; U runs from +Y to -Y (left to right
	 * for somebody looking at it along -X), V from the bottom up.
	 */
	UProceduralMeshComponent* Sheet(AActor* Owner, USceneComponent* Parent, const FVector& Centre, float Width, float Height, UMaterialInterface* Mat)
	{
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("Sheet")));
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		Mesh->SetRelativeLocation(Centre);
		Mesh->bUseAsyncCooking = false;

		const float HW = Width * 0.5f;
		const float HH = Height * 0.5f;
		const FVector Corners[4] = { FVector(0.f, HW, -HH), FVector(0.f, -HW, -HH), FVector(0.f, -HW, HH), FVector(0.f, HW, HH) };
		const FVector2D CornerUV[4] = { FVector2D(0.f, 1.f), FVector2D(1.f, 1.f), FVector2D(1.f, 0.f), FVector2D(0.f, 0.f) };
		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		for (int32 i = 0; i < 4; ++i)
		{
			Verts.Add(Corners[i]);
			Normals.Add(FVector(1.f, 0.f, 0.f));
			UVs.Add(CornerUV[i]);
			Tangents.Add(FProcMeshTangent(FVector(0.f, -1.f, 0.f), false));
		}
		// ONE sheet, ONE winding, and a two-sided material to show it from both sides. Not the Pane
		// rule (both windings, normals forced), which is right for an opaque single-sided surface
		// and wrong here: a two-sided lit material flips the normal of whichever of a coplanar pair
		// faces away from the eye, the pair then z-fight, and the lead — whose far side is lit by
		// the sky outside — came out as pale blue blobs over the whole window, and printed the same
		// blobs onto the landing as its shadow.
		const TArray<int32> Tris = { 0, 2, 1, 0, 3, 2 };
		Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, TArray<FLinearColor>(), Tangents, /*bCreateCollision*/ false);
		Mesh->SetMaterial(0, Mat);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->RegisterComponent();
		Owner->AddInstanceComponent(Mesh);
		return Mesh;
	}
}

namespace
{
	/**
	 * A flat panel of any convex outline, standing in a vertical plane: Origin is the outline's
	 * (0, 0), Along is its first axis, world Z its second, Face which way its front looks. UVs are
	 * centimetres over TexelCm, so a photographed surface on it runs on without a seam however the
	 * outline is cut. Two sheets a few millimetres apart, each with both windings and its normal
	 * forced — the Pane rule, which is right for an opaque single-sided surface like this one.
	 */
	UProceduralMeshComponent* PanelMesh(AActor* Owner, USceneComponent* Parent, const TArray<FVector2D>& Outline, const FVector& Origin,
		const FVector& Along, const FVector& Face, float TexelCm, UMaterialInterface* Mat)
	{
		if (Outline.Num() < 3 || !Mat)
		{
			return nullptr;
		}
		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Tris;
		for (int32 Sheet = 0; Sheet < 2; ++Sheet)
		{
			const int32 First = Verts.Num();
			const FVector Normal = Sheet == 0 ? Face : -Face;
			for (const FVector2D& P : Outline)
			{
				Verts.Add(Origin + Along * P.X + FVector(0.f, 0.f, P.Y) - Face * (Sheet * 0.4f));
				Normals.Add(Normal);
				UVs.Add(FVector2D(P.X / TexelCm, -P.Y / TexelCm));
				Tangents.Add(FProcMeshTangent(Along, false));
			}
			for (int32 i = 1; i + 1 < Outline.Num(); ++i)
			{
				Tris.Append({ First, First + i, First + i + 1, First, First + i + 1, First + i });
			}
		}
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("Panel")));
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
}

AGrandStaircaseActor::AGrandStaircaseActor()
{
	PrimaryActorTick.bCanEverTick = true;

	HallRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HallRoot"));
	SetRootComponent(HallRoot);
	HallRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void AGrandStaircaseActor::Configure(const FStairHallSetup& InSetup, AStormWindowActor* InLeadStorm)
{
	Setup = InSetup;
	LeadStorm = InLeadStorm;
	if (InLeadStorm)
	{
		// The glass reads the flash every frame; it has to be this frame's flash.
		AddTickPrerequisiteActor(InLeadStorm);
	}

	// Here and not in BeginPlay: the motes are scattered in the component's own BeginPlay, which
	// runs before the owner's (see ARoomDressingActor::Configure).
	DustMotes->ConfigureVolume(
		FVector(HallLength * 0.48f, HallWidth * 0.48f, (CeilingZ - GroundZ) * 0.48f),
		FVector(EastX() - HallLength * 0.5f, Setup.CenterY, (CeilingZ + GroundZ) * 0.5f));

	Openings = {
		// Onto the gallery from the corridor.
		{ EWall::East, Setup.CenterY, Setup.OpeningWidth * 0.5f, 0.f, Setup.OpeningHeight },
		// The front door, below it.
		{ EWall::East, Setup.CenterY, 60.f, GroundZ, GroundZ + 250.f },
		// The dining room and the parlour, either side of the hall.
		{ EWall::North, -1560.f, 50.f, GroundZ, GroundZ + 212.f },
		{ EWall::South, -1560.f, 50.f, GroundZ, GroundZ + 212.f },
		// The window over the landing.
		{ EWall::West, Setup.CenterY, WindowWidth * 0.5f, LandingZ + WindowSill, LandingZ + WindowSill + WindowHeight },
	};
}

void AGrandStaircaseActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning the hall never relays the corridor or the bedroom.
	Random.Initialize(18840212);

	FRoomBuilder Build(this, HallRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildFloors(Build);
	BuildFlights(Build);
	BuildBalustrades(Build);
	BuildWallFinish(Build);
	BuildCeiling(Build);
	BuildStainedGlass(Build);
	BuildChandelier(Build);
	BuildDamage(Build);
	BuildFurniture(Build);
	BuildDebris(Build);
	BuildFigure(Build);

	SpawnDoors();
	SpawnWindow();
	BuildClues();
}

void AGrandStaircaseActor::CacheMaterials(FRoomBuilder& Build)
{
	// The corridor's values for everything the two share, so walking through the archway does not
	// change the house; the new surfaces are held to the same rule — a tenth to a fifth reflectance
	// and warm, because the only fill in here is the cold light through the window.
	MatPlaster = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f));
	MatWallpaper = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.160f, 0.132f, 0.108f));
	MatWallpaperDark = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.120f, 0.098f, 0.080f));
	// dark_paneled_wood is photographed nearly black-red (linear 0.024, 0.008, 0.006); lifted and
	// pulled towards brown, so the panelling is dark oak rather than a mahogany that glows red in
	// the lantern.
	MatWainscot = Build.Surface(RoomSurfaces::Wainscot, FLinearColor(2.1f, 3.3f, 3.5f));
	// The same panelling for the generated spandrels, which carry their own UVs in repeats: an
	// instance of its own with the tiling at one (the curtains' rule). A hair off the tint, so the
	// builder's cache hands back a separate instance rather than the one Add() tiles from.
	MatWainscotSheet = Build.Surface(RoomSurfaces::Wainscot, FLinearColor(2.1f, 3.3f, 3.51f));
	if (MatWainscotSheet)
	{
		MatWainscotSheet->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
		MatWainscotSheet->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	// The staircase is oak and darker than the corridor's joinery: it is the one piece of carpentry
	// in the house that was waxed every week for sixty years, and then not for sixty more.
	MatOak = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.290f, 0.272f, 0.250f));
	MatOakDark = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.190f, 0.180f, 0.170f));
	// Treads are worn pale down the middle where the runner has gone and dark where it has not.
	MatTread = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.52f, 0.47f, 0.42f), 0.8f);
	MatTreadWorn = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.36f, 0.32f, 0.28f));
	MatFloorboards = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.62f, 0.59f, 0.55f));
	MatFloorboardsWorn = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.40f, 0.36f, 0.32f));
	// The chequer's pale squares are about 0.2 as shot; held to roughly the plaster's value.
	MatTiles = Build.Surface(RoomSurfaces::HallTiles, FLinearColor(0.70f, 0.68f, 0.66f));
	MatMarble = Build.Surface(RoomSurfaces::Marble, FLinearColor(0.40f, 0.40f, 0.42f), 0.7f);
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.30f, 0.29f, 0.27f));
	MatBeam = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.220f, 0.210f, 0.200f));
	MatCarpet = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.290f, 0.066f, 0.034f));
	MatRubble = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.145f, 0.127f, 0.110f));
	MatPaper = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.464f, 0.245f, 0.108f));
	MatPaperDamp = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.260f, 0.142f, 0.065f));
	MatCloth = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.183f, 0.101f, 0.047f));
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatBrass = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.28f, 0.06f);
	// Cut crystal is clearer and far glossier than window glass; what makes a drop read as crystal
	// in the dark is the one hard highlight it throws back at the lantern.
	MatCrystal = Build.Glass(FLinearColor(0.22f, 0.23f, 0.24f), 0.34f, 0.02f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);
	MatBackRoom = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.035f, 0.032f, 0.029f));
	MatWax = Build.Flat(FLinearColor(0.34f, 0.30f, 0.22f), 0.55f);
	MatStem = Build.Flat(FLinearColor(0.045f, 0.036f, 0.020f), 0.95f);
	MatPetal = Build.Flat(FLinearColor(0.090f, 0.030f, 0.026f), 0.9f);
	MatShadow = Build.Flat(FLinearColor(0.002f, 0.002f, 0.002f), 1.f);
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers. U runs along a wall (X on north/south, Y on east/west); Z is world height.
// ---------------------------------------------------------------------------------------------

float AGrandStaircaseActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector AGrandStaircaseActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector AGrandStaircaseActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

bool AGrandStaircaseActor::IsOnOpening(EWall Wall, float U, float Z, float HalfU, float HalfZ) const
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

void AGrandStaircaseActor::WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud)
{
	// A Mark stands proud along its own +Z; the rotation per wall is the corridor's (FacePanel),
	// which was worked out the hard way.
	for (const FBox2D& Piece : CutAroundAll(Openings, Wall, U0, U1, Z0, Z1))
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
		// On the east and west walls local X runs *along* the wall and local Z out of it. The
		// corridor's rotation for these (pitch +-90) stands local X upright instead, and the tiling —
		// which gives its larger repeat count to the part's longer side and lays it along local X —
		// then squeezed ten metres of panelling into the height of the wainscot: the panels came
		// out as fine horizontal stripes. Plaster hides it; nothing with a grid in it does.
		case EWall::East:  Build.Mark(At, FRotator(0.f, 90.f, 90.f), FVector2D(S.X, S.Y), Mat); break;
		default:           Build.Mark(At, FRotator(0.f, 90.f, -90.f), FVector2D(S.X, S.Y), Mat); break;
		}
	}
}

void AGrandStaircaseActor::WallBox(FRoomBuilder& Build, EWall Wall, float U, float Z, float SizeU, float SizeZ, float Depth, float ProudBase, UMaterialInterface* Mat)
{
	const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
	Build.Box(WallPoint(Wall, U, Z, ProudBase + Depth * 0.5f), FRotator::ZeroRotator,
		bAlongX ? FVector(SizeU, Depth, SizeZ) : FVector(Depth, SizeU, SizeZ), Mat, false);
}

void AGrandStaircaseActor::AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const
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

// ---------------------------------------------------------------------------------------------

void AGrandStaircaseActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float Z0 = GroundZ - 20.f;
	const float Z1 = CeilingZ + 10.f;

	// Solid, colliding and never seen, like the corridor's: every face the player looks at is a
	// finish laid over this. The east wall shares its upper storey with the corridor's end wall,
	// and is simply built again through it; two black boxes in one place draw nothing new.
	auto Run = [&](EWall Wall, float U0, float U1, float Line)
	{
		for (const FBox2D& Piece : CutAroundAll(Openings, Wall, U0, U1, Z0, Z1))
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
	Run(EWall::North, WestX() - T, EastX() + T, NorthY() - T * 0.5f);
	Run(EWall::South, WestX() - T, EastX() + T, SouthY() + T * 0.5f);
	Run(EWall::East, NorthY() - T, SouthY() + T, EastX() + T * 0.5f);
	Run(EWall::West, NorthY() - T, SouthY() + T, WestX() - T * 0.5f);

	const FVector Middle(EastX() - HallLength * 0.5f, Setup.CenterY, 0.f);
	const FVector Span(HallLength + T * 2.f, HallWidth + T * 2.f, 20.f);
	Build.Box(FVector(Middle.X, Middle.Y, GroundZ - 12.f), FRotator::ZeroRotator, Span, MatShell);
	Build.Box(FVector(Middle.X, Middle.Y, CeilingZ + 10.f), FRotator::ZeroRotator, Span, MatShell);

	// Behind the three ground-floor doors, rooms the hall never lets you into, as in the corridor:
	// a floor that goes on and a wall a long way off, both nearly black.
	auto BackRoom = [&](const FVector& Front, const FVector& Into, float HalfWidth, float Depth, float Height)
	{
		const FVector Across(-Into.Y, Into.X, 0.f);
		const FVector Mid = Front + Into * (Depth * 0.5f);
		const FVector Size = FMath::Abs(Into.X) > 0.5f ? FVector(Depth, HalfWidth * 2.f, 10.f) : FVector(HalfWidth * 2.f, Depth, 10.f);
		const FVector Wall = FMath::Abs(Into.X) > 0.5f ? FVector(10.f, HalfWidth * 2.f, Height) : FVector(HalfWidth * 2.f, 10.f, Height);
		const FVector Side = FMath::Abs(Into.X) > 0.5f ? FVector(Depth, 10.f, Height) : FVector(10.f, Depth, Height);
		Build.Box(FVector(Mid.X, Mid.Y, GroundZ - 5.f), FRotator::ZeroRotator, Size, MatFloorboardsWorn);
		Build.Box(FVector(Mid.X, Mid.Y, GroundZ + Height + 5.f), FRotator::ZeroRotator, Size, MatBackRoom);
		const FVector Back = Front + Into * (Depth + 5.f);
		Build.Box(FVector(Back.X, Back.Y, GroundZ + Height * 0.5f), FRotator::ZeroRotator, Wall, MatBackRoom);
		for (const float S : { -1.f, 1.f })
		{
			const FVector At = Mid + Across * S * (HalfWidth + 5.f);
			Build.Box(FVector(At.X, At.Y, GroundZ + Height * 0.5f), FRotator::ZeroRotator, Side, MatBackRoom);
		}
	};
	BackRoom(FVector(-1560.f, NorthY() - T, 0.f), FVector(0.f, -1.f, 0.f), 170.f, 300.f, 300.f);
	BackRoom(FVector(-1560.f, SouthY() + T, 0.f), FVector(0.f, 1.f, 0.f), 170.f, 300.f, 300.f);
	// The vestibule behind the front door is only ever seen through the gaps in the boards.
	BackRoom(FVector(EastX() + T, Setup.CenterY, 0.f), FVector(1.f, 0.f, 0.f), 110.f, 160.f, 270.f);

	// Thresholds through the wall in every doorway, and a deeper stone one at the front door.
	Build.Box(FVector(-1560.f, NorthY() - T * 0.5f, GroundZ + 1.f), FRotator::ZeroRotator, FVector(100.f, T + 4.f, 4.f), MatOakDark);
	Build.Box(FVector(-1560.f, SouthY() + T * 0.5f, GroundZ + 1.f), FRotator::ZeroRotator, FVector(100.f, T + 4.f, 4.f), MatOakDark);
	Build.Box(FVector(EastX() + T * 0.5f, Setup.CenterY, GroundZ + 1.5f), FRotator::ZeroRotator, FVector(T + 30.f, 130.f, 5.f), MatMarble);
	// And through the archway from the corridor, where the two floors meet.
	Build.Box(FVector(EastX() + T * 0.5f, Setup.CenterY, 0.f), FRotator::ZeroRotator, FVector(T + 4.f, Setup.OpeningWidth, 6.f), MatOakDark);
}

void AGrandStaircaseActor::BuildFloors(FRoomBuilder& Build)
{
	// The entrance hall: one chequered floor, wall to wall. It collides; everything laid on it
	// does not.
	Build.Box(FVector(EastX() - HallLength * 0.5f, Setup.CenterY, GroundZ - 1.f), FRotator::ZeroRotator,
		FVector(HallLength, HallWidth, 2.f), MatTiles);

	// The upper floors — the gallery on three sides and the half-landing — are structure under
	// boards, like the corridor: a slab that carries the player, boards over it that are what the
	// eye sees, and the plaster soffit under it that is what the hall below sees.
	struct FDeck { float X0, X1, Y0, Y1, Z; bool bAlongY; };
	const FDeck Decks[] = {
		{ GalleryEdgeX(), EastX(), NorthY(), SouthY(), 0.f, true },
		{ FlightEastX(), GalleryEdgeX(), NorthY(), NorthInnerY(), 0.f, false },
		{ FlightEastX(), GalleryEdgeX(), SouthInnerY(), SouthY(), 0.f, false },
		{ WestX(), LandingEdgeX(), NorthY(), SouthY(), LandingZ, true },
	};
	const float BoardWidth = 21.f;
	int32 DeckIndex = 0;
	for (const FDeck& D : Decks)
	{
		Build.Box(FVector((D.X0 + D.X1) * 0.5f, (D.Y0 + D.Y1) * 0.5f, D.Z - 3.f - (FloorDepth - 3.f) * 0.5f), FRotator::ZeroRotator,
			FVector(D.X1 - D.X0, D.Y1 - D.Y0, FloorDepth - 3.f), MatShell);
		Build.Mark(FVector((D.X0 + D.X1) * 0.5f, (D.Y0 + D.Y1) * 0.5f, D.Z - FloorDepth - 0.2f), FRotator(0.f, 0.f, 180.f),
			FVector2D(D.X1 - D.X0, D.Y1 - D.Y0), MatCeiling);

		// Boards laid the long way, each row from random lengths so no two joints line up. One in
		// thirty is gone, and a few have lifted at one end: the brief's loose boards, which a man
		// walking the gallery in the dark would feel before he saw.
		const float Across0 = D.bAlongY ? D.X0 : D.Y0;
		const float Across1 = D.bAlongY ? D.X1 : D.Y1;
		const float Along0 = D.bAlongY ? D.Y0 : D.X0;
		const float Along1 = D.bAlongY ? D.Y1 : D.X1;
		const int32 Rows = FMath::Max(1, FMath::RoundToInt((Across1 - Across0) / BoardWidth));
		const float RowWidth = (Across1 - Across0) / Rows;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float Across = Across0 + RowWidth * (Row + 0.5f);
			float Cursor = Along0;
			while (Cursor < Along1 - 1.f)
			{
				const float Length = FMath::Min(Random.FRandRange(140.f, 380.f), Along1 - Cursor);
				const float Mid = Cursor + Length * 0.5f;
				Cursor += Length;
				if (Length < 16.f || Random.FRand() < 0.03f)
				{
					continue;
				}
				// A lifted board is tilted about its own short axis, so one end stands proud.
				const bool bLifted = Random.FRand() < 0.05f;
				const float Tilt = bLifted ? Random.FRandRange(1.2f, 2.6f) : Random.FRandRange(-0.4f, 0.4f);
				const FVector At = D.bAlongY ? FVector(Across, Mid, D.Z) : FVector(Mid, Across, D.Z);
				const FVector Size = D.bAlongY ? FVector(RowWidth - 1.5f, Length - 1.5f, 6.f) : FVector(Length - 1.5f, RowWidth - 1.5f, 6.f);
				const FRotator Rot = D.bAlongY ? FRotator(0.f, 0.f, Tilt) : FRotator(Tilt, 0.f, 0.f);
				Build.Box(At + FVector(0.f, 0.f, Random.FRandRange(-0.8f, 0.8f) + (bLifted ? 1.5f : 0.f)), Rot, Size,
					Random.FRand() < 0.3f ? MatFloorboardsWorn.Get() : MatFloorboards.Get(), /*bBlockingCollision*/ false);
			}
		}

		// The edge the void is on gets a moulded fascia: the one line that makes a gallery floor
		// read as a floor rather than as a shelf.
		auto Fascia = [&](const FVector& From, const FVector& To, const FVector& Out)
		{
			const FVector Mid = (From + To) * 0.5f + Out * 2.f;
			const float Length = FVector::Dist(From, To);
			const bool bAlongX = FMath::Abs(To.X - From.X) > 1.f;
			Build.Box(FVector(Mid.X, Mid.Y, D.Z - FloorDepth * 0.5f), FRotator::ZeroRotator,
				bAlongX ? FVector(Length, 4.f, FloorDepth) : FVector(4.f, Length, FloorDepth), MatOak, false);
			const FVector Lip = (From + To) * 0.5f + Out * 4.5f;
			Build.Box(FVector(Lip.X, Lip.Y, D.Z - 3.f), FRotator::ZeroRotator,
				bAlongX ? FVector(Length + 6.f, 5.f, 6.f) : FVector(5.f, Length + 6.f, 6.f), MatOak, false);
			Build.Box(FVector(Lip.X, Lip.Y, D.Z - FloorDepth + 2.f), FRotator::ZeroRotator,
				bAlongX ? FVector(Length + 6.f, 5.f, 4.f) : FVector(5.f, Length + 6.f, 4.f), MatOak, false);
		};
		switch (DeckIndex)
		{
		case 0: Fascia(FVector(GalleryEdgeX(), NorthInnerY(), 0.f), FVector(GalleryEdgeX(), SouthInnerY(), 0.f), FVector(-1.f, 0.f, 0.f)); break;
		case 1: Fascia(FVector(FlightEastX(), NorthInnerY(), 0.f), FVector(GalleryEdgeX(), NorthInnerY(), 0.f), FVector(0.f, 1.f, 0.f)); break;
		case 2: Fascia(FVector(FlightEastX(), SouthInnerY(), 0.f), FVector(GalleryEdgeX(), SouthInnerY(), 0.f), FVector(0.f, -1.f, 0.f)); break;
		default:
			Fascia(FVector(LandingEdgeX(), NorthInnerY(), LandingZ), FVector(LandingEdgeX(), CentralNorthY(), LandingZ), FVector(1.f, 0.f, 0.f));
			Fascia(FVector(LandingEdgeX(), CentralSouthY(), LandingZ), FVector(LandingEdgeX(), SouthInnerY(), LandingZ), FVector(1.f, 0.f, 0.f));
			break;
		}
		++DeckIndex;
	}

	// Under the half-landing: panelled, floor to soffit, across the whole width of the hall. What
	// is behind it is the dark under the stairs, and nothing is.
	Build.Box(FVector(LandingEdgeX() + 2.f, Setup.CenterY, (GroundZ + LandingZ - FloorDepth) * 0.5f), FRotator::ZeroRotator,
		FVector(4.f, HallWidth, LandingZ - FloorDepth - GroundZ), MatWainscot);

	// A runner from the front door to the foot of the stairs, rotted into lengths like the one
	// upstairs, and a worn path down its middle.
	const FVector2D Pieces[] = { { EastX() - 40.f, EastX() - 190.f }, { EastX() - 214.f, EastX() - 420.f }, { EastX() - 446.f, FlightEastX() + 60.f } };
	int32 Seed = 1711;
	for (const FVector2D& Piece : Pieces)
	{
		const float Length = Piece.X - Piece.Y;
		Build.Cloth(FVector((Piece.X + Piece.Y) * 0.5f, Setup.CenterY + Random.FRandRange(-5.f, 5.f), GroundZ + 1.2f),
			FRotator(0.f, Random.FRandRange(-2.f, 2.f), 0.f), FVector2D(Length, 110.f), 0.8f, 0.f, Seed++, MatCarpet, 30.f);
	}
}

void AGrandStaircaseActor::BuildFlight(FRoomBuilder& Build, const FVector& FootNosing, const FVector& Up, float Width, int32 Seed, bool bRunner)
{
	// Everything in the flight's own terms: A runs up the flight from the face of the bottom riser,
	// S across it, Z is world up. The treads are visual only; what is walked on is a ramp through
	// the nosings, which is how every game with good stairs does it — a capsule stepping up
	// seventeen centimetres twenty times a flight shakes the camera like a pneumatic drill.
	FRandomStream Wear(Seed);
	const FVector Side(-Up.Y, Up.X, 0.f);
	const FRotator Yaw = Up.Rotation();
	auto P = [&](float A, float S, float Z) { return FootNosing + Up * A + Side * S + FVector(0.f, 0.f, Z); };
	const int32 Treads = RisersPerFlight - 1;
	const float Slope = Rise / Going;
	const float RunnerWidth = Width - 70.f;

	for (int32 Step = 0; Step < RisersPerFlight; ++Step)
	{
		const float RiserBottom = Step * Rise;

		// The riser, set back under the nosing.
		Build.Box(P(Step * Going + 1.5f, 0.f, RiserBottom + Rise * 0.5f), Yaw, FVector(2.f, Width - 4.f, Rise), MatOakDark, false);

		if (Step >= Treads)
		{
			break; // the last riser comes up under the upper floor's own boards
		}

		// The tread, with its nosing standing three centimetres proud of the riser below. Worn
		// hollow down the middle: a darker board where the runner is gone and the feet went.
		const float Top = (Step + 1) * Rise;
		const bool bLoose = Wear.FRand() < 0.1f;
		const FRotator TreadRot = Yaw + FRotator(bLoose ? Wear.FRandRange(-1.5f, 1.5f) : 0.f, bLoose ? Wear.FRandRange(-1.2f, 1.2f) : 0.f, Wear.FRandRange(-0.3f, 0.3f));
		Build.Box(P(Step * Going + Going * 0.5f - 1.5f, 0.f, Top - 2.f + (bLoose ? 0.6f : 0.f)), TreadRot,
			FVector(Going + 3.f, Width - 4.f, 4.f), Wear.FRand() < 0.35f ? MatTreadWorn.Get() : MatTread.Get(), false);
		// The rounded nosing: a rod along the front edge, which is what takes the light on a stair.
		Build.Cyl(P(Step * Going - 1.5f, 0.f, Top - 2.f), Yaw + FRotator(0.f, 0.f, 90.f), FVector(4.2f, 4.2f, Width - 5.f), MatTread, false);

		// The runner: a length of carpet down each tread and riser, held by a brass rod in the
		// angle between them. A carpet runner is the one thing on these stairs that has an honest
		// straight edge. Some of it has gone, and some rods.
		if (bRunner && Wear.FRand() > 0.14f)
		{
			const float Shift = Wear.FRandRange(-3.f, 3.f);
			Build.Box(P(Step * Going + Going * 0.5f - 1.f, Shift, Top + 0.4f), Yaw, FVector(Going + 1.f, RunnerWidth, 0.8f), MatCarpet, false);
			Build.Box(P((Step + 1) * Going + 0.9f, Shift, Top + Rise * 0.5f), Yaw, FVector(0.8f, RunnerWidth, Rise), MatCarpet, false);
			if (Wear.FRand() > 0.25f)
			{
				Build.Cyl(P((Step + 1) * Going - 0.5f, Shift, Top + 1.6f), Yaw + FRotator(0.f, 0.f, 90.f), FVector(1.5f, 1.5f, RunnerWidth + 10.f), MatBrass, false);
				for (const float End : { -1.f, 1.f })
				{
					Build.Sph(P((Step + 1) * Going + 0.5f, Shift + End * (RunnerWidth * 0.5f + 5.f), Top + 1.6f), 2.6f, MatBrass);
				}
			}
			// Dust in the pile, heavier at the edges where the feet did not go.
			if (Wear.FRand() < 0.4f)
			{
				Build.Stain(RoomSurfaces::Damp, P(Step * Going + Going * 0.5f, Shift, Top + 8.f), FRotator(-90.f, Yaw.Yaw, 0.f),
					FVector2D(Going, RunnerWidth * 0.8f), FLinearColor(0.35f, 0.32f, 0.28f), 0.3f, 1.3f);
			}
		}
	}

	// The strings: the raking boards either side that the treads are housed in. Their top edge
	// rides a few centimetres over the pitch line — the line through the nosings.
	const float Length = FVector2D(Going * Treads + Going, Rise * (Treads + 1)).Size();
	const FRotator Rake = (Up * Going + FVector(0.f, 0.f, Rise)).Rotation();
	const FVector StringMid = P(Going * Treads * 0.5f, 0.f, Rise + Going * Treads * 0.5f * Slope - 12.f);
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(StringMid + Side * S * (Width * 0.5f - 2.5f), Rake, FVector(Length, 5.f, 34.f), MatOak, false);
		// A capping moulding along the top of the string.
		Build.Box(StringMid + Side * S * (Width * 0.5f - 2.5f) + FVector(0.f, 0.f, 18.f), Rake, FVector(Length, 7.f, 3.f), MatOak, false);
	}

	// The ramp. Its top surface runs from the foot of the bottom riser to the top of the last one.
	const FVector From = P(0.f, 0.f, 0.f);
	const FVector To = P(Going * Treads, 0.f, Rise * RisersPerFlight);
	const FVector Along = (To - From).GetSafeNormal();
	const FRotator RampRot = Along.Rotation();
	const FVector Normal = RampRot.RotateVector(FVector::UpVector);
	PawnOnly(Build.Box((From + To) * 0.5f - Normal * 5.f, RampRot, FVector(FVector::Dist(From, To) + 8.f, Width, 10.f), MatVoid));
}

void AGrandStaircaseActor::BuildFlights(FRoomBuilder& Build)
{
	// The central flight, up from the hall floor to the half-landing: wide, and the grand bottom
	// step with its rounded ends that every stair like this begins with.
	const FVector CentralFoot(FlightEastX(), Setup.CenterY, GroundZ);
	BuildFlight(Build, CentralFoot, FVector(-1.f, 0.f, 0.f), CentralWidth, 3101, true);
	{
		// The curtail step: the bottom tread swept out round the foot of each newel.
		const float Z = GroundZ + Rise;
		Build.Box(FVector(FlightEastX() + 14.f, Setup.CenterY, Z - Rise * 0.5f), FRotator::ZeroRotator, FVector(30.f, CentralWidth + 40.f, Rise), MatOakDark);
		Build.Box(FVector(FlightEastX() + 13.f, Setup.CenterY, Z - 2.f), FRotator::ZeroRotator, FVector(33.f, CentralWidth + 40.f, 4.f), MatTread, false);
		for (const float S : { -1.f, 1.f })
		{
			const FVector End(FlightEastX() + 14.f, Setup.CenterY + S * (CentralWidth * 0.5f + 20.f), Z);
			Build.Cyl(End - FVector(0.f, 0.f, Rise * 0.5f), FRotator::ZeroRotator, FVector(34.f, 34.f, Rise), MatOakDark);
			Build.Cyl(End - FVector(0.f, 0.f, 2.f), FRotator::ZeroRotator, FVector(37.f, 37.f, 4.f), MatTread, false);
		}
	}

	// The two return flights, up from the landing along the side walls to the gallery.
	BuildFlight(Build, FVector(LandingEdgeX(), NorthY() + SideWidth * 0.5f, LandingZ), FVector(1.f, 0.f, 0.f), SideWidth, 3202, true);
	BuildFlight(Build, FVector(LandingEdgeX(), SouthY() - SideWidth * 0.5f, LandingZ), FVector(1.f, 0.f, 0.f), SideWidth, 3303, true);

	// Spandrels: what closes the space under each flight, panelled, its top edge running up under
	// the string. One generated panel per side with its UVs in centimetres, so the panelling runs
	// on unbroken from one end to the other. It was a run of narrow boxes stepping up under the
	// string, each tiled and cropped on its own, and the panelling came out as vertical stripes.
	// The boxes are still there, hidden, as the collision.
	auto Spandrel = [&](float Y, float FootX, float Dir, float FootZ, float TopZ, float Face)
	{
		const float Run = Going * (RisersPerFlight - 1);
		const float Slope = Rise / Going;
		const float StartZ = FootZ + Rise - 14.f;
		const float EndZ = StartZ + Run * Slope;
		TArray<FVector2D> Outline = { FVector2D(0.f, GroundZ), FVector2D(Run, GroundZ) };
		if (EndZ > TopZ)
		{
			Outline.Add(FVector2D(Run, TopZ));
			Outline.Add(FVector2D((TopZ - StartZ) / Slope, TopZ));
		}
		else
		{
			Outline.Add(FVector2D(Run, EndZ));
		}
		if (StartZ > GroundZ)
		{
			Outline.Add(FVector2D(0.f, StartZ));
		}
		PanelMesh(this, HallRoot, Outline, FVector(FootX, Y + Face * 4.f, 0.f), FVector(Dir, 0.f, 0.f), FVector(0.f, Face, 0.f),
			RoomSurfaces::Wainscot.TexelSizeCm, MatWainscotSheet);

		const int32 Strips = 18;
		for (int32 i = 0; i < Strips; ++i)
		{
			const float A0 = Run * i / Strips;
			const float A1 = Run * (i + 1) / Strips;
			const float Height = FMath::Min(StartZ + A0 * Slope, TopZ) - GroundZ;
			if (Height > 1.f)
			{
				PawnOnly(Build.Box(FVector(FootX + Dir * (A0 + A1) * 0.5f, Y + Face * 2.f, GroundZ + Height * 0.5f), FRotator::ZeroRotator,
					FVector(A1 - A0 + 0.2f, 4.f, Height), MatVoid));
			}
		}
	};
	Spandrel(CentralNorthY(), FlightEastX(), -1.f, GroundZ, LandingZ, 1.f);
	Spandrel(CentralSouthY(), FlightEastX(), -1.f, GroundZ, LandingZ, -1.f);
	Spandrel(NorthInnerY(), LandingEdgeX(), 1.f, LandingZ, -FloorDepth, -1.f);
	Spandrel(SouthInnerY(), LandingEdgeX(), 1.f, LandingZ, -FloorDepth, 1.f);

	// Where the return flights meet the gallery, the space under them is closed off from the hall
	// by an end wall, and on the north side there is a cupboard door in it — the cupboard under
	// the stairs, which no house of this size was without.
	for (const bool bNorth : { true, false })
	{
		const float Y0 = bNorth ? NorthY() : SouthInnerY();
		const float Y1 = bNorth ? NorthInnerY() : SouthY();
		Build.Box(FVector(FlightEastX() - 2.f, (Y0 + Y1) * 0.5f, (GroundZ - FloorDepth) * 0.5f), FRotator::ZeroRotator,
			FVector(4.f, Y1 - Y0, -FloorDepth - GroundZ), MatWainscot);
	}
	const FVector Cupboard(FlightEastX() + 1.f, NorthY() + 80.f, GroundZ);
	Build.Box(Cupboard + FVector(0.f, 0.f, 80.f), FRotator::ZeroRotator, FVector(2.f, 66.f, 160.f), MatOakDark, false);
	for (const float Z : { 40.f, 120.f })
	{
		Build.Box(Cupboard + FVector(1.2f, 0.f, Z), FRotator::ZeroRotator, FVector(1.2f, 50.f, 60.f), MatOak, false);
	}
	Build.Sph(Cupboard + FVector(3.f, -24.f, 82.f), 4.f, MatBrass);
	// Open a crack, and dark in the crack.
	Build.Box(Cupboard + FVector(1.8f, 33.f, 80.f), FRotator::ZeroRotator, FVector(1.f, 2.2f, 158.f), MatVoid, false);
}

void AGrandStaircaseActor::Newel(FRoomBuilder& Build, const FVector& Base, float Height, bool bGrand)
{
	if (bGrand)
	{
		// The pair at the foot: a plinth, a panelled shaft, a moulded cap and a turned urn on it.
		// Every other post in the hall is a plain version of this one, which is how it was done.
		Build.Box(Base + FVector(0.f, 0.f, 13.f), FRotator::ZeroRotator, FVector(32.f, 32.f, 26.f), MatOakDark);
		Build.Box(Base + FVector(0.f, 0.f, 27.f), FRotator::ZeroRotator, FVector(28.f, 28.f, 4.f), MatOak, false);
		const float ShaftH = Height - 50.f;
		Build.Box(Base + FVector(0.f, 0.f, 29.f + ShaftH * 0.5f), FRotator::ZeroRotator, FVector(22.f, 22.f, ShaftH), MatOak);
		for (int32 Face = 0; Face < 4; ++Face)
		{
			const FRotator Turn(0.f, Face * 90.f, 0.f);
			Build.Box(Base + Turn.RotateVector(FVector(11.6f, 0.f, 0.f)) + FVector(0.f, 0.f, 29.f + ShaftH * 0.5f), Turn,
				FVector(1.2f, 14.f, ShaftH - 16.f), MatOakDark, false);
		}
		Build.Box(Base + FVector(0.f, 0.f, Height - 18.f), FRotator::ZeroRotator, FVector(28.f, 28.f, 5.f), MatOak, false);
		Build.Box(Base + FVector(0.f, 0.f, Height - 12.f), FRotator::ZeroRotator, FVector(32.f, 32.f, 7.f), MatOak, false);
		Build.Cyl(Base + FVector(0.f, 0.f, Height - 5.f), FRotator::ZeroRotator, FVector(12.f, 12.f, 8.f), MatOak, false);
		Build.Sph(Base + FVector(0.f, 0.f, Height + 8.f), 22.f, MatOak);
		Build.Add(FRoomShapes::Cone(), Base + FVector(0.f, 0.f, Height + 24.f), FRotator::ZeroRotator, FVector(9.f, 9.f, 14.f), MatOak, false);
		return;
	}

	Build.Box(Base + FVector(0.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(19.f, 19.f, 12.f), MatOakDark);
	Build.Box(Base + FVector(0.f, 0.f, Height * 0.5f), FRotator::ZeroRotator, FVector(15.f, 15.f, Height - 10.f), MatOak);
	Build.Box(Base + FVector(0.f, 0.f, Height - 4.f), FRotator::ZeroRotator, FVector(19.f, 19.f, 5.f), MatOak, false);
	Build.Sph(Base + FVector(0.f, 0.f, Height + 5.f), 13.f, MatOak);
}

void AGrandStaircaseActor::Blocker(FRoomBuilder& Build, const FVector& From, const FVector& To, float Height)
{
	const FVector Delta = To - From;
	const FRotator Rot = Delta.Rotation();
	PawnOnly(Build.Box((From + To) * 0.5f + FVector(0.f, 0.f, Height * 0.5f), Rot, FVector(Delta.Size(), 6.f, Height), MatVoid));
}

void AGrandStaircaseActor::Banister(FRoomBuilder& Build, const FVector& From, const FVector& To, int32 Seed)
{
	// From and To are on the line the balusters stand on: the floor, or a flight's pitch line.
	FRandomStream Wear(Seed);
	const FVector Delta = To - From;
	const float Length = Delta.Size();
	if (Length < 1.f)
	{
		return;
	}
	const FVector Dir = Delta / Length;
	const bool bRaking = FMath::Abs(Dir.Z) > 0.05f;
	const FVector RailUp(0.f, 0.f, RailHeight);

	// The handrail: a round rail on a moulded under-rail, both along the line.
	const FRotator Along = FRotationMatrix::MakeFromZ(Dir).Rotator();
	Build.Cyl((From + To) * 0.5f + RailUp + FVector(0.f, 0.f, 1.5f), Along, FVector(7.5f, 7.5f, Length + 4.f), MatOak, false);
	Build.Box((From + To) * 0.5f + RailUp - FVector(0.f, 0.f, 3.f), Dir.Rotation(), FVector(Length + 2.f, 5.5f, 4.f), MatOakDark, false);
	if (!bRaking)
	{
		// A level run has a bottom rail too; on a flight the string does that job.
		Build.Box((From + To) * 0.5f + FVector(0.f, 0.f, 5.f), Dir.Rotation(), FVector(Length, 6.f, 7.f), MatOak, false);
	}

	// Balusters, turned: a square block top and bottom and a shaft with a bulb low on it. Three
	// instanced meshes per run rather than three components per baluster — there are two hundred of
	// them. The first of each is built as an ordinary part only to get its tiled material.
	const float Spacing = bRaking ? 13.f : 12.f;
	const int32 Count = FMath::Max(1, FMath::FloorToInt(Length / Spacing));
	const float Base = bRaking ? 12.f : 8.5f;
	const float Top = RailHeight - 5.f;

	UStaticMeshComponent* SampleBlock = Build.Box(From + FVector(0.f, 0.f, -500.f), FRotator::ZeroRotator, FVector(4.6f, 4.6f, 9.f), MatOak, false);
	UStaticMeshComponent* SampleShaft = Build.Cyl(From + FVector(0.f, 0.f, -500.f), FRotator::ZeroRotator, FVector(3.2f, 3.2f, 60.f), MatOak, false);
	UMaterialInterface* BlockMat = SampleBlock ? SampleBlock->GetMaterial(0) : MatOak.Get();
	UMaterialInterface* ShaftMat = SampleShaft ? SampleShaft->GetMaterial(0) : MatOak.Get();
	for (UStaticMeshComponent* Sample : { SampleBlock, SampleShaft })
	{
		if (Sample)
		{
			Sample->SetHiddenInGame(true);
			Sample->SetCastShadow(false);
		}
	}

	UInstancedStaticMeshComponent* Blocks = Build.Instances(FRoomShapes::Cube(), BlockMat);
	UInstancedStaticMeshComponent* Shafts = Build.Instances(FRoomShapes::Cylinder(), ShaftMat);
	UInstancedStaticMeshComponent* Bulbs = Build.Instances(FRoomShapes::Sphere(), ShaftMat);
	if (!Blocks || !Shafts || !Bulbs)
	{
		return;
	}
	for (int32 i = 0; i < Count; ++i)
	{
		const float T = (i + 0.5f) / Count;
		const FVector Foot = From + Delta * T;
		const float Roll = Wear.FRand();
		if (Roll < 0.07f)
		{
			continue; // gone
		}
		// A few have snapped: the bottom half still in the string, the top half on the floor below.
		const bool bSnapped = Roll < 0.12f;
		const float Lean = Wear.FRandRange(-1.2f, 1.2f);
		const FQuat Tilt = FRotator(Lean * 0.5f, 0.f, Lean).Quaternion();
		const float Z0 = Foot.Z + Base;
		const float Z1 = Foot.Z + (bSnapped ? Wear.FRandRange(26.f, 44.f) : Top);
		Blocks->AddInstance(FTransform(Tilt, FVector(Foot.X, Foot.Y, Z0 + 4.5f), FVector(0.046f, 0.046f, 0.09f)));
		if (!bSnapped)
		{
			Blocks->AddInstance(FTransform(Tilt, FVector(Foot.X, Foot.Y, Z1 - 4.5f), FVector(0.046f, 0.046f, 0.09f)));
		}
		const float ShaftBottom = Z0 + 9.f;
		const float ShaftTop = bSnapped ? Z1 : Z1 - 9.f;
		Shafts->AddInstance(FTransform(Tilt, FVector(Foot.X, Foot.Y, (ShaftBottom + ShaftTop) * 0.5f), FVector(0.034f, 0.034f, (ShaftTop - ShaftBottom) / 100.f)));
		if (!bSnapped)
		{
			Bulbs->AddInstance(FTransform(Tilt, FVector(Foot.X, Foot.Y, ShaftBottom + 14.f), FVector(0.058f, 0.058f, 0.2f)));
			Bulbs->AddInstance(FTransform(Tilt, FVector(Foot.X, Foot.Y, ShaftTop - 7.f), FVector(0.045f, 0.045f, 0.06f)));
		}
	}

	// And webs strung between a few of them and the rail: the brief's cobwebs between railings.
	for (int32 i = 0; i < 2; ++i)
	{
		const float T = Wear.FRandRange(0.15f, 0.85f);
		const FVector At = From + Delta * T + FVector(0.f, 0.f, RailHeight * Wear.FRandRange(0.45f, 0.75f));
		Build.Add(FRoomShapes::Plane(), At, Dir.Rotation() + FRotator(0.f, 0.f, 90.f), FVector(Spacing * Wear.FRandRange(2.f, 3.4f), RailHeight * 0.5f, 1.f), MatWeb, false);
	}
}

void AGrandStaircaseActor::BuildBalustrades(FRoomBuilder& Build)
{
	const float GX = GalleryEdgeX() - 3.f;
	const float NY = NorthInnerY() + 3.f;
	const float SY = SouthInnerY() - 3.f;
	const float FX = FlightEastX();
	const float LX = LandingEdgeX() + 3.f;
	const float CN = CentralNorthY() + 3.f;
	const float CS = CentralSouthY() - 3.f;
	// Where the pitch line is at either end of a flight, in the flight's own terms.
	const float PitchTopReturn = LandingZ + Rise + Going * (RisersPerFlight - 1) * (Rise / Going);   // 0
	const float PitchFootReturn = LandingZ + Rise;
	const float PitchTopCentral = GroundZ + Rise + Going * (RisersPerFlight - 1) * (Rise / Going);  // -170
	const float PitchFootCentral = GroundZ + Rise;

	// The gallery: across the east end and along both sides to the stairheads.
	Banister(Build, FVector(GX, NY, 0.f), FVector(GX, SY, 0.f), 41);
	Banister(Build, FVector(GX, NY, 0.f), FVector(FX, NY, 0.f), 42);
	Banister(Build, FVector(GX, SY, 0.f), FVector(FX, SY, 0.f), 43);
	// Down the open side of each return flight.
	Banister(Build, FVector(FX, NY, PitchTopReturn), FVector(LX, NY, PitchFootReturn), 44);
	Banister(Build, FVector(FX, SY, PitchTopReturn), FVector(LX, SY, PitchFootReturn), 45);
	// Across the front of the landing, either side of the central flight.
	Banister(Build, FVector(LX, NY, LandingZ), FVector(LX, CN, LandingZ), 46);
	Banister(Build, FVector(LX, CS, LandingZ), FVector(LX, SY, LandingZ), 47);
	// And down both sides of the central flight.
	Banister(Build, FVector(LX, CN, PitchTopCentral), FVector(FX, CN, PitchFootCentral), 48);
	Banister(Build, FVector(LX, CS, PitchTopCentral), FVector(FX, CS, PitchFootCentral), 49);

	// Posts at every turn.
	for (const FVector& At : { FVector(GX, NY, 0.f), FVector(GX, SY, 0.f), FVector(FX, NY, 0.f), FVector(FX, SY, 0.f) })
	{
		Newel(Build, At, RailHeight + 16.f, false);
	}
	for (const FVector& At : { FVector(LX, NY, LandingZ), FVector(LX, SY, LandingZ), FVector(LX, CN, LandingZ), FVector(LX, CS, LandingZ) })
	{
		Newel(Build, At, RailHeight + 16.f, false);
	}
	// A pendant drop under the landing's two corner posts, where they come through the floor.
	for (const float Y : { NY, SY })
	{
		Build.Box(FVector(LX, Y, LandingZ - FloorDepth - 12.f), FRotator::ZeroRotator, FVector(15.f, 15.f, 24.f), MatOak, false);
		Build.Sph(FVector(LX, Y, LandingZ - FloorDepth - 28.f), 12.f, MatOak);
	}
	for (const float Y : { CN, CS })
	{
		Newel(Build, FVector(FX + 14.f, Y, GroundZ + Rise), RailHeight + 38.f, true);
	}

	// Invisible walls behind all of it, because a banister missing one baluster in eight is a
	// banister a capsule can slide through. On the flights they follow the pitch line.
	Blocker(Build, FVector(GX, NorthInnerY(), 0.f), FVector(GX, SouthInnerY(), 0.f), BlockerHeight);
	Blocker(Build, FVector(GalleryEdgeX(), NY, 0.f), FVector(FX, NY, 0.f), BlockerHeight);
	Blocker(Build, FVector(GalleryEdgeX(), SY, 0.f), FVector(FX, SY, 0.f), BlockerHeight);
	Blocker(Build, FVector(FX, NY, PitchTopReturn - Rise), FVector(LandingEdgeX(), NY, LandingZ), BlockerHeight + 20.f);
	Blocker(Build, FVector(FX, SY, PitchTopReturn - Rise), FVector(LandingEdgeX(), SY, LandingZ), BlockerHeight + 20.f);
	Blocker(Build, FVector(LX, NorthInnerY(), LandingZ), FVector(LX, CN, LandingZ), BlockerHeight);
	Blocker(Build, FVector(LX, CS, LandingZ), FVector(LX, SouthInnerY(), LandingZ), BlockerHeight);
	Blocker(Build, FVector(LandingEdgeX(), CN, LandingZ), FVector(FX, CN, GroundZ), BlockerHeight + 20.f);
	Blocker(Build, FVector(LandingEdgeX(), CS, LandingZ), FVector(FX, CS, GroundZ), BlockerHeight + 20.f);

	// A wall rail up the outer side of each return flight, on brass brackets.
	for (const float Y : { NorthY() + 6.f, SouthY() - 6.f })
	{
		const FVector From(LandingEdgeX(), Y, PitchFootReturn + RailHeight - 4.f);
		const FVector To(FX, Y, PitchTopReturn + RailHeight - 4.f);
		const FVector Dir = (To - From).GetSafeNormal();
		Build.Cyl((From + To) * 0.5f, FRotationMatrix::MakeFromZ(Dir).Rotator(), FVector(5.f, 5.f, FVector::Dist(From, To)), MatOak, false);
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector At = FMath::Lerp(From, To, (i + 0.5f) / 4.f);
			const float Out = Y < Setup.CenterY ? -1.f : 1.f;
			Build.Box(At + FVector(0.f, Out * 3.5f, -3.f), FRotator::ZeroRotator, FVector(1.5f, 7.f, 1.5f), MatBrass, false);
		}
	}
}

void AGrandStaircaseActor::BuildWallFinish(FRoomBuilder& Build)
{
	const float Z0 = GroundZ;
	const float Z1 = CeilingZ;

	// Plaster over every face, floor to ceiling. Everything else goes on top of it.
	WallFill(Build, EWall::North, WestX(), EastX(), Z0, Z1, MatPlaster);
	WallFill(Build, EWall::South, WestX(), EastX(), Z0, Z1, MatPlaster);
	WallFill(Build, EWall::East, NorthY(), SouthY(), Z0, Z1, MatPlaster);
	WallFill(Build, EWall::West, NorthY(), SouthY(), LandingZ, Z1, MatPlaster);

	// The entrance hall: dark oak wainscot to shoulder height on the three walls you can reach,
	// with a skirting and a dado rail, and the paper above it where the paper has held.
	const float Dado = Z0 + 118.f;
	struct FRun { EWall Wall; float U0; float U1; };
	const FRun HallWalls[] = {
		{ EWall::North, FlightEastX(), EastX() },
		{ EWall::South, FlightEastX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
	};
	for (const FRun& R : HallWalls)
	{
		WallFill(Build, R.Wall, R.U0, R.U1, Z0, Dado, MatWainscot, 0.3f);
		for (const FBox2D& Piece : CutAroundAll(Openings, R.Wall, R.U0, R.U1, Z0, Dado + 4.f))
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
		// Paper from the dado to the gallery soffit, in runs, as upstairs.
		const float StripWidth = 53.f;
		const int32 Strips = FMath::FloorToInt((R.U1 - R.U0) / StripWidth);
		for (int32 Strip = 0; Strip < Strips; ++Strip)
		{
			// Written out rather than rolled: in patches, never in stripes (the bedroom's 09-18 note).
			if ((Strip / 3) % 2 == (R.Wall == EWall::East ? 0 : 1))
			{
				continue;
			}
			const float A = R.U0 + Strip * StripWidth;
			const float Bottom = Dado + 4.f + Random.FRandRange(0.f, 20.f);
			WallFill(Build, R.Wall, A, A + StripWidth - 0.3f, Bottom, -FloorDepth - 2.f, (Strip % 2) ? MatWallpaper.Get() : MatWallpaperDark.Get(), 0.5f);
		}
	}

	// Mouldings that go all the way round: a band at the gallery floor's line, a picture rail over
	// the gallery, and the cornice. Straight lines are what make a hall look built rather than
	// found, and these are the only ones left up there.
	for (const EWall Wall : { EWall::North, EWall::South, EWall::East, EWall::West })
	{
		const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
		const float U0 = bAlongX ? WestX() : NorthY();
		const float U1 = bAlongX ? EastX() : SouthY();
		auto Band = [&](float ZA, float ZB, float Depth)
		{
			for (const FBox2D& Piece : CutAroundAll(Openings, Wall, U0, U1, ZA, ZB))
			{
				const FVector2D C = Piece.GetCenter();
				const FVector2D S = Piece.GetSize();
				if (S.X > 1.f && S.Y > 0.5f)
				{
					WallBox(Build, Wall, C.X, C.Y, S.X, S.Y, Depth, 0.f, MatOak);
				}
			}
		};
		Band(-FloorDepth - 4.f, 0.f, 3.f);
		Band(GalleryPictureRail - 2.f, GalleryPictureRail + 2.f, 2.5f);
		Band(CeilingZ - 16.f, CeilingZ, 14.f);
		Band(CeilingZ - 26.f, CeilingZ - 16.f, 7.f);
	}

	// Moulded panels on the upper walls, over the flights and the gallery: the thin applied frames
	// a Victorian hall was divided up with. Paper survives inside a few of them.
	auto Panel = [&](EWall Wall, float U0, float U1, float ZA, float ZB, bool bPaper)
	{
		const float W = 5.f;
		WallBox(Build, Wall, (U0 + U1) * 0.5f, ZA + W * 0.5f, U1 - U0, W, 2.4f, 0.f, MatOak);
		WallBox(Build, Wall, (U0 + U1) * 0.5f, ZB - W * 0.5f, U1 - U0, W, 2.4f, 0.f, MatOak);
		WallBox(Build, Wall, U0 + W * 0.5f, (ZA + ZB) * 0.5f, W, ZB - ZA, 2.4f, 0.f, MatOak);
		WallBox(Build, Wall, U1 - W * 0.5f, (ZA + ZB) * 0.5f, W, ZB - ZA, 2.4f, 0.f, MatOak);
		if (bPaper)
		{
			WallFill(Build, Wall, U0 + W, U1 - W, ZA + W, ZB - W, MatWallpaperDark, 0.4f);
		}
	};
	for (const EWall Wall : { EWall::North, EWall::South })
	{
		const bool bNorth = Wall == EWall::North;
		// Over the gallery and the return flight.
		Panel(Wall, GalleryEdgeX() - 150.f, GalleryEdgeX() + 20.f, 40.f, GalleryPictureRail - 20.f, bNorth);
		Panel(Wall, FlightEastX() + 10.f, GalleryEdgeX() - 170.f, 40.f, GalleryPictureRail - 20.f, !bNorth);
		Panel(Wall, LandingEdgeX() + 20.f, FlightEastX() - 20.f, 20.f, GalleryPictureRail - 20.f, bNorth);
		// Over the landing, running down to it.
		Panel(Wall, WestX() + 20.f, LandingEdgeX() - 10.f, LandingZ + 110.f, GalleryPictureRail - 20.f, false);
	}
	// Either side of the window on the landing.
	Panel(EWall::West, NorthY() + 20.f, Setup.CenterY - WindowWidth * 0.5f - 40.f, LandingZ + 40.f, LandingZ + 330.f, false);
	Panel(EWall::West, Setup.CenterY + WindowWidth * 0.5f + 40.f, SouthY() - 20.f, LandingZ + 40.f, LandingZ + 330.f, true);

	// The gallery walls get the wainscot's dado, and paper from it to the picture rail in runs.
	for (const FRun& R : { FRun{ EWall::East, NorthY(), SouthY() } })
	{
		for (const FBox2D& Piece : CutAroundAll(Openings, R.Wall, R.U0, R.U1, 88.f, 96.f))
		{
			WallBox(Build, R.Wall, Piece.GetCenter().X, 92.f, Piece.GetSize().X, 7.f, 3.2f, 0.f, MatOak);
		}
		WallFill(Build, R.Wall, NorthY() + 40.f, Setup.CenterY - Setup.OpeningWidth * 0.5f - 40.f, 98.f, GalleryPictureRail - 4.f, MatWallpaper, 0.4f);
		WallFill(Build, R.Wall, Setup.CenterY + Setup.OpeningWidth * 0.5f + 90.f, SouthY() - 60.f, 98.f, GalleryPictureRail - 4.f, MatWallpaperDark, 0.4f);
	}

	// The archway from the corridor, cased on this side, and lined through the wall.
	{
		const float Half = Setup.OpeningWidth * 0.5f;
		const float H = Setup.OpeningHeight;
		const float Casing = 14.f;
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, EWall::East, Setup.CenterY + S * (Half + Casing * 0.5f), (H + Casing) * 0.5f, Casing, H + Casing, 3.f, 0.f, MatOak);
			WallBox(Build, EWall::East, Setup.CenterY + S * (Half + Casing * 0.5f), 14.f, Casing + 3.f, 28.f, 4.5f, 0.f, MatOak);
			Build.Box(FVector(EastX() + Setup.WallThickness * 0.5f, Setup.CenterY + S * (Half - 1.f), H * 0.5f), FRotator::ZeroRotator,
				FVector(Setup.WallThickness, 2.f, H), MatOak, false);
		}
		WallBox(Build, EWall::East, Setup.CenterY, H + Casing * 0.5f, Setup.OpeningWidth + Casing * 2.f, Casing, 3.f, 0.f, MatOak);
		WallBox(Build, EWall::East, Setup.CenterY, H + Casing + 3.f, Setup.OpeningWidth + Casing * 2.f + 10.f, 6.f, 5.f, 0.f, MatOak);
		// A keystone, because it is the entrance to the grandest room in the house.
		WallBox(Build, EWall::East, Setup.CenterY, H + 6.f, 18.f, 26.f, 5.f, 0.f, MatOak);
		Build.Box(FVector(EastX() + Setup.WallThickness * 0.5f, Setup.CenterY, H - 1.f), FRotator::ZeroRotator,
			FVector(Setup.WallThickness, Setup.OpeningWidth, 2.f), MatOak, false);
	}

	// The front door's casing: heavier than the others, with a cornice over it.
	{
		const float Half = 60.f;
		const float H = GroundZ + 250.f;
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, EWall::East, Setup.CenterY + S * (Half + 9.f), (GroundZ + H + 18.f) * 0.5f, 18.f, H + 18.f - GroundZ, 4.f, 0.f, MatOak);
		}
		WallBox(Build, EWall::East, Setup.CenterY, H + 9.f, Half * 2.f + 36.f, 18.f, 4.f, 0.f, MatOak);
		WallBox(Build, EWall::East, Setup.CenterY, H + 22.f, Half * 2.f + 50.f, 8.f, 7.f, 0.f, MatOak);
	}

	// The two side doors' casings.
	for (const EWall Wall : { EWall::North, EWall::South })
	{
		const float U = -1560.f;
		const float H = GroundZ + 212.f;
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, Wall, U + S * 56.f, (GroundZ + H + 12.f) * 0.5f, 12.f, H + 12.f - GroundZ, 3.f, 0.3f, MatOak);
		}
		WallBox(Build, Wall, U, H + 6.f, 124.f, 12.f, 3.f, 0.3f, MatOak);
	}
}

void AGrandStaircaseActor::BuildCeiling(FRoomBuilder& Build)
{
	const float MidX = EastX() - HallLength * 0.5f;
	Build.Mark(FVector(MidX, Setup.CenterY, CeilingZ), FRotator(0.f, 0.f, 180.f), FVector2D(HallLength, HallWidth), MatCeiling);

	// Exposed beams across the hall, on wall plates, one of them carrying the chandelier. Seven of
	// them at a metre and a half: close enough that looking up from the hall floor is looking into
	// a ribcage, which is the feeling the brief wants from the ceiling.
	const float BeamDepth = 34.f;
	for (int32 i = -4; i <= 2; ++i)
	{
		const float X = ChandelierX() + i * 150.f;
		if (X > EastX() - 20.f || X < WestX() + 20.f)
		{
			continue;
		}
		Build.Box(FVector(X, Setup.CenterY, CeilingZ - BeamDepth * 0.5f), FRotator(0.f, 0.f, Random.FRandRange(-0.3f, 0.3f)),
			FVector(24.f, HallWidth, BeamDepth), MatBeam, false);
		// Iron straps where the beam sits on the plate.
		for (const float S : { -1.f, 1.f })
		{
			Build.Box(FVector(X, Setup.CenterY + S * (HallWidth * 0.5f - 34.f), CeilingZ - BeamDepth * 0.5f), FRotator::ZeroRotator,
				FVector(25.f, 5.f, BeamDepth + 1.f), MatIron, false);
		}
	}
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(MidX, Setup.CenterY + S * (HallWidth * 0.5f - 12.f), CeilingZ - 40.f), FRotator::ZeroRotator,
			FVector(HallLength, 24.f, 26.f), MatBeam, false);
	}

	// Damp, spreading from the window end, and one big crack across the plaster.
	for (int32 i = 0; i < 10; ++i)
	{
		const float X = (i < 4) ? WestX() + Random.FRandRange(40.f, 300.f) : Random.FRandRange(WestX() + 60.f, EastX() - 60.f);
		Build.Stain(RoomSurfaces::Damp, FVector(X, Setup.CenterY + Random.FRandRange(-420.f, 420.f), CeilingZ - 2.f), FRotator(90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(120.f, 280.f), Random.FRandRange(90.f, 200.f)), FLinearColor(0.32f, 0.27f, 0.21f), Random.FRandRange(0.35f, 0.6f), 1.2f);
	}
	Build.Crack(FVector(MidX - 180.f, Setup.CenterY - 120.f, CeilingZ - 2.f), FRotator(90.f, 0.f, 30.f), FVector2D(420.f, 200.f), 0.8f, 16.f);

	// Cobwebs between the beams, thickest at the window end and in the corners.
	for (int32 i = 0; i < 16; ++i)
	{
		const int32 Bay = Random.RandRange(-4, 1);
		const float X = ChandelierX() + Bay * 150.f + 75.f;
		if (X > EastX() - 30.f || X < WestX() + 30.f)
		{
			continue;
		}
		const bool bNorth = Random.FRand() < 0.5f;
		const float Y = bNorth ? NorthY() + Random.FRandRange(30.f, 260.f) : SouthY() - Random.FRandRange(30.f, 260.f);
		Build.Add(FRoomShapes::Plane(), FVector(X, Y, CeilingZ - Random.FRandRange(10.f, 30.f)),
			FRotator(Random.FRandRange(60.f, 85.f), 0.f, Random.FRandRange(-12.f, 12.f)), FVector(130.f, Random.FRandRange(60.f, 140.f), 1.f), MatWeb, false);
	}
	// And in the four upper corners, across the corner.
	const FVector2D Corners[4] = { { WestX(), NorthY() }, { EastX(), NorthY() }, { WestX(), SouthY() }, { EastX(), SouthY() } };
	for (const FVector2D& Corner : Corners)
	{
		const float SX = Corner.X < Setup.EastFace - 1.f ? 1.f : -1.f;
		const float SY = Corner.Y < Setup.CenterY ? 1.f : -1.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Inset = 30.f + i * 24.f;
			Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SX * Inset, Corner.Y + SY * Inset, CeilingZ - 40.f - i * 8.f),
				FRotator(0.f, SX * SY > 0.f ? 45.f : -45.f, 180.f), FVector(Inset * 1.7f, Inset * 1.7f, 1.f), MatWeb, false);
		}
	}
}

void AGrandStaircaseActor::BuildStainedGlass(FRoomBuilder& Build)
{
	// The window over the landing. The glass is a baked drawing (Tools/make_stained_glass.py) on a
	// sheet in the middle of the wall, with the lead on a second sheet a few millimetres in front of
	// it; the mullions, the transom and the frame are timber laid over both.
	//
	// The glass does not cast a shadow and the lead does. That is the whole trick: the storm light
	// outside the window passes through the colour and is stopped by the cames, so every strike
	// lays the drawing of the window across the stairs.
	const float Sill = LandingZ + WindowSill;
	const float MidZ = Sill + WindowHeight * 0.5f;
	const float GlassX = WestX() - 12.f;

	UMaterialInterface* GlassBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomStainedGlass.M_RoomStainedGlass"));
	UMaterialInterface* LeadBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomLeadCame.M_RoomLeadCame"));
	if (GlassBase && LeadBase)
	{
		StainedGlass = UMaterialInstanceDynamic::Create(GlassBase, this);
		if (UProceduralMeshComponent* Glass = Sheet(this, HallRoot, FVector(GlassX, Setup.CenterY, MidZ), WindowWidth, WindowHeight, StainedGlass))
		{
			Glass->SetCastShadow(false);
		}
		Sheet(this, HallRoot, FVector(GlassX + 0.4f, Setup.CenterY, MidZ), WindowWidth, WindowHeight, UMaterialInstanceDynamic::Create(LeadBase, this));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("M_RoomStainedGlass not found — run python Tools/make_stained_glass.py, then Tools/build_art.py -ArtStage=stainedglass"));
	}

	// The joinery. U is measured from the left as seen from the landing, which faces -X, so the
	// viewer's left is +Y: y = CenterY + Width/2 - u. The numbers are the generator's.
	auto AtU = [&](float U) { return Setup.CenterY + WindowWidth * 0.5f - U; };
	const float Depth = 16.f;
	for (const float U : { 94.f, 206.f })
	{
		Build.Box(FVector(GlassX, AtU(U), Sill + 170.f), FRotator::ZeroRotator, FVector(Depth, 12.f, 340.f), MatOak, false);
		// Moulded on the room side: a narrower fillet standing proud of the mullion's face.
		Build.Box(FVector(GlassX + Depth * 0.5f + 1.f, AtU(U), Sill + 170.f), FRotator::ZeroRotator, FVector(2.f, 6.f, 336.f), MatOakDark, false);
	}
	Build.Box(FVector(GlassX, Setup.CenterY, Sill + 346.f), FRotator::ZeroRotator, FVector(Depth, WindowWidth, 12.f), MatOak, false);
	Build.Box(FVector(GlassX + Depth * 0.5f + 1.f, Setup.CenterY, Sill + 346.f), FRotator::ZeroRotator, FVector(2.f, WindowWidth, 6.f), MatOakDark, false);
	// The frame round the opening, and the reveal lining through the wall.
	const float T = Setup.WallThickness;
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(WestX() - T * 0.5f, Setup.CenterY + S * (WindowWidth * 0.5f - 3.f), MidZ), FRotator::ZeroRotator, FVector(T, 6.f, WindowHeight), MatOak, false);
	}
	Build.Box(FVector(WestX() - T * 0.5f, Setup.CenterY, Sill + WindowHeight - 3.f), FRotator::ZeroRotator, FVector(T, WindowWidth, 6.f), MatOak, false);
	// A deep sill, standing into the landing, that the rain through the broken light has rotted.
	Build.Box(FVector(WestX() - T * 0.5f + 8.f, Setup.CenterY, Sill - 3.f), FRotator(-1.f, 0.f, 0.f), FVector(T + 18.f, WindowWidth + 30.f, 6.f), MatOak);
	Build.Box(FVector(WestX() + 12.f, Setup.CenterY, Sill - 11.f), FRotator::ZeroRotator, FVector(4.f, WindowWidth + 20.f, 10.f), MatOakDark, false);
	Build.Stain(RoomSurfaces::Damp, FVector(WestX() + 4.f, AtU(44.f), Sill + 4.f), FRotator(-90.f, 0.f, 0.f), FVector2D(40.f, 90.f),
		FLinearColor(0.16f, 0.13f, 0.10f), 0.8f, 1.1f, 0.25f);
	// An architrave round it on the wall face.
	for (const float S : { -1.f, 1.f })
	{
		WallBox(Build, EWall::West, Setup.CenterY + S * (WindowWidth * 0.5f + 8.f), MidZ + 8.f, 16.f, WindowHeight + 16.f, 3.5f, 0.f, MatOak);
	}
	WallBox(Build, EWall::West, Setup.CenterY, Sill + WindowHeight + 8.f, WindowWidth + 32.f, 16.f, 3.5f, 0.f, MatOak);
	WallBox(Build, EWall::West, Setup.CenterY, Sill + WindowHeight + 19.f, WindowWidth + 46.f, 6.f, 6.f, 0.f, MatOak);

	// A pair of sconces either side, candles long burnt down. Out of the west wall is +X, yaw 0.
	for (const float S : { -1.f, 1.f })
	{
		Build.Sconce(WallPoint(EWall::West, Setup.CenterY + S * (WindowWidth * 0.5f + 55.f), LandingZ + 170.f, 0.f), 0.f,
			S < 0.f ? 6.f : 10.f, MatBrass, MatWax, MatShadow);
	}
}

void AGrandStaircaseActor::BuildChandelier(FRoomBuilder& Build)
{
	// The chandelier hangs from the middle beam over the void, on a long chain, at the height of
	// the gallery rail: from the archway it is the first thing in front of the window, at eye
	// level, close enough to see that the crystals are not all there.
	const float BeamUnderside = CeilingZ - 34.f;
	const FVector Hook(ChandelierX(), Setup.CenterY, BeamUnderside);
	Build.Cyl(Hook - FVector(0.f, 0.f, 1.5f), FRotator::ZeroRotator, FVector(22.f, 22.f, 3.f), MatBrass, false);
	Build.Sph(Hook - FVector(0.f, 0.f, 4.f), 8.f, MatBrass);

	ChandelierPivot = NewObject<USceneComponent>(this, TEXT("ChandelierPivot"));
	ChandelierPivot->SetMobility(EComponentMobility::Movable);
	ChandelierPivot->AttachToComponent(HallRoot, FAttachmentTransformRules::KeepRelativeTransform);
	ChandelierPivot->SetRelativeLocation(Hook - FVector(0.f, 0.f, 6.f));
	ChandelierPivot->RegisterComponent();
	AddInstanceComponent(ChandelierPivot);

	FRoomBuilder Hang(this, ChandelierPivot);
	const float Chain = 250.f;
	for (int32 Link = 0; Link * 6.f < Chain; ++Link)
	{
		Hang.Box(FVector(0.f, 0.f, -3.f - Link * 6.f), FRotator(0.f, (Link % 2) * 90.f, 0.f), FVector(1.f, 3.4f, 7.f), MatIron, false);
	}

	// Chandelier_03 has its pivot at the top of its rod, so it hangs from the end of the chain as
	// it stands. Over two metres of brass and crystal, dulled with sixty years of dust.
	if (UStaticMeshComponent* Body = Hang.Prop(RoomProps::Chandelier, FVector(0.f, 0.f, -Chain), FRotator(0.f, 17.f, 0.f), 215.f, false))
	{
		FRoomShapes::TintSlots(Body, FLinearColor(0.24f, 0.21f, 0.17f), 0);
		FRoomShapes::TintSlots(Body, FLinearColor(0.55f, 0.55f, 0.56f), 1);
	}

	// What is left of the drops: a few hanging on threads at the wrong lengths from arms that have
	// lost the rest, turning slowly when the rest of it does not.
	FRandomStream Drops(4111);
	for (int32 i = 0; i < 7; ++i)
	{
		const float A = Drops.FRandRange(0.f, 2.f * PI);
		const float R = Drops.FRandRange(50.f, 76.f);
		const float Hangs = Drops.FRandRange(8.f, 34.f);
		const FVector Arm(FMath::Cos(A) * R, FMath::Sin(A) * R, -Chain - 142.f);
		Hang.Box(Arm - FVector(0.f, 0.f, Hangs * 0.5f), FRotator::ZeroRotator, FVector(0.25f, 0.25f, Hangs), MatIron, false);
		Hang.Add(FRoomShapes::Cone(), Arm - FVector(0.f, 0.f, Hangs + 4.f), FRotator(180.f, 0.f, 0.f), FVector(3.6f, 3.6f, 9.f), MatCrystal, false);
	}

	// And the drops that fell, on the tiles under it: a scatter of crystal and one whole arm, with
	// the candle still in its cup.
	const FVector Under(ChandelierX(), Setup.CenterY, GroundZ);
	for (int32 i = 0; i < 16; ++i)
	{
		const float R = Drops.FRandRange(0.f, 110.f);
		const float A = Drops.FRandRange(0.f, 2.f * PI);
		const FVector At = Under + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 1.6f);
		if (i % 3 == 0)
		{
			Build.Add(FRoomShapes::Cone(), At + FVector(0.f, 0.f, 0.4f), FRotator(90.f, Drops.FRandRange(0.f, 360.f), 0.f), FVector(3.6f, 3.6f, 9.f), MatCrystal, false);
		}
		else
		{
			Build.Box(At, FRotator(0.f, Drops.FRandRange(0.f, 360.f), 0.f), FVector(Drops.FRandRange(1.5f, 4.f), Drops.FRandRange(1.2f, 3.f), 0.8f), MatCrystal, false);
		}
	}
	Build.Cyl(Under + FVector(40.f, -30.f, 1.4f), FRotator(90.f, 28.f, 0.f), FVector(2.4f, 2.4f, 44.f), MatBrass, false);
	Build.Cyl(Under + FVector(58.f, -21.f, 3.f), FRotator::ZeroRotator, FVector(7.f, 7.f, 3.f), MatBrass, false);
	Build.Cyl(Under + FVector(58.f, -21.f, 7.f), FRotator::ZeroRotator, FVector(2.2f, 2.2f, 6.f), MatWax, false);
	Build.Crack(Under + FVector(0.f, 0.f, 10.f), FRotator(-90.f, 0.f, 0.f), FVector2D(90.f, 70.f), 0.6f, 30.f);
}

void AGrandStaircaseActor::BuildDamage(FRoomBuilder& Build)
{
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
		const float ZLow = Wall == EWall::West ? LandingZ : GroundZ;

		for (int32 i = 0; i < 16; ++i)
		{
			Stain(Wall, Random.FRandRange(U0, U1), Random.FRandRange(ZLow + 40.f, CeilingZ - 30.f), Random.FRandRange(120.f, 280.f), Random.FRandRange(100.f, 240.f),
				RoomSurfaces::Damp, DampTint * 0.85f, Random.FRandRange(0.14f, 0.28f), Random.FRandRange(0.f, 360.f), 1.15f);
		}
		// Water down from the ceiling line, in long runs: the roof over a hall this size has been
		// letting the storm in for years.
		for (int32 i = 0; i < 6; ++i)
		{
			const float Drop = Random.FRandRange(120.f, 320.f);
			Stain(Wall, Random.FRandRange(U0, U1), CeilingZ - Drop * 0.4f, Random.FRandRange(80.f, 180.f), Drop,
				RoomSurfaces::Damp, DampTint * 0.72f, Random.FRandRange(0.4f, 0.6f), 0.f, 1.25f);
		}
		// Cracks, in clusters.
		for (int32 i = 0; i < 8; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float Z = Random.FRandRange(ZLow + 140.f, CeilingZ - 60.f);
			if (!IsOnOpening(Wall, U, Z, 55.f, 55.f))
			{
				FVector Location;
				FRotator Rotation;
				AimAt(Wall, U, Z, 0.f, Location, Rotation);
				Build.Crack(Location, Rotation, FVector2D(Random.FRandRange(100.f, 220.f), Random.FRandRange(120.f, 260.f)), Random.FRandRange(0.65f, 1.f), Random.FRandRange(14.f, 26.f));
			}
		}
		// Plaster fallen away to the brick, big patches high up — the brief's exposed brickwork —
		// and the plaster that came off is on the floor under each one (see BuildDebris).
		for (int32 i = 0; i < 5; ++i)
		{
			const float U = Random.FRandRange(U0 + 60.f, U1 - 60.f);
			const float Z = Random.FRandRange(60.f, CeilingZ - 80.f);
			if (!IsOnOpening(Wall, U, Z, 60.f, 60.f))
			{
				Stain(Wall, U, Z, Random.FRandRange(60.f, 150.f), Random.FRandRange(70.f, 160.f), RoomSurfaces::Substrate, SubstrateTint, Random.FRandRange(0.85f, 1.f), Random.FRandRange(0.f, 360.f), 0.9f);
			}
		}
		// Mould in the corners, low and high, and along the floor line.
		for (const float Corner : { U0, U1 })
		{
			const float U = Corner + (Corner == U0 ? 1.f : -1.f) * Random.FRandRange(10.f, 50.f);
			Stain(Wall, U, ZLow + 40.f, Random.FRandRange(70.f, 140.f), Random.FRandRange(90.f, 160.f), RoomSurfaces::Damp, MouldTint, 0.72f, 0.f, 1.1f);
			Stain(Wall, U, CeilingZ - 40.f, Random.FRandRange(80.f, 150.f), Random.FRandRange(70.f, 130.f), RoomSurfaces::Damp, MouldTint, 0.62f, 0.f, 1.1f);
		}
	}

	// Under the window, where the rain has been coming in through the broken light for years: a
	// long dark stain down the wall below the sill and a bloom of mould either side of it.
	Stain(EWall::West, Setup.CenterY + 100.f, LandingZ + 40.f, 90.f, 90.f, RoomSurfaces::Damp, MouldTint, 0.8f, 0.f, 1.2f);
	for (int32 i = 0; i < 4; ++i)
	{
		const float U = Setup.CenterY + (i % 2 ? -1.f : 1.f) * Random.FRandRange(150.f, 190.f);
		Stain(EWall::West, U, Random.FRandRange(LandingZ + 60.f, CeilingZ - 100.f), Random.FRandRange(40.f, 70.f), Random.FRandRange(100.f, 200.f), RoomSurfaces::Damp, MouldTint, 0.75f, 0.f, 1.1f);
	}

	// The floors. Water on the tiles under the landing and the chandelier; cracked tiles; dust along
	// every wall; and on the gallery by the north stairhead, scratches in the boards — long, deep,
	// parallel, running to the top step, as if something heavy was dragged to it.
	const FLinearColor DustTint(0.42f, 0.40f, 0.36f);
	for (int32 i = 0; i < 22; ++i)
	{
		const bool bNorth = Random.FRand() < 0.5f;
		const FVector At(Random.FRandRange(FlightEastX() + 30.f, EastX() - 30.f), bNorth ? NorthY() + Random.FRandRange(10.f, 36.f) : SouthY() - Random.FRandRange(10.f, 36.f), GroundZ + 10.f);
		Build.Stain(RoomSurfaces::Damp, At, FRotator(-90.f, 0.f, Random.FRandRange(-20.f, 20.f)), FVector2D(Random.FRandRange(60.f, 150.f), Random.FRandRange(24.f, 44.f)), DustTint, Random.FRandRange(0.22f, 0.36f), 1.35f);
	}
	for (const FVector2D& Spot : { FVector2D(ChandelierX() + 20.f, Setup.CenterY - 60.f), FVector2D(FlightEastX() + 110.f, Setup.CenterY + 280.f), FVector2D(EastX() - 140.f, Setup.CenterY - 260.f) })
	{
		Build.Stain(RoomSurfaces::Damp, FVector(Spot.X, Spot.Y, GroundZ + 10.f), FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(120.f, 200.f), Random.FRandRange(80.f, 140.f)), FLinearColor(0.14f, 0.12f, 0.10f), 0.7f, 1.2f, 0.25f);
	}
	for (int32 i = 0; i < 6; ++i)
	{
		Build.Crack(FVector(Random.FRandRange(FlightEastX() + 40.f, EastX() - 60.f), Setup.CenterY + Random.FRandRange(-420.f, 420.f), GroundZ + 8.f),
			FRotator(-90.f, 0.f, Random.FRandRange(0.f, 180.f)), FVector2D(Random.FRandRange(60.f, 140.f), Random.FRandRange(40.f, 90.f)), 0.9f, Random.FRandRange(28.f, 40.f));
	}
	for (int32 i = 0; i < 5; ++i)
	{
		const float Y = NorthY() + 60.f + i * 8.f + Random.FRandRange(-2.f, 2.f);
		Build.Crack(FVector(FlightEastX() + 110.f + Random.FRandRange(-20.f, 20.f), Y, 8.f), FRotator(-90.f, 0.f, 90.f + Random.FRandRange(-3.f, 3.f)),
			FVector2D(Random.FRandRange(10.f, 16.f), Random.FRandRange(150.f, 210.f)), 1.f, Random.FRandRange(44.f, 56.f));
	}
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector At(Random.FRandRange(WestX() + 20.f, LandingEdgeX() - 20.f), Setup.CenterY + Random.FRandRange(-440.f, 440.f), LandingZ + 10.f);
		Build.Stain(RoomSurfaces::Damp, At, FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)), FVector2D(Random.FRandRange(60.f, 150.f), Random.FRandRange(40.f, 90.f)), DustTint, Random.FRandRange(0.25f, 0.4f), 1.3f);
	}
	// Standing water under the broken light, on the landing boards.
	Build.Stain(RoomSurfaces::Damp, FVector(WestX() + 40.f, Setup.CenterY + 100.f, LandingZ + 10.f), FRotator(-90.f, 0.f, 20.f), FVector2D(90.f, 60.f),
		FLinearColor(0.10f, 0.09f, 0.08f), 0.85f, 1.1f, 0.12f);
}

void AGrandStaircaseActor::BuildFurniture(FRoomBuilder& Build)
{
	// The hall floor. Nothing in the middle of it: the way from the stairs to the door is clear,
	// and everything stands against the walls where a hall keeps its furniture.

	// A console against the north wall between the door and the corner, with a candelabrum on it,
	// a vase of flowers long dead, and above it the clean rectangle where a mirror hung.
	const FVector ConsoleSeat(EastX() - 130.f, NorthY() + 30.f, GroundZ);
	if (UStaticMeshComponent* Console = Build.PropSeated(RoomProps::Console, ConsoleSeat, FRotator::ZeroRotator, 0.f))
	{
		FRoomShapes::TintSlots(Console, FLinearColor(0.60f, 0.56f, 0.50f));
	}
	const float ConsoleTop = GroundZ + 95.f;
	if (UStaticMeshComponent* Candles = Build.PropSeated(RoomProps::Candelabra, ConsoleSeat + FVector(-20.f, 4.f, 95.f), FRotator::ZeroRotator, 0.f, false))
	{
		// Its candles are modelled lit. These went out a very long time ago: the flames go black,
		// which is a wick, and the rest is dulled.
		Candles->SetMaterial(1, MatShadow);
		FRoomShapes::TintSlots(Candles, FLinearColor(0.45f, 0.40f, 0.34f), 0);
		FRoomShapes::TintSlots(Candles, FLinearColor(0.45f, 0.40f, 0.34f), 2);
		FRoomShapes::TintSlots(Candles, FLinearColor(0.50f, 0.46f, 0.38f), 3);
		FRoomShapes::TintSlots(Candles, FLinearColor(0.45f, 0.40f, 0.34f), 4);
	}
	const FVector VaseSeat = ConsoleSeat + FVector(56.f, 2.f, 95.f);
	if (UStaticMeshComponent* Vase = Build.PropSeated(RoomProps::CeramicVase, VaseSeat, FRotator(0.f, 40.f, 0.f), 30.f, false))
	{
		FRoomShapes::TintSlots(Vase, FLinearColor(0.50f, 0.48f, 0.44f));
	}
	{
		// Roses dried on the stem, heads bowed over the rim, and petals on the marble round it.
		FRandomStream Stems(88);
		for (int32 i = 0; i < 9; ++i)
		{
			const float A = Stems.FRandRange(0.f, 2.f * PI);
			const float Lean = Stems.FRandRange(8.f, 30.f);
			const float Length = Stems.FRandRange(34.f, 48.f);
			const FRotator Stem(Lean * FMath::Cos(A), 0.f, Lean * FMath::Sin(A));
			const FVector Base = VaseSeat + FVector(0.f, 0.f, 24.f);
			Build.Cyl(Base + Stem.RotateVector(FVector(0.f, 0.f, Length * 0.5f)), Stem, FVector(0.6f, 0.6f, Length), MatStem, false);
			const FVector Head = Base + Stem.RotateVector(FVector(0.f, 0.f, Length)) - FVector(0.f, 0.f, Stems.FRandRange(0.f, 6.f));
			Build.Sph(Head, Stems.FRandRange(3.2f, 4.4f), MatPetal);
		}
		for (int32 i = 0; i < 7; ++i)
		{
			Build.Sph(FVector(VaseSeat.X + Stems.FRandRange(-26.f, 20.f), VaseSeat.Y + Stems.FRandRange(-10.f, 12.f), ConsoleTop + 0.6f), 1.6f, MatPetal);
		}
	}
	// The mirror's ghost: the one place a hard edge is right, the wall the dirt never reached.
	Build.Mark(WallPoint(EWall::North, ConsoleSeat.X, GroundZ + 200.f, 0.8f), FRotator(0.f, 0.f, 90.f), FVector2D(90.f, 110.f), MatWallpaper);
	Build.Sph(WallPoint(EWall::North, ConsoleSeat.X, GroundZ + 262.f, 1.f), 1.8f, MatIron);

	// A dust sheet over an armchair by the parlour door: the house was being shut up properly once.
	const FVector ChairSeat(-1700.f, SouthY() - 70.f, GroundZ);
	Build.PropSeated(RoomProps::Armchair, ChairSeat, FRotator(0.f, 200.f, 0.f), 0.f);
	Build.Cloth(ChairSeat + FVector(0.f, 0.f, 104.f), FRotator(0.f, 20.f, 0.f), FVector2D(170.f, 150.f), 5.5f, 70.f, 2203,
		Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.20f, 0.15f, 0.11f)), 34.f);

	// The bust at the foot of the stairs on a marble column: the column cracked through and a
	// corner of its cap gone, the bust knocked a hand's width off true and never set straight.
	const FVector Column(FlightEastX() + 80.f, CentralSouthY() + 120.f, GroundZ);
	Build.Box(Column + FVector(0.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(46.f, 46.f, 12.f), MatMarble);
	Build.Box(Column + FVector(0.f, 0.f, 58.f), FRotator::ZeroRotator, FVector(34.f, 34.f, 92.f), MatMarble);
	Build.Box(Column + FVector(0.f, 0.f, 108.f), FRotator::ZeroRotator, FVector(44.f, 44.f, 8.f), MatMarble);
	Build.Box(Column + FVector(17.f, 17.f, 110.f), FRotator(0.f, 45.f, 0.f), FVector(12.f, 12.f, 9.f), MatVoid, false);
	Build.Crack(Column + FVector(24.f, 0.f, 60.f), FRotator(0.f, 180.f, 0.f), FVector2D(40.f, 110.f), 1.f, 22.f);
	if (UStaticMeshComponent* Bust = Build.PropSeated(RoomProps::Bust, Column + FVector(3.f, -4.f, 112.f), FRotator(0.f, 170.f, 2.f), 58.f))
	{
		FRoomShapes::TintSlots(Bust, FLinearColor(0.46f, 0.45f, 0.43f));
	}
	for (int32 i = 0; i < 6; ++i)
	{
		Build.Box(Column + FVector(Random.FRandRange(20.f, 50.f), Random.FRandRange(10.f, 40.f), 1.5f), FRotator(Random.FRandRange(-20.f, 20.f), Random.FRandRange(0.f, 360.f), 0.f),
			FVector(Random.FRandRange(2.f, 6.f), Random.FRandRange(2.f, 5.f), 2.f), MatMarble, false);
	}

	// The statue in the north alcove, turned to look out into the hall, on a low plinth. Bronze gone
	// black, and the one thing down here the height of a man that is not one.
	const FVector Alcove((FlightEastX() + LandingEdgeX()) * 0.5f - 20.f, (NorthInnerY() + CentralNorthY()) * 0.5f, GroundZ);
	Build.Box(Alcove + FVector(0.f, 0.f, 9.f), FRotator::ZeroRotator, FVector(90.f, 90.f, 18.f), MatMarble);
	if (UStaticMeshComponent* Statue = Build.PropSeated(RoomProps::Statue, Alcove + FVector(0.f, 0.f, 18.f), FRotator(0.f, -90.f, 0.f), 176.f))
	{
		FRoomShapes::TintSlots(Statue, FLinearColor(0.42f, 0.40f, 0.36f));
	}
	Build.Crack(Alcove + FVector(40.f, 0.f, 120.f), FRotator(0.f, 180.f, 0.f), FVector2D(60.f, 90.f), 0.8f, 24.f);

	// On the gallery by the archway: a side table with a vase on it, and on the landing, a second
	// one under the portrait, and the brass vase gone over on its side on the boards.
	if (UStaticMeshComponent* Table = Build.PropSeated(RoomProps::SideTable, FVector(EastX() - 32.f, Setup.CenterY - 190.f, 0.f), FRotator(0.f, 12.f, 0.f), 0.f))
	{
		FRoomShapes::TintSlots(Table, FLinearColor(0.55f, 0.52f, 0.48f));
	}
	if (UStaticMeshComponent* Vase = Build.PropSeated(RoomProps::BrassVase, FVector(EastX() - 32.f, Setup.CenterY - 190.f, 76.f), FRotator::ZeroRotator, 44.f, false))
	{
		FRoomShapes::TintSlots(Vase, FLinearColor(0.40f, 0.34f, 0.26f));
	}
	if (UStaticMeshComponent* Table = Build.PropSeated(RoomProps::SideTable, FVector(WestX() + 30.f, PortraitCenterY(), LandingZ), FRotator(0.f, -8.f, 0.f), 0.f))
	{
		FRoomShapes::TintSlots(Table, FLinearColor(0.50f, 0.47f, 0.43f));
	}
	if (UStaticMeshComponent* Vase = Build.PropSeated(RoomProps::CeramicVase, FVector(WestX() + 30.f, PortraitCenterY(), LandingZ + 76.f), FRotator(0.f, 130.f, 0.f), 34.f, false))
	{
		FRoomShapes::TintSlots(Vase, FLinearColor(0.44f, 0.42f, 0.40f));
	}
	if (UStaticMeshComponent* Fallen = Build.PropSeated(RoomProps::BrassVase, FVector(WestX() + 70.f, Setup.CenterY + WindowWidth * 0.5f + 90.f, LandingZ + 3.f), FRotator(0.f, 60.f, 90.f), 50.f, false))
	{
		FRoomShapes::TintSlots(Fallen, FLinearColor(0.40f, 0.34f, 0.26f));
	}

	// The picture that hung on the landing's south wall: face down on the boards, its glass out of
	// it. Face down is roll 90 for this mesh (the bedroom's 09-23 note), and slots 0 and 2 ship as
	// the engine checkerboard.
	const FVector Fallen(WestX() + 110.f, SouthInnerY() - 60.f, LandingZ + 3.f);
	if (UStaticMeshComponent* Frame = Build.PropSeated(RoomProps::PictureFrame, Fallen, FRotator(0.f, 28.f, 90.f), 54.f, false))
	{
		Frame->SetMaterial(0, MatVoid);
		Frame->SetMaterial(2, MatOak);
	}
	for (int32 i = 0; i < 9; ++i)
	{
		Build.Box(Fallen + FVector(Random.FRandRange(-45.f, 45.f), Random.FRandRange(-40.f, 40.f), 1.f), FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
			FVector(Random.FRandRange(4.f, 12.f), Random.FRandRange(3.f, 9.f), 0.6f), MatGlass, false);
	}
	// And where it hung, a nail and the clean patch.
	Build.Mark(WallPoint(EWall::South, WestX() + 110.f, LandingZ + 190.f, 0.8f), FRotator(0.f, 0.f, -90.f), FVector2D(52.f, 70.f), MatWallpaper);
	Build.Sph(WallPoint(EWall::South, WestX() + 110.f, LandingZ + 236.f, 1.f), 1.8f, MatIron);
}

void AGrandStaircaseActor::BuildDebris(FRoomBuilder& Build)
{
	// Rubble along the foot of every wall on every floor, heaps under the places the plaster came
	// away, and papers. No collision on any of it, as upstairs: clutter the boot does not catch.
	struct FStrip { float X0, X1, Y0, Y1, Z; };
	const FStrip Strips[] = {
		{ FlightEastX() + 20.f, EastX() - 20.f, NorthY() + 4.f, NorthY() + 30.f, GroundZ },
		{ FlightEastX() + 20.f, EastX() - 20.f, SouthY() - 30.f, SouthY() - 4.f, GroundZ },
		{ EastX() - 30.f, EastX() - 4.f, NorthY() + 40.f, SouthY() - 40.f, GroundZ },
		{ FlightEastX(), EastX() - 20.f, NorthY() + 4.f, NorthY() + 26.f, 3.f },
		{ FlightEastX(), EastX() - 20.f, SouthY() - 26.f, SouthY() - 4.f, 3.f },
		{ WestX() + 4.f, WestX() + 30.f, NorthY() + 20.f, SouthY() - 20.f, LandingZ + 3.f },
	};
	for (const FStrip& S : Strips)
	{
		for (int32 i = 0; i < 20; ++i)
		{
			const FVector2D Spot(Random.FRandRange(S.X0, S.X1), Random.FRandRange(S.Y0, S.Y1));
			const float Size = Random.FRandRange(1.5f, 7.f);
			if (FMath::Abs(Spot.Y - Setup.CenterY) < 70.f && Spot.X > EastX() - 40.f)
			{
				continue; // not in a doorway
			}
			Build.Box(FVector(Spot.X, Spot.Y, S.Z + Size * 0.35f), FRotator(Random.FRandRange(-30.f, 30.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-30.f, 30.f)),
				FVector(Size * Random.FRandRange(0.8f, 1.8f), Size * Random.FRandRange(0.8f, 1.5f), Size * 0.6f), MatRubble, false);
		}
	}

	// Heaps under a fall of plaster on the landing and in the hall: bigger lumps, and lath.
	for (const FVector& Heap : { FVector(WestX() + 50.f, NorthY() + 60.f, LandingZ + 3.f), FVector(EastX() - 60.f, SouthY() - 70.f, GroundZ), FVector(FlightEastX() + 60.f, NorthY() + 60.f, GroundZ) })
	{
		for (int32 i = 0; i < 16; ++i)
		{
			const float R = Random.FRandRange(0.f, 44.f);
			const float A = Random.FRandRange(0.f, 2.f * PI);
			const float Size = FMath::Lerp(10.f, 2.f, R / 44.f) * Random.FRandRange(0.7f, 1.3f);
			Build.Box(Heap + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Size * 0.3f), FRotator(Random.FRandRange(-25.f, 25.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-25.f, 25.f)),
				FVector(Size * 1.4f, Size, Size * 0.5f), MatRubble, false);
		}
		for (int32 i = 0; i < 3; ++i)
		{
			Build.Box(Heap + FVector(Random.FRandRange(-30.f, 30.f), Random.FRandRange(-30.f, 30.f), 2.f), FRotator(Random.FRandRange(-5.f, 5.f), Random.FRandRange(0.f, 180.f), 0.f),
				FVector(Random.FRandRange(40.f, 80.f), 3.f, 0.8f), MatOakDark, false);
		}
	}

	// Papers: letters and pages on the hall floor, blown along the wall from the door, and a few on
	// the stairs and the landing.
	for (int32 i = 0; i < 22; ++i)
	{
		FVector Spot;
		if (i < 12)
		{
			Spot = FVector(Random.FRandRange(FlightEastX() + 30.f, EastX() - 40.f), Setup.CenterY + Random.FRandRange(-440.f, 440.f), GroundZ + 0.6f);
		}
		else if (i < 17)
		{
			Spot = FVector(Random.FRandRange(WestX() + 30.f, LandingEdgeX() - 20.f), Setup.CenterY + Random.FRandRange(-440.f, 440.f), LandingZ + 3.6f);
		}
		else
		{
			const int32 Step = Random.RandRange(1, RisersPerFlight - 2);
			Spot = FVector(FlightEastX() - (Step + 0.5f) * Going, Setup.CenterY + Random.FRandRange(-100.f, 100.f), GroundZ + (Step + 1) * Rise + 1.f);
		}
		Build.Mark(Spot, FRotator(Random.FRandRange(-2.f, 2.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-2.f, 2.f)),
			FVector2D(Random.FRandRange(19.f, 23.f), Random.FRandRange(26.f, 31.f)), Random.FRand() < 0.5f ? MatPaper.Get() : MatPaperDamp.Get());
	}

	// The balusters that snapped are on the hall floor under the flights.
	for (int32 i = 0; i < 4; ++i)
	{
		Build.Cyl(FVector(Random.FRandRange(LandingEdgeX() + 30.f, FlightEastX() - 20.f), NorthInnerY() + Random.FRandRange(14.f, 60.f), GroundZ + 1.8f),
			FRotator(90.f, Random.FRandRange(0.f, 180.f), 0.f), FVector(3.4f, 3.4f, Random.FRandRange(30.f, 50.f)), MatOak, false);
	}
}

void AGrandStaircaseActor::BuildFigure(FRoomBuilder& /*Build*/)
{
	// A man on the stairs, halfway up the central flight, climbing towards the window. He is not
	// there. He is there only while the sky is lit, and only some of the times it is, and only
	// from far enough away that what he is cannot be made out (see Tick) — a shape against the
	// glass going up the stairs, which is what the end of the story is.
	const int32 Step = 4;
	const FVector Foot(FlightEastX() - Going * (Step + 0.5f), Setup.CenterY - 20.f, GroundZ + (Step + 1) * Rise);
	Figure = NewObject<USceneComponent>(this, TEXT("Figure"));
	Figure->SetMobility(EComponentMobility::Movable);
	Figure->AttachToComponent(HallRoot, FAttachmentTransformRules::KeepRelativeTransform);
	// Facing up the flight, which is -X: his back is to anybody who can see him.
	Figure->SetRelativeLocationAndRotation(Foot, FRotator(0.f, 180.f, 0.f));
	Figure->RegisterComponent();
	AddInstanceComponent(Figure);

	FRoomBuilder Shape(this, Figure);
	// Local +X is up the stairs. One foot on this tread, the other on the next, a coat to the knee.
	Shape.Cyl(FVector(-4.f, -9.f, 26.f), FRotator(-8.f, 0.f, 0.f), FVector(13.f, 13.f, 56.f), MatShadow, false);
	Shape.Cyl(FVector(16.f, 9.f, 32.f + Rise * 0.5f), FRotator(16.f, 0.f, 0.f), FVector(13.f, 13.f, 50.f), MatShadow, false);
	Shape.Add(FRoomShapes::Cone(), FVector(4.f, 0.f, 88.f), FRotator(180.f, 0.f, 0.f), FVector(54.f, 44.f, 70.f), MatShadow, false);
	Shape.Cyl(FVector(4.f, 0.f, 128.f), FRotator(8.f, 0.f, 0.f), FVector(30.f, 46.f, 52.f), MatShadow, false);
	Shape.Sph(FVector(6.f, 0.f, 150.f), 38.f, MatShadow);
	Shape.Cyl(FVector(8.f, 0.f, 160.f), FRotator::ZeroRotator, FVector(11.f, 11.f, 14.f), MatShadow, false);
	Shape.Sph(FVector(10.f, 0.f, 175.f), 22.f, MatShadow);
	// One hand on the rail.
	Shape.Cyl(FVector(8.f, -24.f, 124.f), FRotator(10.f, 0.f, 18.f), FVector(8.f, 8.f, 56.f), MatShadow, false);
	Shape.Cyl(FVector(4.f, 24.f, 118.f), FRotator(-6.f, 0.f, -6.f), FVector(8.f, 8.f, 58.f), MatShadow, false);

	Figure->SetVisibility(false, /*bPropagateToChildren*/ true);
}

void AGrandStaircaseActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ElapsedTime += DeltaTime;

	const float Flash = LeadStorm ? LeadStorm->GetFlashAlpha() : 0.f;
	const float Gust = LeadStorm ? LeadStorm->GetWindGust() : 0.f;

	// The glass: dim between strikes — the overcast sky behind it is a faint grey, and coloured
	// glass lets through a fraction of that — and blazing for the instant the sky is lit.
	if (StainedGlass)
	{
		StainedGlass->SetScalarParameterValue(TEXT("Intensity"), 0.30f + Flash * 5.5f);
	}

	// The chandelier: a long pendulum, so a slow one, pushed by the draught through the broken
	// light. Two swings at periods that never line up, so it never quite repeats.
	if (ChandelierPivot)
	{
		const float Amp = FMath::Lerp(0.25f, 1.4f, Gust);
		const float Pitch = Amp * FMath::Sin(ElapsedTime * 2.f * PI / 4.3f);
		const float Roll = Amp * 0.6f * FMath::Sin(ElapsedTime * 2.f * PI / 5.9f + 1.3f);
		const float Turn = 6.f * FMath::Sin(ElapsedTime * 2.f * PI / 23.f);
		ChandelierPivot->SetRelativeRotation(FRotator(Pitch, Turn, Roll));
	}

	if (DustMotes)
	{
		DustMotes->SetWindStrength(Gust);
	}

	if (!Figure || !LeadStorm)
	{
		return;
	}

	// The man on the stairs: decided once per strike, one strike in four, and only if the detective
	// is in the hall and a long way from him — on the gallery, in the archway, or by the front door.
	const int32 Strike = LeadStorm->GetStrikeCount();
	if (Strike != FigureStrike)
	{
		FigureStrike = Strike;
		bool bFar = false;
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				const FVector Local = GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
				const bool bInHall = Local.X < EastX() + 150.f && Local.X > WestX() && Local.Y > NorthY() && Local.Y < SouthY();
				bFar = bInHall && FVector::Dist(Local, Figure->GetRelativeLocation()) > 650.f;
			}
		}
		bFigureThisStrike = bFar && (Strike % 4 == 1);
	}
	const bool bShow = bFigureThisStrike && Flash > 0.08f;
	if (Figure->IsVisible() != bShow)
	{
		Figure->SetVisibility(bShow, /*bPropagateToChildren*/ true);
	}
}

// ---------------------------------------------------------------------------------------------

void AGrandStaircaseActor::SpawnDoors()
{
	struct FDoorSpec { FVector Hinge; float Yaw; float Width; float Height; float Ajar; FLinearColor Tint; bool bSix; const TCHAR* Prompt; };
	const float Half = 50.f;
	const FDoorSpec Specs[] = {
		// The front door. Local +X is the hall side: yaw 180 on the east wall.
		{ FVector(EastX() + 2.6f, Setup.CenterY + 60.f - 2.f, GroundZ), 180.f, 116.f, 248.f, 0.f, FLinearColor(0.20f, 0.20f, 0.20f), true,
			TEXT("The front door. It does not so much as rattle — the boards across it are nailed deep into the frame.") },
		{ FVector(-1560.f + Half - 2.f, NorthY() - 2.6f, GroundZ), 90.f, 96.f, 210.f, 0.f, FLinearColor(0.26f, 0.27f, 0.28f), true,
			TEXT("Locked. Through the keyhole, the long shape of a table under a sheet, and a place laid at the end of it.") },
		{ FVector(-1560.f - Half + 2.f, SouthY() + 2.6f, GroundZ), -90.f, 96.f, 210.f, 14.f, FLinearColor(0.22f, 0.23f, 0.24f), false,
			TEXT("It gives a hand's width and stops. The room beyond smells of cold ash — and of something electrical, faintly, like a set left on.") },
	};

	int32 Seed = 5101;
	for (const FDoorSpec& Spec : Specs)
	{
		FHallDoorSetup DoorSetup;
		DoorSetup.Width = Spec.Width;
		DoorSetup.Height = Spec.Height;
		DoorSetup.AjarYaw = Spec.Ajar;
		DoorSetup.Seed = Seed++;
		DoorSetup.WoodTint = Spec.Tint;
		DoorSetup.bSixPanel = Spec.bSix;
		DoorSetup.Prompt = Spec.Prompt;

		const FTransform Transform(FRotator(0.f, Spec.Yaw, 0.f), GetActorTransform().TransformPosition(Spec.Hinge));
		if (AHallDoorActor* Door = GetWorld()->SpawnActorDeferred<AHallDoorActor>(AHallDoorActor::StaticClass(), Transform, this))
		{
			Door->Configure(DoorSetup);
			Door->FinishSpawning(Transform);
			Doors.Add(Door);
		}
	}
}

void AGrandStaircaseActor::SpawnWindow()
{
	// A follower of the bedroom's storm, as the corridor's is, but looking out of the other side of
	// the house: its own sky and trees and rain, and its own flash, since the lead's directional
	// light cannot reach in through a west window. The glazing is ours (BuildStainedGlass); the
	// storm window brings everything outside it, and the torn curtains.
	const FTransform Transform(FRotator(0.f, 180.f, 0.f),
		GetActorTransform().TransformPosition(FVector(WestX() - Setup.WallThickness * 0.5f, Setup.CenterY, LandingZ)));
	Window = GetWorld()->SpawnActorDeferred<AStormWindowActor>(AStormWindowActor::StaticClass(), Transform, this);
	if (Window)
	{
		FStormWindowSetup WindowSetup;
		WindowSetup.OpeningWidth = WindowWidth;
		WindowSetup.SillHeight = WindowSill;
		WindowSetup.TopHeight = WindowSill + WindowHeight;
		WindowSetup.WallThickness = Setup.WallThickness;
		WindowSetup.bGlazed = false;
		WindowSetup.bOwnView = true;
		// Leaded coloured glass lets through a fraction of what a clear pane does.
		WindowSetup.PortalScale = 0.55f;
		Window->Configure(WindowSetup);
		Window->SetLead(LeadStorm);
		Window->FinishSpawning(Transform);
	}
}

AClueActor* AGrandStaircaseActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description)
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

void AGrandStaircaseActor::BuildClues()
{
	auto HitVolume = [&](FRoomBuilder& B, const FVector& At, const FVector& Size)
	{
		if (UStaticMeshComponent* Hit = B.Box(At, FRotator::ZeroRotator, Size, MatVoid))
		{
			Hit->SetHiddenInGame(true);
			Hit->SetCastShadow(false);
			Hit->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	};

	// The front door, boarded from the inside. These are the boards that used to be across the
	// stairs upstairs, and the line goes with them: whoever nailed them was on this side.
	if (AClueActor* Boards = SpawnClue(FVector(EastX() - 5.f, Setup.CenterY, GroundZ + 125.f), FRotator::ZeroRotator,
		TEXT("Examine the boards"),
		TEXT("The front door, boarded over from the inside. The nails were driven from this side. Whoever did it meant to stay in.")))
	{
		FRoomBuilder B(Boards, Boards->GetRootScene());
		// In the plane of the door only — roll about the wall's normal, which is X here — or a
		// plank stands off the door at one end (the corridor's 09-26 note on the old boards).
		struct FPlank { float Z; float Roll; float Length; float Height; };
		const FPlank Planks[] = {
			{ -95.f, 5.f, 168.f, 17.f }, { -60.f, -7.f, 160.f, 15.f }, { -18.f, 3.f, 172.f, 18.f }, { 18.f, 16.f, 176.f, 15.f },
			{ 52.f, -4.f, 164.f, 16.f }, { 88.f, -13.f, 170.f, 14.f }, { 122.f, 6.f, 158.f, 17.f },
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Planks); ++i)
		{
			const FPlank& P = Planks[i];
			const float X = -1.8f - (i % 2) * 2.2f;
			B.Box(FVector(X, (i % 3 - 1) * 4.f, P.Z), FRotator(0.f, 0.f, P.Roll), FVector(2.2f, P.Length, P.Height), (i % 3 == 0) ? MatOakDark.Get() : MatOak.Get(), false);
			const float Reach = P.Length * 0.5f - 9.f;
			const float R = FMath::DegreesToRadians(P.Roll);
			for (const float Side : { -1.f, 1.f })
			{
				B.Sph(FVector(X - 1.4f, (i % 3 - 1) * 4.f + Side * Reach * FMath::Cos(R), P.Z + Side * Reach * FMath::Sin(R)), 1.4f, MatIron);
			}
		}
		HitVolume(B, FVector(-4.f, 0.f, 0.f), FVector(2.f, 150.f, 250.f));
	}

	// The cases, by the door, ready to go. vintage_suitcase is two cases standing side by side along
	// its local X, with their faces on Y; yaw 90 stands them along the east wall.
	if (AClueActor* Cases = SpawnClue(FVector(EastX() - 22.f, Setup.CenterY + 170.f, GroundZ), FRotator::ZeroRotator,
		TEXT("Examine the cases"),
		TEXT("Two cases, packed, strapped and set down by the door, ready to go. The dust on the handles says nobody ever came down for them.")))
	{
		FRoomBuilder B(Cases, Cases->GetRootScene());
		if (UStaticMeshComponent* Mesh = B.PropSeated(RoomProps::Suitcases, FVector::ZeroVector, FRotator(0.f, 94.f, 0.f), 0.f))
		{
			FRoomShapes::TintSlots(Mesh, FLinearColor(0.52f, 0.48f, 0.42f));
		}
		HitVolume(B, FVector(0.f, 0.f, 28.f), FVector(26.f, 160.f, 56.f));
	}

	// The umbrella stand, on the other side of the door: iron, rusted through at the foot, three
	// black umbrellas and one small yellow one.
	if (AClueActor* Stand = SpawnClue(FVector(EastX() - 26.f, Setup.CenterY - 130.f, GroundZ), FRotator::ZeroRotator,
		TEXT("Examine the umbrellas"),
		TEXT("Three black umbrellas, and a small yellow one with a duck's head for a handle. All of them bone dry, on a night like this.")))
	{
		FRoomBuilder B(Stand, Stand->GetRootScene());
		// The stand: a drip tray on the floor, six uprights, and two open hoops — hoops, not discs,
		// or the umbrellas stand on a lid instead of in a stand.
		B.Cyl(FVector(0.f, 0.f, 1.5f), FRotator::ZeroRotator, FVector(34.f, 34.f, 3.f), MatIron, false);
		auto Hoop = [&](float Z, float Radius, float Thick)
		{
			const int32 Segments = 18;
			for (int32 i = 0; i < Segments; ++i)
			{
				const float A0 = 2.f * PI * i / Segments;
				const float A1 = 2.f * PI * (i + 1) / Segments;
				const FVector P0(FMath::Cos(A0) * Radius, FMath::Sin(A0) * Radius, Z);
				const FVector P1(FMath::Cos(A1) * Radius, FMath::Sin(A1) * Radius, Z);
				B.Cyl((P0 + P1) * 0.5f, FRotationMatrix::MakeFromZ((P1 - P0).GetSafeNormal()).Rotator(), FVector(Thick, Thick, (P1 - P0).Size() + 0.3f), MatIron, false);
			}
		};
		Hoop(3.4f, 16.6f, 1.2f);
		Hoop(30.f, 14.f, 1.3f);
		Hoop(58.f, 14.f, 1.6f);
		for (int32 i = 0; i < 6; ++i)
		{
			const float A = i * PI / 3.f;
			B.Cyl(FVector(FMath::Cos(A) * 14.f, FMath::Sin(A) * 14.f, 30.f), FRotator::ZeroRotator, FVector(1.4f, 1.4f, 58.f), MatIron, false);
			B.Sph(FVector(FMath::Cos(A) * 14.f, FMath::Sin(A) * 14.f, 59.5f), 2.4f, MatIron);
		}

		// The umbrellas, furled, each standing on its ferrule in the tray and leaning out against the
		// top hoop. A furled umbrella is a spindle: thin at the tip, swelling where the folds of the
		// cloth are bunched a third of the way up, tapering into the shaft, strapped round the
		// middle; then the bare shaft and a bent handle.
		UMaterialInterface* Black = B.Surface(RoomSurfaces::Drapery, FLinearColor(0.034f, 0.030f, 0.032f), 0.7f);
		UMaterialInterface* Yellow = B.Surface(RoomSurfaces::Drapery, FLinearColor(0.86f, 0.40f, 0.05f));
		UMaterialInterface* DuckYellow = B.Flat(FLinearColor(0.30f, 0.20f, 0.03f), 0.5f);
		struct FBrolly { float Azimuth; float Length; bool bChild; };
		const FBrolly Brollies[] = { { 0.4f, 92.f, false }, { 2.3f, 88.f, false }, { 4.2f, 95.f, false }, { 5.4f, 64.f, true } };
		for (const FBrolly& U : Brollies)
		{
			const FVector Out(FMath::Cos(U.Azimuth), FMath::Sin(U.Azimuth), 0.f);
			const FVector Tip = Out * 4.f + FVector(0.f, 0.f, 3.4f);
			// Leaning so the shaft rests against the inside of the top hoop.
			const FVector Axis = (Out * 13.f + FVector(0.f, 0.f, 58.f) - Tip).GetSafeNormal();
			const FRotator AlongAxis = FRotationMatrix::MakeFromZ(Axis).Rotator();
			auto P = [&](float S) { return Tip + Axis * S; };
			const float L = U.Length;
			const float Scale = U.bChild ? 0.72f : 1.f;
			UMaterialInterface* Canopy = U.bChild ? Yellow : Black;

			// Ferrule.
			B.Cyl(P(2.f), AlongAxis, FVector(1.f, 1.f, 4.f), MatIron, false);
			// The canopy, in stacked sections of changing girth, each sleeved a little into the next.
			const float Radii[] = { 0.9f, 1.8f, 2.6f, 3.0f, 2.9f, 2.4f, 1.7f, 1.1f };
			const int32 Sections = UE_ARRAY_COUNT(Radii);
			const float CanopyStart = 4.f;
			const float CanopyEnd = L * 0.7f;
			const float Section = (CanopyEnd - CanopyStart) / Sections;
			for (int32 i = 0; i < Sections; ++i)
			{
				const float R = Radii[i] * Scale;
				B.Cyl(P(CanopyStart + Section * (i + 0.5f)), AlongAxis, FVector(R * 2.f, R * 1.8f, Section + 1.2f), Canopy, false);
			}
			// The strap and its button, round the fattest part.
			const float StrapAt = CanopyStart + Section * 3.4f;
			B.Cyl(P(StrapAt), AlongAxis, FVector(3.2f * Scale * 2.f, 3.2f * Scale * 2.f, 1.6f), Canopy, false);
			B.Sph(P(StrapAt) + Out * 3.2f * Scale, 1.1f, MatBrass);
			// The bare shaft above the canopy.
			const float ShaftTop = L - 3.f;
			B.Cyl(P((CanopyEnd + ShaftTop) * 0.5f), AlongAxis, FVector(1.1f, 1.1f, ShaftTop - CanopyEnd + 1.f), MatIron, false);

			if (U.bChild)
			{
				// The duck: a round head on the end of the shaft and a bill, which at this size is all
				// a duck is, and a pair of black eyes.
				const FVector Head = P(ShaftTop + 2.4f);
				B.Sph(Head, 5.f, DuckYellow);
				B.Add(FRoomShapes::Cone(), Head + Out * 3.6f, FRotationMatrix::MakeFromZ(Out).Rotator(), FVector(2.2f, 1.4f, 3.2f), MatBrass, false);
				const FVector Side(-Out.Y, Out.X, 0.f);
				for (const float E : { -1.f, 1.f })
				{
					B.Sph(Head + Out * 1.8f + Side * E * 1.7f + FVector(0.f, 0.f, 1.2f), 0.8f, MatShadow);
				}
				continue;
			}
			// A crook: the shaft carried up and bent over outwards in a half circle.
			const float Radius = 4.2f;
			const FVector OutPerp = (Out - Axis * FVector::DotProduct(Out, Axis)).GetSafeNormal();
			const FVector Centre = P(ShaftTop) + OutPerp * Radius;
			FVector Previous = P(ShaftTop);
			for (int32 i = 1; i <= 9; ++i)
			{
				const float Theta = PI * i / 9.f;
				const FVector Next = Centre + (-OutPerp * FMath::Cos(Theta) + Axis * FMath::Sin(Theta)) * Radius;
				B.Cyl((Previous + Next) * 0.5f, FRotationMatrix::MakeFromZ((Next - Previous).GetSafeNormal()).Rotator(), FVector(2.f, 2.f, (Next - Previous).Size() + 0.3f), MatOakDark, false);
				B.Sph(Next, 2.1f, MatOakDark);
				Previous = Next;
			}
		}
		HitVolume(B, FVector(0.f, 0.f, 55.f), FVector(36.f, 36.f, 110.f));
	}

	// The family, on the landing wall beside the window: gone brown under its varnish, and the
	// man's face scraped off it. The frame is the gilt one; its canvas is darkened past reading.
	const float PortraitY = PortraitCenterY();
	if (AClueActor* Portrait = SpawnClue(FVector(WestX() + 2.f, PortraitY, LandingZ + 190.f), FRotator::ZeroRotator,
		TEXT("Examine the portrait"),
		TEXT("A street by a canal, gone brown under the varnish. Three small figures at the water's edge — a man, a woman, a little girl. The man's face has been scraped away to the canvas.")))
	{
		FRoomBuilder B(Portrait, Portrait->GetRootScene());
		// fancy_picture_frame_02 is 66 x 9 x 77 with its back at local Y = 0 and its face towards
		// +Y; yaw -90 turns +Y to +X, out of the west wall.
		if (UStaticMeshComponent* Frame = B.Prop(RoomProps::GiltFrame, FVector(0.f, 0.f, 0.f), FRotator(-1.5f, -90.f, 0.f), 118.f, false))
		{
			FRoomShapes::TintSlots(Frame, FLinearColor(0.45f, 0.38f, 0.28f), 0);
			FRoomShapes::TintSlots(Frame, FLinearColor(0.16f, 0.12f, 0.08f), 1);
		}
		// The scraped place, down among the figures by the water: small, pale and gouged.
		B.Stain(RoomSurfaces::Substrate, FVector(12.f, 4.f, -18.f), FRotator(0.f, 180.f, 0.f), FVector2D(6.f, 8.f), FLinearColor(0.34f, 0.30f, 0.24f), 0.95f, 0.8f);
		B.Crack(FVector(12.f, 4.f, -18.f), FRotator(0.f, 180.f, 0.f), FVector2D(9.f, 11.f), 1.f, 46.f);
		B.Sph(FVector(1.f, 0.f, 72.f), 1.8f, MatIron);
		HitVolume(B, FVector(6.f, 0.f, 0.f), FVector(4.f, 100.f, 118.f));
	}

	// The long-case clock, face down in the south alcove. vintage_grandfather_clock_01 faces its
	// local +Y; roll 90 puts that face on the tiles, and yaw lays it along the alcove.
	const FVector ClockAt((FlightEastX() + LandingEdgeX()) * 0.5f, (SouthInnerY() + CentralSouthY()) * 0.5f, GroundZ);
	if (AClueActor* Clock = SpawnClue(ClockAt, FRotator::ZeroRotator,
		TEXT("Examine the clock"),
		TEXT("It came down on its face, and nobody stood it up again. The pendulum is under it, and the glass of the dial is in pieces on the tiles.")))
	{
		FRoomBuilder B(Clock, Clock->GetRootScene());
		if (UStaticMeshComponent* Mesh = B.PropSeated(RoomProps::LongcaseClock, FVector::ZeroVector, FRotator(0.f, -86.f, 90.f), 0.f))
		{
			FRoomShapes::TintSlots(Mesh, FLinearColor(0.50f, 0.45f, 0.40f), 0);
		}
		for (int32 i = 0; i < 12; ++i)
		{
			B.Box(FVector(Random.FRandRange(-120.f, 120.f), Random.FRandRange(-45.f, 45.f), 0.5f), FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
				FVector(Random.FRandRange(3.f, 10.f), Random.FRandRange(2.f, 7.f), 0.5f), MatGlass, false);
		}
		B.Cyl(FVector(-40.f, 46.f, 1.2f), FRotator(90.f, 10.f, 0.f), FVector(1.2f, 1.2f, 80.f), MatBrass, false);
		B.Cyl(FVector(-82.f, 52.f, 1.6f), FRotator::ZeroRotator, FVector(17.f, 17.f, 2.4f), MatBrass, false);
		HitVolume(B, FVector(0.f, 0.f, 22.f), FVector(230.f, 70.f, 44.f));
	}

	// A card on the central flight, trodden flat.
	const int32 CardStep = 6;
	if (AClueActor* Card = SpawnClue(FVector(FlightEastX() - Going * (CardStep + 0.5f), Setup.CenterY + 70.f, GroundZ + (CardStep + 1) * Rise + 1.2f),
		FRotator(0.f, 20.f, 0.f),
		TEXT("Examine the card"),
		TEXT("A birthday card, trodden into the stair. Inside, in crayon, a house with too many windows and three people holding hands in front of it. \"Me, Mummy and Daddy.\"")))
	{
		FRoomBuilder B(Card, Card->GetRootScene());
		B.Box(FVector(0.f, 0.f, 0.2f), FRotator::ZeroRotator, FVector(15.f, 21.f, 0.4f), MatPaper, false);
		B.Box(FVector(0.4f, 10.5f, 0.6f), FRotator(0.f, 0.f, 8.f), FVector(15.f, 21.f, 0.3f), MatPaperDamp, false);
		HitVolume(B, FVector(0.f, 4.f, 2.f), FVector(22.f, 34.f, 4.f));
	}
}
