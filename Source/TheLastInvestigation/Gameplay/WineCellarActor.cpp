#include "WineCellarActor.h"
#include "CellarActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

const float AWineCellarActor::PierXs[4] = { -360.f, -120.f, 120.f, 360.f };
const float AWineCellarActor::BayXs[3] = { -240.f, 0.f, 240.f };

namespace
{
	/**
	 * The bottlings in the racks: which bottle (and so which label), which glass, which capsule.
	 * A rack face holds two or three of them in bands, the way a cellar is binned: by the wine.
	 */
	struct FWine
	{
		int32 Mesh;
		int32 Glass;
		int32 Capsule;
	};
	const FWine Wines[] = {
		{ 0, 0, 0 },   // Chateau Valmont 1947: green glass, red foil
		{ 1, 0, 1 },   // Domaine des Brumes 1952, a Burgundy: black wax
		{ 2, 0, 2 },   // Clos Saint-Aubin 1959: gold foil
		{ 3, 1, 3 },   // Maison Lavergne 1961: brown glass, burgundy wax
		{ 0, 0, 4 },   // the Valmont again, a later bottling under lead
		{ 1, 1, 2 },   // a Burgundy in brown glass, gold foil
	};
	constexpr int32 WineCount = UE_ARRAY_COUNT(Wines);

	/** Cells of the wine_paper atlas (make_cellar_art.py): column, row, and the used size in pixels. */
	struct FPaperCell
	{
		int32 Col;
		int32 Row;
		float W;
		float H;
	};
	FPaperCell PaperCell(int32 Cell)
	{
		switch (Cell)
		{
		case 12: return { 0, 3, 1024.f, 512.f };  // the bin chart, across two cells
		case 14: return { 2, 3, 512.f, 512.f };   // the engraving
		default:
			if (Cell >= 8 && Cell <= 11)
			{
				return { Cell - 8, 2, 366.f, 512.f };  // the ledger and the notebook's pages
			}
			return { Cell % 4, Cell / 4, 512.f, 410.f };  // a label
		}
	}

	/** Sets an instance used on a model's own UVs to one repeat, the nursery's rule. */
	void WineSheet(UMaterialInstanceDynamic* Mat)
	{
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}

	/** A rotation that lays local +Z along Dir, turned Roll degrees about it. */
	FRotator AlongAxis(const FVector& Dir, float Roll)
	{
		const FVector D = Dir.GetSafeNormal();
		return (FQuat(D, FMath::DegreesToRadians(Roll)) * FRotationMatrix::MakeFromZ(D).ToQuat()).Rotator();
	}

	/** A horizontal direction at Yaw degrees. */
	FVector Heading(float Yaw)
	{
		return FRotator(0.f, Yaw, 0.f).RotateVector(FVector(1.f, 0.f, 0.f));
	}

	/**
	 * How far a decal aimed straight down reaches along X and Y, turned by its roll. Aimed down,
	 * a decal lays its first size along Y and its second along X (09-27); the roll turns that
	 * rectangle, and this is the box round it.
	 */
	FVector2D WineFloorDecalHalfExtent(const FVector2D& Size, float Roll)
	{
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		return FVector2D(Cos * Size.Y + Sin * Size.X, Cos * Size.X + Sin * Size.Y) * 0.5f;
	}
}

AWineCellarActor::AWineCellarActor()
{
	PrimaryActorTick.bCanEverTick = false;

	CellarRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CellarRoot"));
	SetRootComponent(CellarRoot);
	CellarRoot->SetMobility(EComponentMobility::Movable);
}

void AWineCellarActor::BeginPlay()
{
	Super::BeginPlay();

	FRoomBuilder Build(this, CellarRoot);
	CacheMaterials(Build);
	BuildShell(Build);
	BuildRacks(Build);
	BuildEastEnd(Build);
	BuildTastingTable(Build);
	BuildAlcove(Build);
	BuildCorners(Build);
	BuildWallDamp(Build);
	BuildFloor(Build);
	BuildCobwebs(Build);
	BuildMist();
}

// ---------------------------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	if (!Mesh)
	{
		return;
	}
	for (const TPair<const TCHAR*, UMaterialInterface*>& Slot : Slots)
	{
		if (Slot.Value && Mesh->GetMaterialIndex(FName(Slot.Key)) != INDEX_NONE)
		{
			Mesh->SetMaterialByName(FName(Slot.Key), Slot.Value);
		}
	}
}

UStaticMeshComponent* AWineCellarActor::Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
	std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	Dress(Mesh, Slots);
	return Mesh;
}

UStaticMeshComponent* AWineCellarActor::Bottle(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation, int32 Wine)
{
	const FWine& W = Wines[FMath::Clamp(Wine, 0, WineCount - 1)];
	return Place(Build, Name, Location, Rotation,
		{ { TEXT("Glass"), MatBottleGlass[W.Glass] }, { TEXT("Capsule"), MatCapsules[W.Capsule] }, { TEXT("Label"), MatLabel } });
}

UStaticMeshComponent* AWineCellarActor::Glass(FRoomBuilder& Build, const FVector& Location, const FRotator& Rotation, bool bDregs)
{
	return Place(Build, RoomProps::WineGlass, Location, Rotation,
		{ { TEXT("Crystal"), MatCrystal }, { TEXT("Residue"), bDregs ? MatResidue.Get() : MatCrystal.Get() } });
}

void AWineCellarActor::Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw)
{
	if (UStaticMeshComponent* Part = Build.Box(Centre, FRotator(0.f, Yaw, 0.f), Size, MatVoid))
	{
		Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Part->SetCollisionResponseToAllChannels(ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Part->SetHiddenInGame(true);
		Part->SetCastShadow(false);
	}
}

UStaticMeshComponent* AWineCellarActor::Paper(FRoomBuilder& Build, int32 Cell, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H)
{
	// One instance per cell, made straight from the asset and never registered with the builder,
	// so Add() does not re-tile it to the plane's size (the nursery's drawings).
	TObjectPtr<UMaterialInstanceDynamic>* Found = PaperMats.Find(Cell);
	UMaterialInstanceDynamic* Mat = Found ? Found->Get() : nullptr;
	if (!Found)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_wine_paper.MI_wine_paper"));
		Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Mat)
		{
			const FPaperCell C = PaperCell(Cell);
			// Old paper in a cellar, at about a fifth of what the bake holds: under the lantern at arm's
			// length anything paler burns out to a blank sheet (the nursery's drawings did).
			Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.24f, 0.22f, 0.19f));
			Mat->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(C.W / 2048.f, C.H / 2048.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(C.Col * 0.25f, C.Row * 0.25f, 0.f, 1.f));
		}
		PaperMats.Add(Cell, Mat);
	}
	// Local Z out of the sheet, local X to the viewer's right, so local -Y is the top of the page.
	const FVector Right = FVector::CrossProduct(Normal, Up).GetSafeNormal();
	const FRotator Facing = FRotationMatrix::MakeFromZX(Normal, Right).Rotator();
	UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Facing, FVector(W, H, 1.f), Mat ? Mat : MatPages.Get(), false);
	if (Sheet)
	{
		Sheet->SetCastShadow(false);
	}
	return Sheet;
}

void AWineCellarActor::Brand(FRoomBuilder& Build, int32 Cell, const FVector& Centre, const FVector& Normal, float W)
{
	TObjectPtr<UMaterialInstanceDynamic>* Found = BrandMats.Find(Cell);
	UMaterialInstanceDynamic* Mat = Found ? Found->Get() : nullptr;
	if (!Found)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_wine_brands.MI_wine_brands"));
		Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.5f, 0.45f, 0.4f));
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(0.5f, 0.25f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor((Cell % 2) * 0.5f, (Cell / 2) * 0.25f, 0.f, 1.f));
		}
		BrandMats.Add(Cell, Mat);
	}
	if (!Mat)
	{
		return;
	}
	const FVector Right = FVector::CrossProduct(Normal, FVector::UpVector).GetSafeNormal();
	const FRotator Facing = FRotationMatrix::MakeFromZX(Normal, Right).Rotator();
	if (UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Facing, FVector(W, W * 0.5f, 1.f), Mat, false))
	{
		Sheet->SetCastShadow(false);
	}
}

void AWineCellarActor::Web(FRoomBuilder& Build, const FVector& Centre, const FRotator& Rotation, const FVector2D& Size)
{
	if (UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Rotation, FVector(Size.X, Size.Y, 1.f), MatWeb, false))
	{
		Sheet->SetCastShadow(false);
	}
}

AClueActor* AWineCellarActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AClueActor* Clue = GetWorld()->SpawnActor<AClueActor>(AClueActor::StaticClass(), GetActorTransform().TransformPosition(LocalLocation),
		GetActorRotation() + Rotation, SpawnParams);
	if (Clue)
	{
		Clues.Add(Clue);
	}
	return Clue;
}

FVector AWineCellarActor::AlcoveChair(float Side, float& OutYaw)
{
	// The chair faces its local +Y: the north one looks south across the table, the south one north.
	OutYaw = Side < 0.f ? -AlcoveChairTurn : 180.f + AlcoveChairTurn;
	return FVector(AlcoveX, Side * AlcoveChairY, 0.f);
}

void AWineCellarActor::KeepWallDecalsOff(const UPrimitiveComponent* Part)
{
	if (!Part || !Part->IsRegistered())
	{
		return;
	}
	// The room is translated from the cellar's frame and never turned, so its own box is the world
	// box moved by the actor's location. A wall decal is put two centimetres off the plaster and
	// reaches nine either way from there, so anything within a dozen of a wall is under its reach.
	const FBox Box = Part->Bounds.GetBox().ShiftBy(-GetActorLocation());
	const float Reach = 12.f;
	const FVector2D AlongX(Box.Min.X, Box.Max.X);
	const FVector2D AlongY(Box.Min.Y, Box.Max.Y);
	const FVector2D Up(Box.Min.Z, Box.Max.Z);
	if (Box.Min.Y < -HalfY + Reach)
	{
		WallKeepOuts.Add({ EWall::North, AlongX, Up });
	}
	if (Box.Max.Y > HalfY - Reach)
	{
		WallKeepOuts.Add({ EWall::South, AlongX, Up });
	}
	if (Box.Max.X > HalfX - Reach)
	{
		WallKeepOuts.Add({ EWall::East, AlongY, Up });
	}
	if (Box.Min.X < -HalfX + Reach)
	{
		WallKeepOuts.Add({ EWall::West, AlongY, Up });
	}
}

void AWineCellarActor::KeepWallDecalsOff(const AActor* Actor)
{
	if (!Actor)
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
	for (const UPrimitiveComponent* Part : Parts)
	{
		KeepWallDecalsOff(Part);
	}
}

bool AWineCellarActor::WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfAlong, float HalfUp) const
{
	// The doorway, with its stone surround (16 out from the opening either side, 18 over it): a
	// decal projects straight through, smears down the reveal and lies on the leaf as it swings.
	// A hand's width of margin, the cellar's (ACellarActor::DecalHitsDoorway).
	if (Wall == EWall::North)
	{
		const float DoorMargin = 15.f;
		if (FMath::Abs(U - DoorX) < DoorHalf + 16.f + HalfAlong + DoorMargin && Z - HalfUp < DoorHeight + 18.f + DoorMargin)
		{
			return true;
		}
	}
	// Against a piece of furniture or a frame a few centimetres are enough: a decal's edge is already
	// torn and faded by then.
	const float Margin = 5.f;
	for (const FWallKeepOut& Keep : WallKeepOuts)
	{
		if (Keep.Wall == Wall
			&& U + HalfAlong + Margin > Keep.U.X && U - HalfAlong - Margin < Keep.U.Y
			&& Z + HalfUp + Margin > Keep.Z.X && Z - HalfUp - Margin < Keep.Z.Y)
		{
			return true;
		}
	}
	return false;
}

bool AWineCellarActor::FloorDecalReachesDoor(const FVector2D& Point, const FVector2D& HalfExtent)
{
	const float Margin = 15.f;
	// The doorway itself: the sill and the reveal in the wall's thickness, and the foot of the stone
	// surround standing three centimetres proud of the wall either side. A floor decal is projected
	// from four above the flags and reaches nine either way, so it takes in the bottom of all three.
	const float SurroundHalf = DoorHalf + 16.f;
	const float SurroundFace = -HalfY + 3.f;
	if (FMath::Abs(Point.X - DoorX) < SurroundHalf + HalfExtent.X + Margin && Point.Y - HalfExtent.Y < SurroundFace + Margin)
	{
		return true;
	}
	// The leaf's sweep, as the cellar hangs it: hinged at the west jamb on the corridor's side, its
	// leaf along +X when shut, swinging south into the room — a quarter disc of the leaf's width on
	// the hinge's east side, pushed back past the hinge line by any opening beyond ninety degrees
	// (ARoomDressingActor::ReachesDoorSwing). The footprint's half-diagonal stands in for its shape.
	const FVector2D Hinge(DoorX - DoorHalf + ACellarActor::DoorHingeInset, -(Depth + WallThickness) * 0.5f + ACellarActor::DoorHingeProud);
	const float Leaf = DoorHalf * 2.f - ACellarActor::DoorLeafClearance;
	const float Overswing = Leaf * FMath::Sin(FMath::DegreesToRadians(FMath::Max(ACellarActor::DoorOpenYaw - 90.f, 0.f)));
	return FVector2D::Distance(Point, Hinge) < Leaf + HalfExtent.Size() + Margin
		&& Point.X + HalfExtent.X > Hinge.X - Overswing - Margin;
}

// ---------------------------------------------------------------------------------------------
// Materials. Every tint is worked back from the photograph's measured mean, as everywhere in the
// house: an old cellar lit by one flame has nothing in it much above a tenth.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::CacheMaterials(FRoomBuilder& Build)
{
	// castle_brick_01 is a warm grey-brown at linear (0.26, 0.20, 0.13): held at about 0.07, warm.
	MatWall = Build.Surface(RoomSurfaces::CellarBrick, FLinearColor(0.29f, 0.30f, 0.34f));
	// The corridor's face of the door wall is the corridor's brick, as on every other cellar door.
	MatCorridorBrick = Build.Surface(RoomSurfaces::Substrate, FLinearColor(0.150f, 0.130f, 0.112f));
	// Cold flags: large_floor_tiles_02 is a neutral grey at 0.19; kept a shade cooler than the walls.
	MatFlags = Build.Surface(RoomSurfaces::CellarFlags, FLinearColor(0.36f, 0.35f, 0.34f));
	MatStone = Build.Surface(RoomSurfaces::Stone, FLinearColor(0.40f, 0.42f, 0.46f));
	// The vault's and the arch's own UVs are in repeats, so each wants an instance tiled at one, a
	// hair off the tint so the builder's cache hands back one of its own (the hall's spandrels).
	MatStoneSheet = Build.Surface(RoomSurfaces::Stone, FLinearColor(0.40f, 0.42f, 0.461f));
	WineSheet(MatStoneSheet);
	// medieval_red_brick at (0.23, 0.13, 0.09) linear: a brick vault a shade darker than the walls,
	// and redder, since it is the one thing down here that was laid to be looked at.
	MatVaultSheet = Build.Surface(RoomSurfaces::VaultBrick, FLinearColor(0.28f, 0.34f, 0.42f));
	WineSheet(MatVaultSheet);
	MatBeam = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.30f, 0.30f, 0.30f));
	MatBoards = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.42f, 0.41f, 0.40f));
	// black_oak_veneer is a pale grey oak at 0.12; brought down and warmed to a near-black oak.
	MatOak = Build.Surface(RoomSurfaces::DarkOak, FLinearColor(0.22f, 0.17f, 0.13f), 1.8f);
	WineSheet(MatOak);
	// The racks the same oak with more dust on it: rougher, so the lantern's highlight spreads.
	// black_oak_veneer is shot as polished veneer: at its own roughness every slat of a rack took a
	// highlight from the lantern and the racks read as pale grey shelving. Matt, and darker still.
	MatRackOak = Build.Surface(RoomSurfaces::DarkOak, FLinearColor(0.10f, 0.075f, 0.06f), 2.6f);
	WineSheet(MatRackOak);
	// brown_leather is a saturated tan at (0.084, 0.028, 0.006): pulled to an old dark brown, and
	// rougher, because leather nobody has sat in for decades is dry and has lost its shine.
	MatLeather = Build.Surface(RoomSurfaces::Leather, FLinearColor(0.36f, 0.50f, 0.78f), 1.5f);
	WineSheet(MatLeather);
	MatLeatherBox = Build.Surface(RoomSurfaces::Leather, FLinearColor(0.50f, 0.70f, 1.10f), 1.4f);
	MatWood = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.45f, 0.42f, 0.40f));
	WineSheet(MatWood);
	// Crates are deal, the pale softwood, gone grey: twice the oak's value.
	MatDeal = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.80f, 0.74f, 0.66f));
	WineSheet(MatDeal);
	MatMarble = Build.Surface(RoomSurfaces::Marble, FLinearColor(0.24f, 0.27f, 0.31f), 1.3f);
	WineSheet(MatMarble);
	// Brass gone brown and iron gone to rust; green_metal_rust needs its corrected tint (the door).
	MatBrass = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	// Crystal: clear, but not clean. The decanter and the cabinet's doors carry more dust.
	// Dark tints and low opacity: lit by a lantern at arm's length, glass at the window panes' values
	// came out as milk-white porcelain. What says glass is that it is nearly not there, with a rim.
	MatCrystal = Build.Glass(FLinearColor(0.035f, 0.038f, 0.036f), 0.06f, 0.12f);
	MatCrystalDusty = Build.Glass(FLinearColor(0.06f, 0.06f, 0.055f), 0.14f, 0.32f);
	MatResidue = Build.Flat(FLinearColor(0.030f, 0.003f, 0.006f), 0.45f);
	MatWax = Build.Flat(FLinearColor(0.20f, 0.18f, 0.13f), 0.6f);
	MatWick = Build.Flat(FLinearColor(0.008f, 0.008f, 0.008f), 0.9f);
	MatCork = Build.Flat(FLinearColor(0.13f, 0.08f, 0.04f), 0.92f);
	MatWineStain = Build.Flat(FLinearColor(0.05f, 0.006f, 0.01f), 0.8f);
	MatFelt = Build.Flat(FLinearColor(0.012f, 0.024f, 0.016f), 0.95f);
	// A dial held near the plaster's value, near-black figures on it: contrast, not brightness.
	MatDial = Build.Flat(FLinearColor(0.15f, 0.14f, 0.11f), 0.8f);
	MatInk = Build.Flat(FLinearColor(0.010f, 0.009f, 0.008f), 0.6f);
	MatTag = Build.Flat(FLinearColor(0.15f, 0.13f, 0.10f), 0.9f);
	MatShadow = Build.Flat(FLinearColor(0.008f, 0.007f, 0.006f), 1.f);
	MatGilt = Build.Flat(FLinearColor(0.18f, 0.12f, 0.04f), 0.55f, 0.7f);
	MatPages = Build.Flat(FLinearColor(0.16f, 0.14f, 0.11f), 0.9f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	for (const FLinearColor& Cloth : { FLinearColor(0.060f, 0.012f, 0.008f), FLinearColor(0.012f, 0.030f, 0.016f), FLinearColor(0.040f, 0.020f, 0.010f),
		FLinearColor(0.010f, 0.009f, 0.009f), FLinearColor(0.090f, 0.060f, 0.035f) })
	{
		MatBooks.Add(Build.Flat(Cloth, 0.75f));
	}
	// The bottles: dark green and brown glass. Rough, because they are dusted over: what the lantern
	// finds on a racked bottle is a soft grey sheen, not a glint.
	MatBottleGlass.Add(Build.Flat(FLinearColor(0.004f, 0.012f, 0.006f), 0.52f));
	MatBottleGlass.Add(Build.Flat(FLinearColor(0.014f, 0.006f, 0.002f), 0.52f));
	// The capsules dulled and dusted over: no foil down here has caught the light in fifty years.
	MatCapsules.Add(Build.Flat(FLinearColor(0.030f, 0.002f, 0.002f), 0.62f, 0.3f));
	MatCapsules.Add(Build.Flat(FLinearColor(0.008f, 0.008f, 0.008f), 0.7f));
	MatCapsules.Add(Build.Flat(FLinearColor(0.075f, 0.050f, 0.018f), 0.7f, 0.5f));
	MatCapsules.Add(Build.Flat(FLinearColor(0.030f, 0.004f, 0.006f), 0.7f));
	MatCapsules.Add(Build.Flat(FLinearColor(0.035f, 0.035f, 0.032f), 0.75f, 0.4f));
	// The labels: the paper atlas on the bottles' own UVs.
	if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_wine_paper.MI_wine_paper")))
	{
		MatLabel = UMaterialInstanceDynamic::Create(Parent, this);
		MatLabel->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.26f, 0.24f, 0.21f));
		MatLabel->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
		WineSheet(MatLabel);
	}
	else
	{
		MatLabel = Build.Flat(FLinearColor(0.15f, 0.13f, 0.10f), 0.9f);
	}
}

// ---------------------------------------------------------------------------------------------
// The shell: walls, floor, the aisles' ceilings, the piers, the beams, the vault and the arch.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildShell(FRoomBuilder& Build)
{
	const float T = WallThickness;
	const float WH = Width * 0.5f;
	const float DH = Depth * 0.5f;

	// The floor stops at the middle of the door wall: past it is the corridor's floor (the bare
	// rooms' rule — a face shared with it would flicker).
	Build.Box(FVector(0.f, T * 0.25f, -5.f), FRotator::ZeroRotator, FVector(Width + T, Depth + T * 0.5f, 10.f), MatFlags);

	// The door wall, in two skins: stone brick inside, the corridor's brick outside. Only as high
	// as the aisles' ceiling — above that the corridor's own wall carries on (ACellarActor).
	const float DoorL = DoorX - DoorHalf;
	const float DoorR = DoorX + DoorHalf;
	for (const float Skin : { -1.f, 1.f })
	{
		UMaterialInterface* Mat = Skin > 0.f ? MatWall.Get() : MatCorridorBrick.Get();
		const float Y = -DH + Skin * T * 0.25f;
		const float LeftWidth = DoorL - (-WH - T * 0.5f);
		const float RightWidth = (WH + T * 0.5f) - DoorR;
		Build.Box(FVector(-WH - T * 0.5f + LeftWidth * 0.5f, Y, AisleCeiling * 0.5f), FRotator::ZeroRotator, FVector(LeftWidth, T * 0.5f, AisleCeiling), Mat);
		Build.Box(FVector(DoorR + RightWidth * 0.5f, Y, AisleCeiling * 0.5f), FRotator::ZeroRotator, FVector(RightWidth, T * 0.5f, AisleCeiling), Mat);
		Build.Box(FVector(DoorX, Y, (DoorHeight + AisleCeiling) * 0.5f), FRotator::ZeroRotator, FVector(DoorHalf * 2.f, T * 0.5f, AisleCeiling - DoorHeight), Mat);
	}
	// A dressed stone surround on both faces, the reveal lined in stone and a worn sill: this door
	// was the cellar's one door built to be seen.
	for (const float Side : { -1.f, 1.f })
	{
		const float FaceY = -DH + Side * (T * 0.5f + 1.5f);
		for (const float Jamb : { DoorL - 8.f, DoorR + 8.f })
		{
			Build.Box(FVector(Jamb, FaceY, (DoorHeight + 16.f) * 0.5f), FRotator(90.f, 0.f, 0.f), FVector(DoorHeight + 16.f, 3.f, 16.f), MatStone, false);
		}
		Build.Box(FVector(DoorX, FaceY, DoorHeight + 9.f), FRotator::ZeroRotator, FVector(DoorHalf * 2.f + 32.f, 3.f, 18.f), MatStone, false);
		Build.Box(FVector(DoorX, FaceY + Side * 0.6f, DoorHeight + 9.f), FRotator::ZeroRotator, FVector(22.f, 3.f, 22.f), MatStone, false);
	}
	for (const float Jamb : { -1.f, 1.f })
	{
		Build.Box(FVector(DoorX + Jamb * (DoorHalf - 3.f), -DH, DoorHeight * 0.5f), FRotator(90.f, 0.f, 0.f), FVector(DoorHeight, T + 2.f, 6.f), MatStone);
	}
	Build.Box(FVector(DoorX, -DH, DoorHeight - 4.f), FRotator::ZeroRotator, FVector(DoorHalf * 2.f, T + 2.f, 8.f), MatStone, false);
	Build.Box(FVector(DoorX, -DH, 1.f), FRotator::ZeroRotator, FVector(DoorHalf * 2.f - 12.f, T + 2.f, 2.f), MatStone);

	// The far wall, and the two end walls, which go up past the vault's crown to close it.
	Build.Box(FVector(0.f, DH, AisleCeiling * 0.5f + 5.f), FRotator::ZeroRotator, FVector(Width + T, T, AisleCeiling + 10.f), MatWall);
	const float EndHeight = 440.f;
	for (const float Side : { -1.f, 1.f })
	{
		Build.Box(FVector(Side * WH, 0.f, EndHeight * 0.5f), FRotator(0.f, 90.f, 0.f), FVector(Depth + T, T, EndHeight), MatWall);
	}

	// The aisles' ceilings: boards on joists, from over the beams out to the walls. The north one
	// stops at the middle of the door wall, like the floor.
	for (const float Side : { -1.f, 1.f })
	{
		const float Inner = PierY - 10.f;
		const float Outer = Side < 0.f ? DH : DH + T * 0.5f;
		Build.Box(FVector(0.f, Side * (Inner + Outer) * 0.5f, AisleCeiling + 5.f), FRotator::ZeroRotator, FVector(HalfX * 2.f, Outer - Inner, 10.f), MatBoards);
		for (float X = -HalfX + 34.f; X < HalfX - 20.f; X += 61.f)
		{
			const float Y0 = PierY + BeamHalf;
			Build.Box(FVector(X, Side * (Y0 + HalfY) * 0.5f, AisleCeiling - 6.f), FRotator::ZeroRotator, FVector(8.f, HalfY - Y0, 12.f), MatBeam, false);
		}
	}

	// The piers: a moulded base, a chamfered shaft and a capital, dressed stone. A model rather than
	// boxes: the engine cube's faces do not share one UV orientation, and two sides of every box
	// pier came out with the courses stretched across them. Collision is a plain box per pier.
	for (const float X : PierXs)
	{
		for (const float Side : { -1.f, 1.f })
		{
			const FVector At(X, Side * PierY, 0.f);
			Place(Build, RoomProps::WinePier, At, FRotator(0.f, Side > 0.f ? 0.f : 180.f, 0.f), { { TEXT("Stone"), MatStoneSheet } });
			if (UStaticMeshComponent* Solid = Build.Box(At + FVector(0.f, 0.f, PierTop * 0.5f), FRotator::ZeroRotator, FVector(60.f, 60.f, PierTop), MatStone))
			{
				Solid->SetHiddenInGame(true);
				Solid->SetCastShadow(false);
			}
		}
	}
	// The beams the vault stands on, the length of the room over each row of piers.
	for (const float Side : { -1.f, 1.f })
	{
		Build.Box(FVector(0.f, Side * PierY, (PierTop + BeamTop) * 0.5f), FRotator::ZeroRotator, FVector(HalfX * 2.f, BeamHalf * 2.f, BeamTop - PierTop), MatBeam, false);
	}

	// The vault and its ribs, and the arch into the alcove. In the room's frame (make_wine_cellar.py).
	Place(Build, RoomProps::WineVault, FVector::ZeroVector, FRotator::ZeroRotator, { { TEXT("Vault"), MatVaultSheet }, { TEXT("Rib"), MatStoneSheet } });
	Place(Build, RoomProps::WineArch, FVector(PierXs[0], 0.f, 0.f), FRotator::ZeroRotator, { { TEXT("Stone"), MatStoneSheet } });
}

// ---------------------------------------------------------------------------------------------
// Damp on the walls and the vault.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildWallDamp(FRoomBuilder& Build)
{
	// Damp coming through the walls from the earth, low down and in the corners, and moss in the
	// joints where it is wettest. Only where nothing stands against the wall: a decal reaches nine
	// centimetres off it, and the racks' backs, the barrels' far heads, the furniture backed up to
	// the plaster and the paper pinned on it are all inside that. So the runs are laid along the
	// stretches of bare wall between them (the north wall has none: a rack's back from the corner
	// to the door, a rack and the door's surround leave nothing a stain fits in), and every decal
	// is tested against what was recorded standing there (KeepWallDecalsOff) — a skipped one still
	// draws all its numbers, so the ones after it do not move (09-29).
	FRandomStream Random(4410);
	const FLinearColor Damp(0.40f, 0.34f, 0.27f);
	const FLinearColor Moss(0.16f, 0.22f, 0.08f);
	struct FRun { EWall Wall; float From; float To; };
	const FRun Runs[] = {
		// East: north of the barrels, under the key board; between the barrels and the storage crate;
		// past the crate into the south-east corner.
		{ EWall::East, -HalfY + 30.f, -150.f },
		{ EWall::East, 140.f, 220.f },
		{ EWall::East, 370.f, HalfY - 30.f },
		// West: past the engraving towards the cabinet's corner; between the bookcase and the cork
		// board; the south-west corner past it.
		{ EWall::West, -HalfY + 30.f, -170.f },
		{ EWall::West, 120.f, 230.f },
		{ EWall::West, 360.f, HalfY - 30.f },
		// South: the two gaps between the back racks and the shelving and the writing table.
		{ EWall::South, 380.f, 420.f },
		{ EWall::South, -420.f, -380.f },
	};
	for (const FRun& Run : Runs)
	{
		// On the inner face two centimetres out, aimed at the wall; U runs along it.
		FVector Normal;
		FVector Along;
		switch (Run.Wall)
		{
		case EWall::East:  Normal = FVector(1.f, 0.f, 0.f);  Along = FVector(0.f, 1.f, 0.f); break;
		case EWall::West:  Normal = FVector(-1.f, 0.f, 0.f); Along = FVector(0.f, 1.f, 0.f); break;
		case EWall::South: Normal = FVector(0.f, 1.f, 0.f);  Along = FVector(1.f, 0.f, 0.f); break;
		default:           Normal = FVector(0.f, -1.f, 0.f); Along = FVector(1.f, 0.f, 0.f); break;
		}
		const float Plane = FMath::Abs(Normal.X) > 0.5f ? HalfX - 2.f : HalfY - 2.f;
		const FRotator Aim = Normal.Rotation();
		const int32 Count = FMath::Max(2, FMath::RoundToInt((Run.To - Run.From) / 120.f));
		for (int32 i = 0; i < Count; ++i)
		{
			// Every number drawn into a local first, in order, the crack's whether or not there is one.
			const float T01 = Random.FRandRange(0.f, 1.f);
			const float Height = Random.FRandRange(40.f, 140.f);
			const float WidthCm = Random.FRandRange(70.f, 170.f);
			const float Opacity = Random.FRandRange(0.45f, 0.75f);
			const bool bMoss = Random.FRand() < 0.35f;
			const bool bCrack = Random.FRand() < 0.4f;
			const float CrackWidth = Random.FRandRange(60.f, 140.f);
			const float CrackHeight = Random.FRandRange(80.f, 180.f);
			const float CrackRise = Random.FRandRange(60.f, 160.f);
			const float CrackOpacity = Random.FRandRange(0.5f, 0.85f);
			const float CrackSharpness = Random.FRandRange(16.f, 26.f);

			const float U = FMath::Lerp(Run.From, Run.To, T01);
			const FVector At = Normal * Plane + Along * U + FVector(0.f, 0.f, Height * 0.35f);
			if (!WallDecalHitsSomething(Run.Wall, U, At.Z, WidthCm * 0.5f, Height * 0.5f))
			{
				Build.Stain(RoomSurfaces::Damp, At, Aim, FVector2D(WidthCm, Height), bMoss ? Moss : Damp * 0.8f, bMoss ? Opacity * 0.6f : Opacity, 1.2f);
			}
			const FVector CrackAt = At + FVector(0.f, 0.f, CrackRise);
			if (bCrack && !WallDecalHitsSomething(Run.Wall, U, CrackAt.Z, CrackWidth * 0.5f, CrackHeight * 0.5f))
			{
				Build.Crack(CrackAt, Aim, FVector2D(CrackWidth, CrackHeight), CrackOpacity, CrackSharpness);
			}
		}
	}

	// The vault weeps where it meets the end walls and along its springing: dark streaks. Each is put
	// six centimetres in from the vault's face on its real curve and aimed out along the normal
	// there, so the projection meets the brick squarely: aimed straight up with nine centimetres of
	// reach, the stains on the steep part of the curve came out as thin bands.
	for (int32 i = 0; i < 10; ++i)
	{
		const float X = (i % 2 ? 1.f : -1.f) * Random.FRandRange(HalfX - 140.f, HalfX - 20.f);
		const float Y = Random.FRandRange(-150.f, 150.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const FVector2D Size(Random.FRandRange(60.f, 140.f), Random.FRandRange(50.f, 110.f));
		const float Z = VaultCentreZ + FMath::Sqrt(FMath::Square(VaultRadius) - FMath::Square(Y));
		const FVector Normal = FVector(0.f, Y, Z - VaultCentreZ).GetSafeNormal();
		const FQuat Facing = FQuat(Normal, FMath::DegreesToRadians(Roll)) * FRotationMatrix::MakeFromX(Normal).ToQuat();
		Build.Stain(RoomSurfaces::Damp, FVector(X, Y, Z) - Normal * 6.f, Facing.Rotator(), Size, i % 3 == 0 ? Moss : Damp * 0.7f, 0.6f, 1.3f);
	}
}

// ---------------------------------------------------------------------------------------------
// The racks and what is in them.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::FillRack(const FRack& Rack, int32 Seed, TArray<TArray<FTransform>>& PerWine)
{
	FRandomStream Random(Seed);
	const FTransform RackXf(FRotator(0.f, Rack.Yaw, 0.f), Rack.Location);
	const TArray<float> Sides = Rack.bSpine ? TArray<float>{ 1.f, -1.f } : TArray<float>{ 1.f };
	for (const float Side : Sides)
	{
		// Binned by the wine: two bottlings in bands, the split somewhere up the rack.
		const int32 Split = Random.RandRange(6, 16);
		const int32 Low = Random.RandRange(0, WineCount - 1);
		const int32 High = Random.RandRange(0, WineCount - 1);
		// Where bottles were taken: a block or two cleared together, which is how a cellar is drunk —
		// a case at a time, not a bottle here and there.
		struct FGap { int32 C0, C1, R0, R1; };
		TArray<FGap> Gaps;
		const int32 GapCount = Random.RandRange(1, 2);
		for (int32 g = 0; g < GapCount; ++g)
		{
			const int32 C0 = Random.RandRange(0, RackCols - 3);
			const int32 R0 = Random.RandRange(0, RackRows - 3);
			Gaps.Add({ C0, C0 + Random.RandRange(1, 5), R0, R0 + Random.RandRange(1, 6) });
		}
		for (int32 Row = 0; Row < RackRows; ++Row)
		{
			for (int32 Col = 0; Col < RackCols; ++Col)
			{
				// Every cell draws the same numbers whether or not it is filled, so a change to the
				// gaps does not reshuffle every bottle after it.
				const float Fill = Random.FRand();
				const bool bReversed = Random.FRand() < 0.04f;
				const float Roll = Random.FRandRange(0.f, 360.f);
				const float Jitter = Random.FRandRange(-0.5f, 0.5f);
				const float Tilt = Random.FRandRange(-1.2f, 1.2f);
				const bool bInGap = Gaps.ContainsByPredicate([&](const FGap& Gap) { return Col >= Gap.C0 && Col <= Gap.C1 && Row >= Gap.R0 && Row <= Gap.R1; });
				const float Empty = 0.07f + (Row >= RackRows - 3 ? 0.2f : 0.f);
				if (bInGap || Fill < Empty)
				{
					continue;
				}
				const float X = -RackInner + (Col + 0.5f) * RackPitch + Jitter;
				const float Z = RackPlinth + Row * RackPitch + RackSlat + BottleRadius;
				float BaseY;
				float Dir;
				if (Rack.bSpine)
				{
					BaseY = bReversed ? Side * (RackSpineDepth * 0.5f + 1.f) : Side * 0.8f;
					Dir = bReversed ? -Side : Side;
				}
				else
				{
					BaseY = bReversed ? -RackBackDepth * 0.5f + 1.4f + BottleLength : -RackBackDepth * 0.5f + 1.4f;
					Dir = bReversed ? -1.f : 1.f;
				}
				const FVector Axis = FRotator(Tilt, 0.f, 0.f).RotateVector(FVector(0.f, Dir, 0.f));
				const FTransform Cell(AlongAxis(Axis, Roll), FVector(X, BaseY, Z));
				PerWine[Row < Split ? Low : High].Add(Cell * RackXf);
			}
		}
	}
}

void AWineCellarActor::BuildRacks(FRoomBuilder& Build)
{
	TArray<FRack> Racks;
	// Against the walls, at the back of each bay.
	for (const float X : BayXs)
	{
		Racks.Add({ FVector(X, -HalfY + RackBackDepth * 0.5f, 0.f), 0.f, false });
		Racks.Add({ FVector(X, HalfY - RackBackDepth * 0.5f, 0.f), 180.f, false });
	}
	// The spines between the bays, from each pier back to the wall's rack.
	const float SpineY = (PierY + PierHalf + HalfY - RackBackDepth) * 0.5f;
	for (const float X : PierXs)
	{
		for (const float Side : { -1.f, 1.f })
		{
			Racks.Add({ FVector(X, Side * SpineY, 0.f), 90.f, true });
		}
	}

	TArray<TArray<FTransform>> PerWine;
	PerWine.SetNum(WineCount);
	for (int32 i = 0; i < Racks.Num(); ++i)
	{
		const FRack& Rack = Racks[i];
		KeepWallDecalsOff(Place(Build, Rack.bSpine ? RoomProps::WineRackSpine : RoomProps::WineRackBack, Rack.Location, FRotator(0.f, Rack.Yaw, 0.f),
			{ { TEXT("Oak"), MatRackOak }, { TEXT("Brass"), MatBrass } }));
		const float RackDepth = Rack.bSpine ? RackSpineDepth : RackBackDepth;
		Blocker(Build, Rack.Location + FVector(0.f, 0.f, 145.f), FVector(184.f, RackDepth + 4.f, 290.f), Rack.Yaw);
		FillRack(Rack, 52000 + i * 37, PerWine);
	}

	// One instanced set per bottling for the whole room: thousands of bottles, Nanite meshes.
	for (int32 w = 0; w < WineCount; ++w)
	{
		if (PerWine[w].Num() == 0)
		{
			continue;
		}
		UStaticMesh* Mesh = FRoomShapes::Prop(RoomProps::WineBottles[Wines[w].Mesh]);
		if (UInstancedStaticMeshComponent* Set = Build.Instances(Mesh, nullptr))
		{
			Dress(Set, { { TEXT("Glass"), MatBottleGlass[Wines[w].Glass] }, { TEXT("Capsule"), MatCapsules[Wines[w].Capsule] }, { TEXT("Label"), MatLabel } });
			Set->AddInstances(PerWine[w], /*bShouldReturnIndices*/ false);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// The east end, by the door: barrels, crates, the shelving, the keys and the lantern.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildEastEnd(FRoomBuilder& Build)
{
	FRandomStream Random(4420);

	// Three barrels on their cradles along the end wall, heads to the room. A barrel lies with its
	// axis along X here, so it is the standing model turned on its side; its origin is the middle of
	// its bottom head.
	const float BarrelX = HalfX - 45.f;
	const float BarrelZ = 46.f;
	const float BarrelHalfLength = 43.6f;
	for (const float Y : { -86.f, 0.f, 86.f })
	{
		KeepWallDecalsOff(Place(Build, RoomProps::CellarCradle, FVector(BarrelX, Y, 0.f), FRotator(0.f, 90.f + Random.FRandRange(-2.f, 2.f), 0.f), { { TEXT("Wood"), MatWood } }));
		const FVector Axis(-1.f, 0.f, 0.f);
		if (UStaticMeshComponent* Barrel = Build.Prop(RoomProps::WineBarrel, FVector(BarrelX + BarrelHalfLength, Y, BarrelZ), AlongAxis(Axis, Random.FRandRange(0.f, 360.f)), 0.f, false))
		{
			FRoomShapes::TintSlots(Barrel, FLinearColor(0.26f, 0.23f, 0.20f));
			KeepWallDecalsOff(Barrel);
		}
		Blocker(Build, FVector(BarrelX, Y, 45.f), FVector(90.f, 78.f, 90.f));
		// A wooden spigot in the middle barrel's head, and what dripped from it, long dried.
		if (FMath::IsNearlyZero(Y))
		{
			const float HeadX = BarrelX - BarrelHalfLength;
			Build.Cyl(FVector(HeadX - 4.f, Y, BarrelZ - 18.f), FRotator(90.f, 0.f, 0.f), FVector(3.4f, 3.4f, 9.f), MatWood, false);
			Build.Cyl(FVector(HeadX - 7.f, Y, BarrelZ - 21.f), FRotator::ZeroRotator, FVector(1.8f, 1.8f, 5.f), MatWood, false);
			Build.Box(FVector(HeadX - 7.f, Y, BarrelZ - 14.f), FRotator::ZeroRotator, FVector(1.4f, 6.f, 2.f), MatBrass, false);
			Build.Stain(RoomSurfaces::Damp, FVector(HeadX - 12.f, Y, 4.f), FRotator(-90.f, 0.f, 20.f), FVector2D(44.f, 32.f),
				FLinearColor(0.05f, 0.006f, 0.01f), 0.7f, 1.2f, 0.6f);
		}
	}

	// The rusted shelving against the far wall in the south-east corner, a crate on its top shelf.
	if (UStaticMeshComponent* Shelves = Build.Prop(RoomProps::MetalRack, FVector(505.f, HalfY - 31.f, 0.f), FRotator(0.f, 180.f + 2.f, 0.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Shelves, FLinearColor(0.55f, 0.50f, 0.46f));
		KeepWallDecalsOff(Shelves);
	}
	Blocker(Build, FVector(505.f, HalfY - 31.f, 95.f), FVector(92.f, 60.f, 190.f));
	// The storage crate along the east wall, and a stack of wine cases by it with their brands.
	if (UStaticMeshComponent* Store = Build.Prop(RoomProps::StorageCrate, FVector(HalfX - 28.f, 300.f, 1.f), FRotator(0.f, 90.f, 0.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Store, FLinearColor(0.45f, 0.42f, 0.38f));
		KeepWallDecalsOff(Store);
	}
	Blocker(Build, FVector(HalfX - 28.f, 300.f, 23.f), FVector(54.f, 118.f, 46.f));
	struct FCase { FVector At; float Yaw; int32 Brand; bool bOpen; };
	const FCase Cases[] = {
		{ FVector(470.f, 250.f, 0.f), 8.f, 0, false },
		{ FVector(472.f, 248.f, 20.2f), -4.f, 6, false },
		{ FVector(466.f, 252.f, 40.4f), 15.f, 2, false },
		{ FVector(470.f, 300.f, 0.f), 84.f, 7, false },
		{ FVector(430.f, 330.f, 0.f), -22.f, 3, true },
	};
	for (const FCase& Case : Cases)
	{
		Place(Build, Case.bOpen ? RoomProps::WineCrateOpen : RoomProps::WineCrate, Case.At, FRotator(0.f, Case.Yaw, 0.f),
			{ { TEXT("Wood"), MatDeal }, { TEXT("Iron"), MatIron } });
		const FVector Out = Heading(Case.Yaw);
		Brand(Build, Case.Brand, Case.At + Out * 25.06f + FVector(0.f, 0.f, 9.5f), Out, 27.f);
		Blocker(Build, Case.At + FVector(0.f, 0.f, 10.f), FVector(50.f, 33.f, 20.f), Case.Yaw);
		if (Case.bOpen)
		{
			// Half a case left: six bottles lying in it, the other six gone.
			const FTransform CaseXf(FRotator(0.f, Case.Yaw, 0.f), Case.At);
			for (int32 i = 0; i < 6; ++i)
			{
				const float Y = -12.f + (i % 3) * 8.f;
				const float Z = 1.2f + BottleRadius + (i / 3) * (BottleRadius * 1.8f);
				const float Flip = (i % 2) ? 1.f : -1.f;
				const FVector Local(-Flip * 15.f, Y, Z);
				const FTransform Cell(AlongAxis(FVector(Flip, 0.f, 0.f), Random.FRandRange(0.f, 360.f)), Local);
				const FTransform World = Cell * CaseXf;
				Bottle(Build, RoomProps::WineBottleFull, World.GetLocation(), World.Rotator(), 3);
			}
		}
	}

	// The keys, on a board on the east wall by the door, where whoever came down took them from.
	KeepWallDecalsOff(Place(Build, RoomProps::CellarKeyRack, FVector(HalfX, -320.f, 152.f), FRotator(0.f, 90.f, 0.f),
		{ { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron }, { TEXT("Brass"), MatBrass }, { TEXT("Tag"), MatTag } }));

	// A lantern hung on an iron hook on the pier by the door: the light the cellar was walked with,
	// left where it was hung, dusted over, its candle long gone.
	const FVector PierFace(PierXs[3] + PierHalf, -PierY, 0.f);
	Build.Box(PierFace + FVector(5.f, 0.f, 214.f), FRotator::ZeroRotator, FVector(10.f, 1.6f, 1.6f), MatIron, false);
	Build.Box(PierFace + FVector(10.f, 0.f, 211.f), FRotator::ZeroRotator, FVector(1.6f, 1.6f, 7.f), MatIron, false);
	if (UStaticMeshComponent* Lantern = Build.Prop(RoomProps::CellarLantern, PierFace + FVector(12.f, 0.f, 156.f), FRotator(0.f, 30.f, 0.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Lantern, FLinearColor(0.22f, 0.21f, 0.19f));
	}
}

// ---------------------------------------------------------------------------------------------
// The tasting table in the middle of the nave, as it was left.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildTastingTable(FRoomBuilder& Build)
{
	FRandomStream Random(4430);
	const FVector T(TastingTableX, 0.f, 0.f);
	const float Top = TastingTableTop;
	Place(Build, RoomProps::CellarTable, T, FRotator::ZeroRotator, { { TEXT("Oak"), MatOak } });
	Blocker(Build, T + FVector(0.f, 0.f, 39.f), FVector(300.f, 100.f, 78.f));
	auto On = [&](float X, float Y) { return T + FVector(X, Y, Top); };

	// The west end, where they stood and tasted: glasses, two with the last of the wine dried in
	// them, one knocked over; the bottle they opened, its cork still on the corkscrew; another not
	// yet opened; the decanter, its stopper lying beside it.
	Glass(Build, On(-122.f, 26.f), FRotator(0.f, 0.f, 0.f), true);
	Glass(Build, On(-104.f, 34.f), FRotator(0.f, 40.f, 0.f), true);
	Glass(Build, On(-132.f, -4.f), FRotator(0.f, 10.f, 0.f), false);
	Glass(Build, On(-62.f, -32.f), FRotator(0.f, 80.f, 0.f), true);
	{
		// On its side: rim and foot both touch, so the axis rises from rim to foot by the difference.
		const float Rise = FMath::RadiansToDegrees(FMath::Atan2(3.65f - 3.15f, 20.8f));
		const FVector Dir = FRotator(Rise, -150.f, 0.f).RotateVector(FVector(1.f, 0.f, 0.f));
		Glass(Build, On(-104.f, -28.f) + FVector(0.f, 0.f, 3.65f), AlongAxis(-Dir, 30.f), true);
		Build.Stain(RoomSurfaces::Damp, On(-112.f, -34.f) + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, 30.f), FVector2D(16.f, 24.f),
			FLinearColor(0.05f, 0.006f, 0.01f), 0.75f, 1.0f, 0.7f);
	}
	Bottle(Build, RoomProps::WineBottleOpen, On(-92.f, -8.f), FRotator(0.f, 160.f, 0.f), 0);
	Bottle(Build, RoomProps::WineBottleFull, On(-142.f, -24.f), FRotator(0.f, 200.f, 0.f), 2);
	if (UStaticMeshComponent* Screw = Build.PropSeated(RoomProps::CellarCorkscrew, On(-78.f, -30.f), FRotator(90.f, 25.f, 0.f), 0.f, false))
	{
		Dress(Screw, { { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron }, { TEXT("Cork"), MatCork }, { TEXT("Stain"), MatWineStain } });
	}
	Place(Build, RoomProps::WineDecanter, On(-62.f, 16.f), FRotator(0.f, 0.f, 0.f), { { TEXT("Crystal"), MatCrystalDusty }, { TEXT("Residue"), MatResidue } });
	if (UStaticMeshComponent* Stopper = Build.PropSeated(RoomProps::WineDecanterStopper, On(-44.f, 32.f), FRotator(80.f, -40.f, 0.f), 0.f, false))
	{
		Dress(Stopper, { { TEXT("Crystal"), MatCrystalDusty } });
	}
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector At = On(Random.FRandRange(-130.f, -40.f), Random.FRandRange(-40.f, 40.f));
		Place(Build, RoomProps::WineCork, At + FVector(0.f, 0.f, 1.2f), AlongAxis(Heading(Random.FRandRange(0.f, 360.f)), Random.FRandRange(0.f, 360.f)),
			{ { TEXT("Cork"), MatCork }, { TEXT("Stain"), MatWineStain } });
	}

	// Two candlesticks down the middle, burned to the sockets.
	for (const FVector2D At : { FVector2D(-20.f, 6.f), FVector2D(34.f, -8.f) })
	{
		Place(Build, RoomProps::CellarCandlestick, On(At.X, At.Y), FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
			{ { TEXT("Brass"), MatBrass }, { TEXT("Wax"), MatWax }, { TEXT("Wick"), MatWick } });
	}

	// The notebook the tastings were written up in, open at the east end with its pencil: the clue.
	if (AClueActor* Clue = SpawnClue(On(80.f, 6.f), FRotator(0.f, -8.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		for (const float S : { -1.f, 1.f })
		{
			// An open book lies in a shallow V, pinched at the spine: pitch is the tilt about it.
			const FTransform Half(FRotator(S * 2.5f, 0.f, 0.f), FVector(S * 7.6f, 0.f, 0.35f));
			B.Box(Half.GetLocation(), Half.Rotator(), FVector(15.4f, 21.4f, 0.5f), MatLeatherBox, false);
			const FTransform Block = FTransform(FVector(0.f, 0.f, 0.75f)) * Half;
			B.Box(Block.GetLocation(), Block.Rotator(), FVector(14.6f, 20.6f, 0.9f), MatPages, false);
			// Read from the south side of the table: the tops of the pages towards the north.
			const FTransform Face = FTransform(FVector(0.f, 0.f, 0.47f)) * Block;
			Paper(B, S < 0.f ? 10 : 11, Face.GetLocation(), Face.GetRotation().GetUpVector(), Face.GetRotation().RotateVector(FVector(0.f, -1.f, 0.f)), 14.f, 19.6f);
		}
		B.Cyl(FVector(4.f, 13.f, 1.8f), FRotator(90.f, 70.f, 0.f), FVector(0.8f, 0.8f, 15.f), MatWood, false);
		B.Stain(RoomSurfaces::Damp, FVector(0.f, 0.f, 5.f), FRotator(-90.f, 0.f, 0.f), FVector2D(28.f, 36.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.25f, 1.4f);
	}

	// Dust over the whole top, and a wine ring where a glass stood and was taken away.
	Build.Stain(RoomSurfaces::Damp, T + FVector(0.f, 0.f, Top + 4.f), FRotator(-90.f, 0.f, 90.f), FVector2D(300.f, 100.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.35f, 1.5f);
	Build.Stain(RoomSurfaces::Damp, On(-40.f, -12.f) + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, 0.f), FVector2D(9.f, 9.f), FLinearColor(0.05f, 0.006f, 0.01f), 0.6f, 0.6f);
}

// ---------------------------------------------------------------------------------------------
// The tasting alcove through the arch: the one place in the room that was for sitting in.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildAlcove(FRoomBuilder& Build)
{
	// The two armchairs, facing each other across the table and turned a little to the room, as two
	// people sit who are talking and not only drinking.
	for (const float Side : { -1.f, 1.f })
	{
		float Yaw;
		const FVector At = AlcoveChair(Side, Yaw);
		Place(Build, RoomProps::CellarArmchair, At, FRotator(0.f, Yaw, 0.f),
			{ { TEXT("Leather"), MatLeather }, { TEXT("Brass"), MatBrass }, { TEXT("Wood"), MatWood } });
		Blocker(Build, At + FVector(0.f, 0.f, 45.f), FVector(90.f, 86.f, 90.f), Yaw);
		const FVector Fwd = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.f, 1.f, 0.f));
		// Dust on the seat and the arms, settled where nobody has sat since.
		Build.Stain(RoomSurfaces::Damp, At + Fwd * 4.f + FVector(0.f, 0.f, 56.f), FRotator(-90.f, 0.f, Yaw), FVector2D(84.f, 80.f),
			FLinearColor(0.42f, 0.40f, 0.36f), 0.3f, 1.5f);
	}

	// The table between them, and on it what was left: the bottle on its tray with the candle burned
	// down into a puddle beside it, the cork, and the two glasses, each on the side of its chair.
	const FVector Table = AlcoveTable();
	Place(Build, RoomProps::CellarPedestalTable, Table, FRotator(0.f, 20.f, 0.f), { { TEXT("Marble"), MatMarble }, { TEXT("Oak"), MatOak } });
	Blocker(Build, Table + FVector(0.f, 0.f, 31.f), FVector(60.f, 60.f, 62.f));
	const float Top = 62.6f;
	if (AClueActor* Clue = SpawnClue(Table + FVector(0.f, 0.f, Top), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		const FTransform Tray(FRotator(0.f, 8.f, 0.f), FVector(-3.f, 2.f, 0.f));
		if (UStaticMeshComponent* TrayMesh = B.Prop(RoomProps::CellarTray, Tray.GetLocation(), Tray.Rotator(), 0.f, false))
		{
			Dress(TrayMesh, { { TEXT("Oak"), MatOak } });
		}
		auto OnTray = [&](float X, float Y) { return Tray.TransformPosition(FVector(X, Y, 1.2f)); };
		Bottle(B, RoomProps::WineBottleOpenB, OnTray(-10.f, 5.f), FRotator(0.f, 70.f, 0.f), 5);
		Place(B, RoomProps::CellarChamberstick, OnTray(12.f, -5.f), FRotator(0.f, 130.f, 0.f), { { TEXT("Brass"), MatBrass }, { TEXT("Wax"), MatWax }, { TEXT("Wick"), MatWick } });
		Place(B, RoomProps::WineCork, OnTray(6.f, 9.f) + FVector(0.f, 0.f, 1.2f), AlongAxis(Heading(35.f), 80.f), { { TEXT("Cork"), MatCork }, { TEXT("Stain"), MatWineStain } });
		Glass(B, FVector(4.f, -24.f, 0.f), FRotator(0.f, 20.f, 0.f), true);
		Glass(B, FVector(-6.f, 24.f, 0.f), FRotator(0.f, -50.f, 0.f), true);
		B.Stain(RoomSurfaces::Damp, FVector(0.f, 0.f, 5.f), FRotator(-90.f, 0.f, 0.f), FVector2D(66.f, 66.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.3f, 1.5f);
	}

	// The bookcase against the end wall between the chairs: books on wine, and the tasting journals.
	KeepWallDecalsOff(Place(Build, RoomProps::CellarBookshelf, FVector(-HalfX + 16.f, 0.f, 0.f), FRotator(0.f, -90.f, 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Gilt"), MatGilt }, { TEXT("Pages"), MatPages }, { TEXT("BookRed"), MatBooks[0] },
		  { TEXT("BookGreen"), MatBooks[1] }, { TEXT("BookBrown"), MatBooks[2] }, { TEXT("BookBlack"), MatBooks[3] }, { TEXT("BookTan"), MatBooks[4] } }));
	Blocker(Build, FVector(-HalfX + 16.f, 0.f, 56.f), FVector(32.f, 96.f, 112.f));
	Build.Stain(RoomSurfaces::Damp, FVector(-HalfX + 16.f, 0.f, 116.f), FRotator(-90.f, 0.f, 0.f), FVector2D(96.f, 34.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.4f, 1.4f);

	// The clock over it, stopped, its glass smashed out and its hand hanging; and a small engraving of
	// a chateau among its vines to one side.
	KeepWallDecalsOff(Place(Build, RoomProps::CellarClock, FVector(-HalfX, 0.f, 182.f), FRotator(0.f, -90.f, 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Shadow"), MatShadow }, { TEXT("Dial"), MatDial }, { TEXT("Brass"), MatBrass },
		  { TEXT("Ink"), MatInk }, { TEXT("Crystal"), MatCrystal } }));
	KeepWallDecalsOff(Place(Build, RoomProps::CellarPrintFrame, FVector(-HalfX, -128.f, 136.f), FRotator(0.f, -90.f, 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Gilt"), MatGilt }, { TEXT("Shadow"), MatShadow } }));
	// Paper hung on a wall takes the wall's decals as if printed on it (the kitchen's calendar), so it
	// takes none, whatever the keep-outs leave near it.
	if (UStaticMeshComponent* Print = Paper(Build, 14, FVector(-HalfX + 0.75f, -128.f, 156.f), FVector(1.f, 0.f, 0.f), FVector::UpVector, 26.f, 32.f))
	{
		Print->SetReceivesDecals(false);
		KeepWallDecalsOff(Print);
	}
}

// ---------------------------------------------------------------------------------------------
// The corners past the end piers: the cabinet, and the table the cellar book was kept at.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildCorners(FRoomBuilder& Build)
{
	FRandomStream Random(4440);

	// North-west: the cabinet the best bottles stood in, behind glass.
	const FVector Cabinet(-500.f, -HalfY + 24.f, 0.f);
	KeepWallDecalsOff(Place(Build, RoomProps::CellarWineCabinet, Cabinet, FRotator::ZeroRotator,
		{ { TEXT("Oak"), MatOak }, { TEXT("Brass"), MatBrass }, { TEXT("Shadow"), MatShadow }, { TEXT("Crystal"), MatCrystalDusty } }));
	Blocker(Build, Cabinet + FVector(0.f, 0.f, 103.f), FVector(114.f, 48.f, 206.f));
	for (const float ShelfZ : { 92.f, 124.f, 158.f })
	{
		const int32 Count = ShelfZ > 150.f ? 3 : 5;
		for (int32 i = 0; i < Count; ++i)
		{
			if (Random.FRand() < 0.15f)
			{
				continue;
			}
			const float X = -40.f + 80.f * i / FMath::Max(1, Count - 1) + Random.FRandRange(-2.f, 2.f);
			// Labels to the glass, as bottles that were shown were stood.
			Bottle(Build, RoomProps::WineBottleFull, Cabinet + FVector(X, -6.f, ShelfZ), FRotator(0.f, 180.f + Random.FRandRange(-10.f, 10.f), 0.f), i % 2 ? 2 : 4);
		}
	}
	Build.Stain(RoomSurfaces::Damp, Cabinet + FVector(0.f, -4.f, 202.f), FRotator(-90.f, 0.f, 0.f), FVector2D(46.f, 114.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.4f, 1.4f);

	// South-west: the writing table, its chair pushed back, the cellar book open on it at the last
	// page written up, the ink and the pen; the bin chart pinned over it; the cork collection on the
	// end wall beside it.
	const FVector Desk(-500.f, HalfY - 28.f, 0.f);
	KeepWallDecalsOff(Place(Build, RoomProps::CellarWritingTable, Desk, FRotator(0.f, 180.f, 0.f), { { TEXT("Oak"), MatOak }, { TEXT("Brass"), MatBrass } }));
	Blocker(Build, Desk + FVector(0.f, 0.f, 38.f), FVector(96.f, 52.f, 76.f));
	if (UStaticMeshComponent* Chair = Build.Prop(RoomProps::KitchenChair, Desk + FVector(22.f, -64.f, 0.f), FRotator(0.f, -14.f, 0.f), 0.f, false))
	{
		// The kitchen's ladder-back, its blue-grey paint pulled down and warmed to a dark cellar chair.
		FRoomShapes::TintSlots(Chair, FLinearColor(0.22f, 0.17f, 0.13f));
	}
	Blocker(Build, Desk + FVector(22.f, -64.f, 45.f), FVector(48.f, 48.f, 90.f), -14.f);
	if (AClueActor* Clue = SpawnClue(Desk + FVector(4.f, -2.f, 76.f), FRotator(0.f, 4.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		for (const float S : { -1.f, 1.f })
		{
			const FTransform Half(FRotator(S * 2.f, 0.f, 0.f), FVector(S * 11.4f, 0.f, 0.5f));
			B.Box(Half.GetLocation(), Half.Rotator(), FVector(23.f, 31.f, 0.8f), MatBooks[2], false);
			const FTransform Block = FTransform(FVector(0.f, 0.f, 1.2f)) * Half;
			B.Box(Block.GetLocation(), Block.Rotator(), FVector(22.f, 30.f, 1.6f), MatPages, false);
			// Read sitting at it, facing the wall: the tops of the pages away from the chair.
			const FTransform Face = FTransform(FVector(0.f, 0.f, 0.82f)) * Block;
			Paper(B, S < 0.f ? 8 : 9, Face.GetLocation(), Face.GetRotation().GetUpVector(), Face.GetRotation().RotateVector(FVector(0.f, 1.f, 0.f)), 21.4f, 29.9f);
		}
		// The inkwell, square glass gone black inside, its brass lid open; the dip pen beside it.
		B.Box(FVector(32.f, 8.f, 2.6f), FRotator(0.f, 12.f, 0.f), FVector(6.f, 6.f, 5.2f), MatShadow, false);
		B.Cyl(FVector(32.f, 8.f, 5.6f), FRotator(0.f, 12.f, 0.f), FVector(3.2f, 3.2f, 1.2f), MatBrass, false);
		B.Cyl(FVector(30.f, -6.f, 0.5f), FRotator(90.f, -30.f, 0.f), FVector(0.9f, 0.9f, 17.f), MatOak, false);
		B.Stain(RoomSurfaces::Damp, FVector(10.f, 0.f, 5.f), FRotator(-90.f, 0.f, 0.f), FVector2D(50.f, 90.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.3f, 1.4f);
	}
	if (AClueActor* Chart = SpawnClue(FVector(Desk.X, HalfY - 1.2f, 168.f), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Chart, Chart->GetRootScene());
		Paper(B, 12, FVector::ZeroVector, FVector(0.f, -1.f, 0.f), FVector::UpVector, 80.f, 40.f);
		for (const FVector2D Pin : { FVector2D(-38.f, 18.f), FVector2D(38.f, 18.f), FVector2D(-38.f, -18.f) })
		{
			B.Sph(FVector(Pin.X, -0.4f, Pin.Y), 1.2f, MatIron);
		}
		// The fourth corner has lost its pin and curls away from the wall.
		B.Box(FVector(38.f, -1.4f, -18.f), FRotator(0.f, 0.f, -18.f), FVector(6.f, 0.3f, 4.f), MatPages, false);
		// Pinned to the wall: none of the wall's decals on it, and none put near it.
		TInlineComponentArray<UPrimitiveComponent*> Parts(Chart);
		for (UPrimitiveComponent* Part : Parts)
		{
			Part->SetReceivesDecals(false);
		}
		KeepWallDecalsOff(Chart);
	}
	KeepWallDecalsOff(Place(Build, RoomProps::CellarCorkBoard, FVector(-HalfX, 300.f, 94.f), FRotator(0.f, -90.f, 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Felt"), MatFelt }, { TEXT("Cork"), MatCork }, { TEXT("Stain"), MatWineStain } }));
	// The corks that have dropped out of the bottom of it, on the floor below.
	for (int32 i = 0; i < 9; ++i)
	{
		const FVector At(-HalfX + Random.FRandRange(6.f, 40.f), 300.f + Random.FRandRange(-26.f, 26.f), 1.2f);
		KeepWallDecalsOff(Place(Build, RoomProps::WineCork, At, AlongAxis(Heading(Random.FRandRange(0.f, 360.f)), Random.FRandRange(0.f, 360.f)),
			{ { TEXT("Cork"), MatCork }, { TEXT("Stain"), MatWineStain } }));
	}
}

// ---------------------------------------------------------------------------------------------
// The floor: water, damp, moss, corks, a bottle that fell and one that broke.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildFloor(FRoomBuilder& Build)
{
	FRandomStream Random(4450);
	const FLinearColor Water(0.12f, 0.12f, 0.12f);
	const FLinearColor Damp(0.30f, 0.26f, 0.21f);
	const FLinearColor Moss(0.12f, 0.17f, 0.06f);
	const FLinearColor Dirt(0.20f, 0.17f, 0.13f);

	// Standing water where the floor is lowest: in the nave east of the table, and in a bay.
	Build.Stain(RoomSurfaces::Floorboards, FVector(260.f, 70.f, 4.f), FRotator(-90.f, 0.f, 20.f), FVector2D(150.f, 90.f), Water, 0.8f, 1.2f, 0.1f);
	Build.Stain(RoomSurfaces::Floorboards, FVector(-250.f, -60.f, 4.f), FRotator(-90.f, 0.f, 70.f), FVector2D(70.f, 46.f), Water, 0.75f, 1.3f, 0.1f);
	Build.Stain(RoomSurfaces::Floorboards, FVector(0.f, -330.f, 4.f), FRotator(-90.f, 0.f, -10.f), FVector2D(90.f, 60.f), Water, 0.8f, 1.2f, 0.1f);
	// Damp patches across the flags, and moss round the foot of every pier. None of them near the
	// door: projected from four above the flags they take in the sill, the foot of the reveal and
	// the foot of the leaf wherever it stands in its swing (FloorDecalReachesDoor). A skipped one
	// still draws its numbers, each into a local in order (09-29).
	for (int32 i = 0; i < 16; ++i)
	{
		const float X = Random.FRandRange(-HalfX + 40.f, HalfX - 40.f);
		const float Y = Random.FRandRange(-HalfY + 40.f, HalfY - 40.f);
		const float SizeX = Random.FRandRange(80.f, 200.f);
		const float SizeY = Random.FRandRange(60.f, 160.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float Opacity = Random.FRandRange(0.35f, 0.6f);
		const FVector2D Size(SizeX, SizeY);
		if (FloorDecalReachesDoor(FVector2D(X, Y), WineFloorDecalHalfExtent(Size, Roll)))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(X, Y, 4.f), FRotator(-90.f, 0.f, Roll), Size, Damp, Opacity, 1.3f);
	}
	for (const float X : PierXs)
	{
		for (const float Side : { -1.f, 1.f })
		{
			const float Roll = Random.FRandRange(0.f, 360.f);
			const FVector2D Size(110.f, 110.f);
			if (FloorDecalReachesDoor(FVector2D(X, Side * PierY), WineFloorDecalHalfExtent(Size, Roll)))
			{
				continue;
			}
			Build.Stain(RoomSurfaces::Damp, FVector(X, Side * PierY, 4.f), FRotator(-90.f, 0.f, Roll), Size, Moss, 0.5f, 1.3f);
		}
	}
	// Dirt and dust driven into the corners, where nobody's broom reached — but for the north-east
	// corner, which is the door's: that corner is under half a metre from the surround, and a patch
	// that size there lay on the sill and in the leaf's sweep.
	for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f) })
	{
		const FVector2D At(Corner.X * (HalfX - 30.f), Corner.Y * (HalfY - 30.f));
		const FVector2D Size(110.f, 110.f);
		if (FloorDecalReachesDoor(At, WineFloorDecalHalfExtent(Size, 45.f)))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(At.X, At.Y, 4.f), FRotator(-90.f, 0.f, 45.f), Size, Dirt, 0.7f, 1.2f);
	}

	// Corks dropped and kicked about, mostly round the table and in front of the racks.
	if (UInstancedStaticMeshComponent* Corks = Build.Instances(FRoomShapes::Prop(RoomProps::WineCork), nullptr))
	{
		Dress(Corks, { { TEXT("Cork"), MatCork }, { TEXT("Stain"), MatWineStain } });
		TArray<FTransform> Lying;
		for (int32 i = 0; i < 34; ++i)
		{
			const bool bNearTable = i < 16;
			const FVector At = bNearTable
				? FVector(20.f + Random.FRandRange(-190.f, 190.f), Random.FRandRange(-90.f, 90.f), 1.2f)
				: FVector(Random.FRandRange(-330.f, 330.f), (i % 2 ? 1.f : -1.f) * Random.FRandRange(240.f, 395.f), 1.2f);
			const FRotator Rot = AlongAxis(Heading(Random.FRandRange(0.f, 360.f)), Random.FRandRange(0.f, 360.f));
			Lying.Add(FTransform(Rot, At));
		}
		Corks->AddInstances(Lying, false);
	}

	// A bottle that went off the rack in the middle south bay and broke on the flags: the bottom of
	// it on its side, the neck rolled away, glass everywhere, and the stain of the wine, black now.
	const FVector Broke(10.f, 300.f, 0.f);
	Build.Stain(RoomSurfaces::Damp, Broke + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, 30.f), FVector2D(80.f, 110.f), FLinearColor(0.04f, 0.005f, 0.008f), 0.75f, 1.3f, 0.6f);
	Place(Build, RoomProps::WineBottleBroken, Broke + FVector(-8.f, 4.f, BottleRadius), AlongAxis(Heading(200.f), 40.f),
		{ { TEXT("Glass"), MatBottleGlass[0] }, { TEXT("Label"), MatLabel } });
	Place(Build, RoomProps::WineBottleNeck, Broke + FVector(34.f, -30.f, 3.4f) - Heading(110.f) * 19.f, AlongAxis(Heading(110.f), 0.f),
		{ { TEXT("Glass"), MatBottleGlass[0] }, { TEXT("Capsule"), MatCapsules[2] } });
	Place(Build, RoomProps::WineShards, Broke, FRotator(0.f, 20.f, 0.f), { { TEXT("Glass"), MatBottleGlass[0] } });
	Place(Build, RoomProps::WineShards, Broke + FVector(40.f, -20.f, 0.f), FRotator(0.f, 140.f, 0.f), { { TEXT("Glass"), MatBottleGlass[0] } });
	// And one that rolled out whole, across the north bay by the arch.
	Bottle(Build, RoomProps::WineBottles[1], FVector(-230.f, -300.f, BottleRadius), AlongAxis(Heading(60.f), 120.f), 1);
	Place(Build, RoomProps::WineShards, FVector(-380.f, 120.f, 0.f), FRotator(0.f, 60.f, 0.f), { { TEXT("Glass"), MatBottleGlass[1] } });
}

// ---------------------------------------------------------------------------------------------
// Cobwebs: in the racks' corners, between the racks and the joists, under the chairs and tables.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildCobwebs(FRoomBuilder& Build)
{
	FRandomStream Random(4460);
	// The engine plane lies in its XY with its normal up; roll ninety stands it up facing along -Y.
	auto Upright = [](float Yaw) { return FRotator(0.f, Yaw, 90.f); };

	// Across the back corners of every bay, high up, where the spine meets the back rack.
	for (const float BayX : BayXs)
	{
		for (const float Side : { -1.f, 1.f })
		{
			for (const float Corner : { -1.f, 1.f })
			{
				if (Random.FRand() < 0.3f)
				{
					continue;
				}
				const FVector At(BayX + Corner * 78.f, Side * (HalfY - RackBackDepth - 14.f), Random.FRandRange(236.f, 270.f));
				Web(Build, At, Upright(Corner * Side * 45.f), FVector2D(Random.FRandRange(32.f, 46.f), Random.FRandRange(30.f, 50.f)));
			}
			// And a sheet from the top of the back rack up to the joists.
			Web(Build, FVector(BayX + Random.FRandRange(-40.f, 40.f), Side * (HalfY - 18.f), 296.f), Upright(0.f), FVector2D(Random.FRandRange(50.f, 90.f), 10.f));
		}
	}
	// Under the armchairs, between their feet, and under the little table.
	for (const float Side : { -1.f, 1.f })
	{
		float Yaw;
		const FVector Chair = AlcoveChair(Side, Yaw);
		const FVector Front = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.f, 36.f, 0.f));
		Web(Build, Chair + Front + FVector(0.f, 0.f, 6.f), Upright(Yaw), FVector2D(72.f, 11.f));
		Web(Build, Chair + FVector(0.f, 0.f, 6.f), Upright(Yaw + 90.f), FVector2D(60.f, 10.f));
	}
	for (int32 k = 0; k < 3; ++k)
	{
		Web(Build, AlcoveTable() + FVector(0.f, 0.f, 9.f) + Heading(20.f + 120.f * k + 60.f) * 10.f, Upright(20.f + 120.f * k + 150.f), FVector2D(22.f, 16.f));
	}
	// Under the long table, in the angles of its end legs and stretchers.
	for (const float End : { -1.f, 1.f })
	{
		for (const float Side : { -1.f, 1.f })
		{
			Web(Build, FVector(TastingTableX + End * TastingTableLegX, Side * TastingTableLegY, 36.f), Upright(0.f), FVector2D(36.f, 44.f));
		}
	}
	Web(Build, FVector(TastingTableX, 0.f, 40.f), Upright(90.f), FVector2D(60.f, 46.f));
	// In the vault's corners at the end walls, and slung under the ends of the beams.
	for (const float End : { -1.f, 1.f })
	{
		for (const float Side : { -1.f, 1.f })
		{
			Web(Build, FVector(End * (HalfX - 26.f), Side * 160.f, 330.f), FRotator(0.f, End * Side * 45.f, 180.f), FVector2D(70.f, 60.f));
			Web(Build, FVector(End * (HalfX - 20.f), Side * PierY, 262.f), Upright(90.f), FVector2D(40.f, 18.f));
		}
	}
}

// ---------------------------------------------------------------------------------------------
// A low mist on the floor: a local fog volume, thickest on the flags and gone by knee height.
// ---------------------------------------------------------------------------------------------

void AWineCellarActor::BuildMist()
{
	// A local fog volume is ALWAYS a sphere: its scene proxy replaces the component's scale with the
	// largest of its three axes (FLocalFogVolumeSceneProxy::UpdateComponentTransform), so a volume
	// scaled to an ellipsoid over the room was a sphere of the room's half-length — 6.7m about a
	// centre 4.4m from the north and south walls — and the mist ran two metres out into the cellar
	// corridor past the door and half a metre into the room next door. So it is two spheres, each
	// as big as the room's half-depth allows less a margin, their centres on the floor (the lower
	// halves are under it) and as far apart along the nave as keeps their ends off the end walls.
	// The middle of the nave, where they overlap, gets both: each is at half the density the one
	// volume had, so the overlap is the old mist and the two ends half of it.
	//
	// The height fog is defined in the sphere's own unit space (falloff per unit radius, scaled by a
	// hundredth), so the falloff is worked out from the radius to keep the mist's thickness the same
	// in centimetres: it halves every ~66cm up from the flags, as it did in the one big volume.
	const float Base = ULocalFogVolumeComponent::GetBaseVolumeSize();
	const float Radius = HalfY - 10.f;
	const float CentreX = HalfX - 10.f - Radius;
	const float ScaleHeightCm = HalfX * 1.1f / 7.f;
	for (const float Side : { -1.f, 1.f })
	{
		ULocalFogVolumeComponent* Volume = NewObject<ULocalFogVolumeComponent>(this, Side < 0.f ? TEXT("GroundMistWest") : TEXT("GroundMistEast"));
		if (!Volume)
		{
			continue;
		}
		Volume->SetMobility(EComponentMobility::Movable);
		Volume->AttachToComponent(CellarRoot, FAttachmentTransformRules::KeepRelativeTransform);
		Volume->SetRelativeLocation(FVector(Side * CentreX, 0.f, 0.f));
		Volume->SetRelativeScale3D(FVector(Radius / Base));
		Volume->SetRadialFogExtinction(0.f);
		Volume->SetHeightFogExtinction(0.45f);
		Volume->SetHeightFogFalloff(100.f * Radius / ScaleHeightCm);
		Volume->SetHeightFogOffset(0.f);
		Volume->SetFogAlbedo(FLinearColor(0.72f, 0.72f, 0.70f));
		Volume->SetFogPhaseG(0.25f);
		Volume->RegisterComponent();
		AddInstanceComponent(Volume);
		Mist.Add(Volume);
	}
}
