#include "RoomDressingActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "StormWindowActor.h"
#include "DustMotesComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

namespace
{
	/** Which surface a mark or panel is painted onto, so callers can work in wall-local U/V. */
	enum class EWallSide : uint8
	{
		North, // the door wall, at -Y
		South,
		East,  // the window wall, at +X
		West
	};
}

ARoomDressingActor::ARoomDressingActor()
{
	PrimaryActorTick.bCanEverTick = true;

	DressingRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DressingRoot"));
	SetRootComponent(DressingRoot);
	DressingRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void ARoomDressingActor::Configure(const FRoomDressingSetup& InSetup)
{
	Setup = InSetup;
}

void ARoomDressingActor::CacheMaterials()
{
	FRoomBuilder Build(this, DressingRoot);

	MatPlaster = Build.Material(RoomPalette::Plaster, 0.96f);
	MatWallpaper = Build.Material(RoomPalette::Wallpaper, 0.94f);
	MatWallpaperFaded = Build.Material(RoomPalette::WallpaperFaded, 0.94f);
	MatCeilingStain = Build.Material(RoomPalette::CeilingStain, 0.9f);
	MatRottenWood = Build.Material(RoomPalette::RottenWood, 0.98f);
	MatDarkWood = Build.Material(RoomPalette::DarkWood, 0.85f);
	MatMold = Build.Material(RoomPalette::Mold, 1.f);
	MatRust = Build.Material(RoomPalette::Rust, 0.95f, 0.6f);
	MatIron = Build.Material(RoomPalette::Iron, 0.7f, 0.9f);
	MatBrass = Build.Material(RoomPalette::Brass, 0.45f, 1.f);
	MatPaper = Build.Material(RoomPalette::Paper, 0.95f);
	MatPhoto = Build.Material(RoomPalette::Photo, 0.6f);
	MatGlass = Build.Material(RoomPalette::GlassShard, 0.2f);
	MatCloth = Build.Material(RoomPalette::Cloth, 0.99f);
	MatBlood = Build.Material(RoomPalette::DriedBlood, 0.95f);
	MatDust = Build.Material(RoomPalette::DustFilm, 1.f);
	MatWeb = Build.Material(FLinearColor(0.32f, 0.31f, 0.29f), 1.f);
	MatVoid = Build.Material(FLinearColor(0.004f, 0.004f, 0.004f), 1.f); // the black under a collapsed floor
}

bool ARoomDressingActor::IsFloorSpotClear(const FVector2D& Point, float Radius) const
{
	const float WidthHalf = Setup.Width * 0.5f;
	const float DepthHalf = Setup.Depth * 0.5f;

	// The player materialises at the room's centre and must not be standing inside a wardrobe.
	if (Point.SizeSquared() < FMath::Square(Radius + 70.f))
	{
		return false;
	}

	// Keep the doorway approach and the window bay walkable.
	if (FMath::Abs(Point.X) < Setup.DoorOpeningWidth * 0.5f + Radius && Point.Y < -DepthHalf + 90.f + Radius)
	{
		return false;
	}
	if (FMath::Abs(Point.Y) < Setup.WindowOpeningWidth * 0.5f && Point.X > WidthHalf - 70.f - Radius)
	{
		return false;
	}

	// Inside the walls.
	return FMath::Abs(Point.X) < WidthHalf - Setup.WallThickness - Radius
		&& FMath::Abs(Point.Y) < DepthHalf - Setup.WallThickness - Radius;
}

void ARoomDressingActor::BeginPlay()
{
	Super::BeginPlay();

	// Fixed seed: the layout is random in the sense that it was not hand-placed, but it is the
	// same room every single time the player wakes up in it.
	Random.Initialize(19551104);

	CacheMaterials();

	DustMotes->ConfigureVolume(
		FVector(Setup.Width * 0.46f, Setup.Depth * 0.46f, Setup.Height * 0.46f),
		FVector(0.f, 0.f, Setup.Height * 0.5f));

	BuildWalls();
	BuildFloor();
	BuildCeiling();
	BuildFurniture();
	BuildDebris();
	BuildTraces();
	BuildClues();
}

void ARoomDressingActor::BuildWalls()
{
	FRoomBuilder Build(this, DressingRoot);

	const float WidthHalf = Setup.Width * 0.5f;
	const float DepthHalf = Setup.Depth * 0.5f;
	const float FaceInset = Setup.WallThickness * 0.5f;

	// Places a flat panel on a wall's inner face, in wall-local coordinates: U runs along the
	// wall, V runs up it. Keeps every wall loop readable instead of drowning in axis swaps.
	auto WallPanel = [&](EWallSide Side, float U, float V, float SizeU, float SizeV, UMaterialInterface* Mat, float Tilt = 0.f) -> UStaticMeshComponent*
	{
		FVector Location;
		FRotator Rotation;
		FVector2D Size;

		switch (Side)
		{
		case EWallSide::North: // inner face points +Y
			Location = FVector(U, -DepthHalf + FaceInset, V);
			Rotation = FRotator(0.f, 0.f, -90.f);
			Size = FVector2D(SizeU, SizeV);
			break;
		case EWallSide::South:
			Location = FVector(U, DepthHalf - FaceInset, V);
			Rotation = FRotator(0.f, 0.f, 90.f);
			Size = FVector2D(SizeU, SizeV);
			break;
		case EWallSide::East: // inner face points -X
			Location = FVector(WidthHalf - FaceInset, U, V);
			Rotation = FRotator(90.f, 0.f, 0.f);
			Size = FVector2D(SizeV, SizeU);
			break;
		default: // West, inner face points +X
			Location = FVector(-WidthHalf + FaceInset, U, V);
			Rotation = FRotator(-90.f, 0.f, 0.f);
			Size = FVector2D(SizeV, SizeU);
			break;
		}

		// Tilt fakes a panel that has come away from the plaster at one edge.
		Rotation.Yaw += Tilt;
		return Build.Mark(Location, Rotation, Size, Mat);
	};

	// Openings that must stay clear on each wall, in that wall's U/V space.
	auto SpotBlocked = [&](EWallSide Side, float U, float V, float HalfU, float HalfV)
	{
		if (Side == EWallSide::North)
		{
			return FMath::Abs(U) < Setup.DoorOpeningWidth * 0.5f + HalfU && V < 215.f + HalfV;
		}
		if (Side == EWallSide::East)
		{
			return FMath::Abs(U) < Setup.WindowOpeningWidth * 0.5f + HalfU
				&& V > Setup.WindowSillHeight - HalfV && V < Setup.WindowTopHeight + HalfV;
		}
		return false;
	};

	const EWallSide Sides[4] = { EWallSide::North, EWallSide::South, EWallSide::East, EWallSide::West };

	for (const EWallSide Side : Sides)
	{
		const float WallLength = (Side == EWallSide::North || Side == EWallSide::South) ? Setup.Width : Setup.Depth;
		const float HalfLength = WallLength * 0.5f - Setup.WallThickness;

		// Layer 1: cracked plaster over the whole wall, so wherever the paper is gone there is
		// something underneath rather than the bare shell colour.
		WallPanel(Side, 0.f, Setup.Height * 0.5f, WallLength - Setup.WallThickness * 2.f, Setup.Height, MatPlaster);

		// Hairline cracks in the plaster.
		const int32 CrackCount = 5;
		for (int32 i = 0; i < CrackCount; ++i)
		{
			const float U = Random.FRandRange(-HalfLength, HalfLength);
			const float V = Random.FRandRange(40.f, Setup.Height - 40.f);
			if (SpotBlocked(Side, U, V, 40.f, 40.f))
			{
				continue;
			}
			WallPanel(Side, U, V, Random.FRandRange(1.5f, 3.f), Random.FRandRange(50.f, 160.f), MatCeilingStain, Random.FRandRange(-14.f, 14.f));
		}

		// Layer 2: wallpaper, in strips, with roughly a third of them torn away entirely.
		const float StripWidth = 52.f;
		const int32 StripCount = FMath::FloorToInt((HalfLength * 2.f) / StripWidth);
		for (int32 i = 0; i < StripCount; ++i)
		{
			const float U = -HalfLength + StripWidth * (i + 0.5f);

			if (Random.FRand() < 0.3f)
			{
				continue; // torn off long ago; bare plaster shows here
			}

			// Paper survives from the skirting up to a ragged line — damp comes from below and the
			// top of the wall is where it lets go first.
			const float TopV = Random.FRandRange(Setup.Height * 0.45f, Setup.Height * 0.98f);
			if (SpotBlocked(Side, U, TopV * 0.5f, StripWidth * 0.5f, TopV * 0.5f))
			{
				continue;
			}

			UMaterialInterface* PaperMat = Random.FRand() < 0.35f ? Cast<UMaterialInterface>(MatWallpaperFaded) : Cast<UMaterialInterface>(MatWallpaper);
			WallPanel(Side, U, TopV * 0.5f, StripWidth - 2.f, TopV, PaperMat);

			// Layer 3: a loose flap hanging off the top of some strips, which the wind moves.
			if (Random.FRand() < 0.4f)
			{
				USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("PeelPivot")));
				Pivot->SetMobility(EComponentMobility::Movable);
				Pivot->AttachToComponent(DressingRoot, FAttachmentTransformRules::KeepRelativeTransform);

				FVector PivotLocation;
				FRotator PivotRotation;
				switch (Side)
				{
				case EWallSide::North: PivotLocation = FVector(U, -DepthHalf + FaceInset + 1.f, TopV); PivotRotation = FRotator(0.f, 0.f, 0.f); break;
				case EWallSide::South: PivotLocation = FVector(U, DepthHalf - FaceInset - 1.f, TopV); PivotRotation = FRotator(0.f, 180.f, 0.f); break;
				case EWallSide::East:  PivotLocation = FVector(WidthHalf - FaceInset - 1.f, U, TopV); PivotRotation = FRotator(0.f, 270.f, 0.f); break;
				default:               PivotLocation = FVector(-WidthHalf + FaceInset + 1.f, U, TopV); PivotRotation = FRotator(0.f, 90.f, 0.f); break;
				}
				Pivot->SetRelativeLocationAndRotation(PivotLocation, PivotRotation);
				Pivot->RegisterComponent();
				AddInstanceComponent(Pivot);

				// Built hanging straight down from the pivot; the pivot's roll is what peels it
				// away from the wall each time a gust comes through.
				FRoomBuilder PeelBuild(this, Pivot);
				const float FlapLength = Random.FRandRange(30.f, 85.f);
				PeelBuild.Box(FVector(-1.f, 0.f, -FlapLength * 0.5f), FRotator::ZeroRotator, FVector(1.5f, StripWidth - 6.f, FlapLength), MatWallpaperFaded, /*bBlockingCollision*/ false);

				WindMovedParts.Add(Pivot);
				WindPartPhases.Add(Random.FRandRange(0.f, 100.f));
			}
		}

		// Damp corners: mold creeping up from the skirting.
		for (int32 i = 0; i < 2; ++i)
		{
			const float U = (i == 0 ? -1.f : 1.f) * Random.FRandRange(HalfLength * 0.72f, HalfLength * 0.95f);
			const float V = Random.FRandRange(25.f, 70.f);
			if (SpotBlocked(Side, U, V, 45.f, 45.f))
			{
				continue;
			}
			WallPanel(Side, U, V, Random.FRandRange(45.f, 95.f), V * 2.f, MatMold);
		}

		// Skirting board, half rotted off the wall.
		WallPanel(Side, 0.f, 9.f, WallLength - Setup.WallThickness * 2.f, 18.f, MatRottenWood);

		// A mouse hole at the base — chewed through where the skirting has gone soft.
		const float HoleU = Random.FRandRange(-HalfLength * 0.8f, HalfLength * 0.8f);
		if (!SpotBlocked(Side, HoleU, 6.f, 12.f, 12.f))
		{
			WallPanel(Side, HoleU, 6.f, Random.FRandRange(9.f, 14.f), Random.FRandRange(8.f, 12.f), MatVoid);
		}
	}

	// Cobwebs: sheets slung across the upper corners, plus loose strands that drift.
	const float CornerX[4] = { -WidthHalf, WidthHalf, -WidthHalf, WidthHalf };
	const float CornerY[4] = { -DepthHalf, -DepthHalf, DepthHalf, DepthHalf };
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const float SignX = FMath::Sign(CornerX[Corner]);
		const float SignY = FMath::Sign(CornerY[Corner]);

		for (int32 i = 0; i < 3; ++i)
		{
			const float Inset = 26.f + i * 20.f;
			const FVector Location(CornerX[Corner] - SignX * Inset, CornerY[Corner] - SignY * Inset, Setup.Height - 8.f - i * 6.f);
			// A web spans the corner diagonally, so the slab is rotated 45 degrees in plan.
			Build.Mark(Location, FRotator(0.f, SignX * SignY > 0.f ? 45.f : -45.f, 180.f), FVector2D(Inset * 1.6f, Inset * 1.6f), MatWeb);
		}

		// One hanging strand per corner, long enough to catch the lantern and move in the draught.
		USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("WebPivot")));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(DressingRoot, FAttachmentTransformRules::KeepRelativeTransform);
		Pivot->SetRelativeLocation(FVector(CornerX[Corner] - SignX * 55.f, CornerY[Corner] - SignY * 55.f, Setup.Height - 12.f));
		Pivot->RegisterComponent();
		AddInstanceComponent(Pivot);

		FRoomBuilder WebBuild(this, Pivot);
		const float StrandLength = Random.FRandRange(50.f, 130.f);
		WebBuild.Box(FVector(0.f, 0.f, -StrandLength * 0.5f), FRotator::ZeroRotator, FVector(1.f, 1.f, StrandLength), MatWeb, /*bBlockingCollision*/ false);

		WindMovedParts.Add(Pivot);
		WindPartPhases.Add(Random.FRandRange(0.f, 100.f));
	}
}

void ARoomDressingActor::BuildFloor()
{
	FRoomBuilder Build(this, DressingRoot);

	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness * 0.5f;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness * 0.5f;

	// Where the boards have given way entirely. Standing next to a hole you cannot see the bottom
	// of does more for the "this floor will not hold you" feeling than any amount of texture.
	const FVector2D CollapseCenter(-215.f, 165.f);
	const float CollapseRadius = 88.f;

	// Black under the missing boards, and two joists bridging the gap.
	Build.Box(FVector(CollapseCenter.X, CollapseCenter.Y, -26.f), FRotator::ZeroRotator, FVector(CollapseRadius * 2.2f, CollapseRadius * 2.2f, 24.f), MatVoid, /*bBlockingCollision*/ false);
	Build.Box(FVector(CollapseCenter.X - 30.f, CollapseCenter.Y, -12.f), FRotator(0.f, 0.f, 0.f), FVector(CollapseRadius * 2.4f, 14.f, 12.f), MatRottenWood);
	Build.Box(FVector(CollapseCenter.X + 46.f, CollapseCenter.Y, -10.f), FRotator(0.f, 6.f, 0.f), FVector(CollapseRadius * 2.4f, 12.f, 10.f), MatRottenWood);

	// Floorboards, laid in rows along X with a broken seam so the joints do not line up.
	const float BoardWidth = 21.f;
	const int32 RowCount = FMath::FloorToInt((DepthHalf * 2.f) / BoardWidth);

	for (int32 Row = 0; Row < RowCount; ++Row)
	{
		const float Y = -DepthHalf + BoardWidth * (Row + 0.5f);
		const float SeamX = Random.FRandRange(-WidthHalf * 0.5f, WidthHalf * 0.5f);

		const float Spans[2][2] = {
			{ -WidthHalf, SeamX },
			{ SeamX, WidthHalf }
		};

		for (int32 Span = 0; Span < 2; ++Span)
		{
			const float Start = Spans[Span][0];
			const float End = Spans[Span][1];
			const float Length = End - Start;
			if (Length < 20.f)
			{
				continue;
			}

			const FVector2D Center((Start + End) * 0.5f, Y);

			// Boards inside the collapse are simply not there.
			if (FVector2D::Distance(Center, CollapseCenter) < CollapseRadius)
			{
				continue;
			}
			// A few more are missing at random, leaving gaps you can see down into.
			if (Random.FRand() < 0.05f)
			{
				continue;
			}

			// Warped: each board sits at its own slight angle and height, which is what makes an
			// old floor read as rotten rather than merely brown.
			const float Warp = Random.FRandRange(-1.6f, 1.6f);
			const float Lift = Random.FRandRange(-1.2f, 2.4f);
			UStaticMeshComponent* Board = Build.Box(
				FVector(Center.X, Center.Y, Lift),
				FRotator(Random.FRandRange(-0.5f, 0.5f), 0.f, Warp),
				FVector(Length - 1.5f, BoardWidth - 1.5f, 6.f),
				Random.FRand() < 0.25f ? Cast<UMaterialInterface>(MatDarkWood) : Cast<UMaterialInterface>(MatRottenWood));

			// One board in twenty has lifted at an end — a loose board underfoot.
			if (Board && Random.FRand() < 0.05f)
			{
				Board->AddRelativeRotation(FRotator(Random.FRandRange(1.5f, 3.5f), 0.f, 0.f));
			}
		}
	}

	// A film of dust over the open middle of the floor, which is what the footprints later cut through.
	for (int32 i = 0; i < 14; ++i)
	{
		const FVector2D Spot(Random.FRandRange(-WidthHalf * 0.85f, WidthHalf * 0.85f), Random.FRandRange(-DepthHalf * 0.85f, DepthHalf * 0.85f));
		if (FVector2D::Distance(Spot, CollapseCenter) < CollapseRadius)
		{
			continue;
		}
		Build.Mark(FVector(Spot.X, Spot.Y, 4.f), FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f), FVector2D(Random.FRandRange(70.f, 190.f), Random.FRandRange(70.f, 190.f)), MatDust);
	}
}

void ARoomDressingActor::BuildCeiling()
{
	FRoomBuilder Build(this, DressingRoot);

	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness;
	const float CeilingZ = Setup.Height;

	// Exposed beams running across the room, where the plaster has come down.
	for (int32 i = 0; i < 4; ++i)
	{
		const float X = -WidthHalf + (WidthHalf * 2.f) * (i + 0.5f) / 4.f;
		Build.Box(FVector(X, 0.f, CeilingZ - 9.f), FRotator::ZeroRotator, FVector(24.f, DepthHalf * 2.f, 18.f), MatDarkWood);
	}

	// Water damage: overlapping rings of stain, darkest at the centre where it still gets wet.
	const FVector2D StainCenters[3] = { FVector2D(120.f, -80.f), FVector2D(-180.f, 130.f), FVector2D(250.f, 170.f) };
	for (const FVector2D& StainCenter : StainCenters)
	{
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const float Size = Random.FRandRange(70.f, 150.f) * (1.f + Ring * 0.55f);
			Build.Mark(
				FVector(StainCenter.X + Random.FRandRange(-20.f, 20.f), StainCenter.Y + Random.FRandRange(-20.f, 20.f), CeilingZ - 1.f),
				FRotator(0.f, Random.FRandRange(0.f, 360.f), 180.f),
				FVector2D(Size, Size * Random.FRandRange(0.7f, 1.3f)),
				Ring == 0 ? Cast<UMaterialInterface>(MatMold) : Cast<UMaterialInterface>(MatCeilingStain));
		}
	}

	// Cracked, flaking paint between the beams.
	for (int32 i = 0; i < 12; ++i)
	{
		Build.Mark(
			FVector(Random.FRandRange(-WidthHalf, WidthHalf), Random.FRandRange(-DepthHalf, DepthHalf), CeilingZ - 1.f),
			FRotator(0.f, Random.FRandRange(0.f, 360.f), 180.f),
			FVector2D(Random.FRandRange(20.f, 60.f), Random.FRandRange(3.f, 10.f)),
			MatPlaster);
	}

	// The dead bulb: a length of cord, a corroded fitting, and glass that has not carried current
	// in decades. Hung off-centre so it is not the first thing the player's eye lands on.
	const FVector BulbAnchor(-60.f, -40.f, CeilingZ);
	const float CordLength = 52.f;
	Build.Cyl(BulbAnchor + FVector(0.f, 0.f, -CordLength * 0.5f), FRotator(0.f, 0.f, 2.f), FVector(1.6f, 1.6f, CordLength), MatIron, /*bBlockingCollision*/ false);
	Build.Cyl(BulbAnchor + FVector(0.f, 0.f, -CordLength - 5.f), FRotator::ZeroRotator, FVector(9.f, 9.f, 12.f), MatRust, /*bBlockingCollision*/ false);
	// Dark, slightly blue glass — a bulb that is off, not a bulb that is glowing faintly.
	UMaterialInstanceDynamic* DeadGlass = Build.Material(FLinearColor(0.055f, 0.058f, 0.062f), 0.15f);
	Build.Sph(BulbAnchor + FVector(0.f, 0.f, -CordLength - 18.f), 20.f, DeadGlass);

	// The lantern hook — an iron hook screwed into a beam, worn bright where something hung from
	// it for years. A quiet piece of the house's history rather than a prop.
	const FVector HookAnchor(180.f, 60.f, CeilingZ - 18.f);
	Build.Cyl(HookAnchor + FVector(0.f, 0.f, -8.f), FRotator::ZeroRotator, FVector(2.4f, 2.4f, 16.f), MatIron, /*bBlockingCollision*/ false);
	Build.Cyl(HookAnchor + FVector(0.f, 5.f, -18.f), FRotator(0.f, 0.f, 75.f), FVector(2.2f, 2.2f, 14.f), MatIron, /*bBlockingCollision*/ false);

	// Water dripping from the worst of the stains into a puddle that never dries.
	DropOrigin = FVector(StainCenters[0].X, StainCenters[0].Y, CeilingZ - 6.f);
	DropStartZ = DropOrigin.Z;
	UMaterialInstanceDynamic* WaterMat = Build.Material(FLinearColor(0.055f, 0.070f, 0.080f), 0.08f);
	WaterDrop = Build.Sph(DropOrigin, 3.2f, WaterMat);
	Build.Mark(FVector(DropOrigin.X, DropOrigin.Y, 4.5f), FRotator::ZeroRotator, FVector2D(46.f, 38.f), WaterMat);
}

void ARoomDressingActor::BuildFurniture()
{
	FRoomBuilder Build(this, DressingRoot);

	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness;

	// A chest of drawers against the west wall. Two drawers hang half out — someone went through
	// this room looking for something, and did not put anything back.
	const FVector ChestLocation(-WidthHalf + 42.f, -110.f, 0.f);
	const FVector ChestSize(78.f, 140.f, 96.f);
	Build.Box(ChestLocation + FVector(0.f, 0.f, ChestSize.Z * 0.5f), FRotator::ZeroRotator, ChestSize, MatDarkWood);
	for (int32 i = 0; i < 3; ++i)
	{
		const float DrawerZ = 22.f + i * 28.f;
		const float PullOut = (i == 1) ? 34.f : (i == 2 ? 12.f : 0.f); // one hanging open, one nudged
		Build.Box(
			ChestLocation + FVector(ChestSize.X * 0.5f + PullOut * 0.5f, 0.f, DrawerZ),
			FRotator(0.f, 0.f, i == 1 ? 2.5f : 0.f),
			FVector(4.f + PullOut, ChestSize.Y - 14.f, 22.f),
			MatRottenWood);
		Build.Cyl(ChestLocation + FVector(ChestSize.X * 0.5f + PullOut + 2.f, 0.f, DrawerZ), FRotator(0.f, 90.f, 0.f), FVector(3.f, 3.f, 26.f), MatBrass, /*bBlockingCollision*/ false);
	}

	// A bookcase against the south wall that has lost a shelf and is leaning on what is left.
	const FVector CaseLocation(210.f, DepthHalf - 30.f, 0.f);
	Build.Box(CaseLocation + FVector(0.f, 0.f, 90.f), FRotator(0.f, 0.f, -3.f), FVector(150.f, 30.f, 180.f), MatDarkWood);
	for (int32 Shelf = 0; Shelf < 3; ++Shelf)
	{
		if (Shelf == 1)
		{
			continue; // the collapsed one
		}
		Build.Box(CaseLocation + FVector(0.f, -4.f, 40.f + Shelf * 55.f), FRotator(0.f, 0.f, -3.f), FVector(142.f, 26.f, 5.f), MatRottenWood);
	}
	// The fallen shelf, and the books that came down with it.
	Build.Box(CaseLocation + FVector(-20.f, -55.f, 8.f), FRotator(0.f, 22.f, 4.f), FVector(130.f, 24.f, 5.f), MatRottenWood);

	// A small table with a snapped leg, tipped onto its corner.
	const FVector TableLocation(-40.f, 205.f, 0.f);
	Build.Box(TableLocation + FVector(0.f, 0.f, 56.f), FRotator(3.f, 12.f, -7.f), FVector(110.f, 70.f, 6.f), MatDarkWood);
	const FVector2D LegOffsets[4] = { FVector2D(-46.f, -26.f), FVector2D(46.f, -26.f), FVector2D(-46.f, 26.f), FVector2D(46.f, 26.f) };
	for (int32 i = 0; i < 4; ++i)
	{
		if (i == 3)
		{
			// The broken leg, lying where it snapped off.
			Build.Cyl(TableLocation + FVector(70.f, 45.f, 4.f), FRotator(0.f, 0.f, 90.f), FVector(7.f, 7.f, 48.f), MatRottenWood);
			continue;
		}
		Build.Cyl(TableLocation + FVector(LegOffsets[i].X, LegOffsets[i].Y, 27.f), FRotator(3.f, 0.f, -7.f), FVector(8.f, 8.f, 54.f), MatRottenWood);
	}
}

void ARoomDressingActor::BuildDebris()
{
	FRoomBuilder Build(this, DressingRoot);

	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness;

	// Fallen plaster and splintered wood, thickest near the walls where the ceiling let go.
	for (int32 i = 0; i < 70; ++i)
	{
		const FVector2D Spot(Random.FRandRange(-WidthHalf, WidthHalf), Random.FRandRange(-DepthHalf, DepthHalf));
		if (!IsFloorSpotClear(Spot, 6.f))
		{
			continue;
		}

		const bool bPlaster = Random.FRand() < 0.55f;
		const float Size = Random.FRandRange(3.f, bPlaster ? 16.f : 9.f);
		Build.Box(
			FVector(Spot.X, Spot.Y, 5.f + Size * 0.3f),
			FRotator(Random.FRandRange(-20.f, 20.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-20.f, 20.f)),
			FVector(Size, Size * Random.FRandRange(0.4f, 1.f), Size * Random.FRandRange(0.2f, 0.5f)),
			bPlaster ? Cast<UMaterialInterface>(MatPlaster) : Cast<UMaterialInterface>(MatRottenWood),
			/*bBlockingCollision*/ false);
	}

	// Loose papers, blown into drifts against whatever stopped them.
	for (int32 i = 0; i < 24; ++i)
	{
		const FVector2D Spot(Random.FRandRange(-WidthHalf, WidthHalf), Random.FRandRange(-DepthHalf, DepthHalf));
		if (!IsFloorSpotClear(Spot, 10.f))
		{
			continue;
		}
		Build.Mark(FVector(Spot.X, Spot.Y, 5.f), FRotator(Random.FRandRange(-6.f, 6.f), Random.FRandRange(0.f, 360.f), 0.f), FVector2D(Random.FRandRange(16.f, 26.f), Random.FRandRange(20.f, 32.f)), MatPaper);
	}

	// Glass from the window, thrown inward across the boards below the sill.
	for (int32 i = 0; i < 20; ++i)
	{
		const float X = Random.FRandRange(WidthHalf - 150.f, WidthHalf - 20.f);
		const float Y = Random.FRandRange(-Setup.WindowOpeningWidth * 0.7f, Setup.WindowOpeningWidth * 0.7f);
		Build.Box(
			FVector(X, Y, 6.f),
			FRotator(Random.FRandRange(-14.f, 14.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-14.f, 14.f)),
			FVector(Random.FRandRange(4.f, 13.f), Random.FRandRange(3.f, 9.f), 1.f),
			MatGlass,
			/*bBlockingCollision*/ false);
	}

	// Books spilled from the fallen shelf.
	for (int32 i = 0; i < 9; ++i)
	{
		const FVector2D Spot(210.f + Random.FRandRange(-90.f, 90.f), DepthHalf - Random.FRandRange(40.f, 110.f));
		Build.Box(
			FVector(Spot.X, Spot.Y, 7.f + i * 0.6f),
			FRotator(Random.FRandRange(-8.f, 8.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-6.f, 6.f)),
			FVector(Random.FRandRange(18.f, 26.f), Random.FRandRange(13.f, 18.f), Random.FRandRange(4.f, 7.f)),
			Random.FRand() < 0.5f ? Cast<UMaterialInterface>(MatDarkWood) : Cast<UMaterialInterface>(MatCloth));
	}
}

void ARoomDressingActor::BuildTraces()
{
	FRoomBuilder Build(this, DressingRoot);

	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness;

	// Scratch marks gouged into the plaster beside the door, at roughly the height of a hand.
	// Left unexplained: the detective can see them, and nothing tells him what made them.
	for (int32 i = 0; i < 7; ++i)
	{
		const float X = -Setup.DoorOpeningWidth * 0.5f - 25.f - Random.FRandRange(0.f, 40.f);
		const float Z = 108.f + Random.FRandRange(-14.f, 14.f);
		Build.Mark(
			FVector(X, -DepthHalf - Setup.WallThickness * 0.5f + Setup.WallThickness, Z),
			FRotator(0.f, Random.FRandRange(-22.f, 22.f), -90.f),
			FVector2D(Random.FRandRange(16.f, 42.f), 1.6f),
			MatVoid);
	}

	// What is left of a bloodstain, soaked into the boards near the west wall. Decades of dust have
	// taken almost all of it; only the deepest part of the soak is still readable.
	const FVector2D BloodCenter(-WidthHalf + 120.f, 40.f);
	for (int32 i = 0; i < 5; ++i)
	{
		Build.Mark(
			FVector(BloodCenter.X + Random.FRandRange(-26.f, 26.f), BloodCenter.Y + Random.FRandRange(-26.f, 26.f), 5.f),
			FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
			FVector2D(Random.FRandRange(18.f, 54.f), Random.FRandRange(14.f, 44.f)),
			MatBlood);
	}

	// Footprints, pressed into decades of dust — and this is the part that does not sit right:
	// they start in the middle of the floor and stop at a blank wall. Nothing walked in, and
	// nothing walked out.
	const FVector2D TrailStart(70.f, 150.f);
	const FVector2D TrailEnd(-WidthHalf + 30.f, -60.f);
	const int32 StepCount = 9;
	for (int32 i = 0; i < StepCount; ++i)
	{
		const float Alpha = i / static_cast<float>(StepCount - 1);
		const FVector2D Along = FMath::Lerp(TrailStart, TrailEnd, Alpha);
		const FVector2D Direction = (TrailEnd - TrailStart).GetSafeNormal();
		const FVector2D Side(-Direction.Y, Direction.X);
		// Alternating left and right of the line of travel, like an actual gait.
		const FVector2D Spot = Along + Side * ((i % 2 == 0) ? 11.f : -11.f);

		Build.Mark(
			FVector(Spot.X, Spot.Y, 5.2f),
			FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)), 0.f),
			FVector2D(27.f, 11.f),
			MatVoid);
	}
}

AClueActor* ARoomDressingActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AClueActor* Clue = GetWorld()->SpawnActor<AClueActor>(AClueActor::StaticClass(), GetActorLocation() + LocalLocation, Rotation, SpawnParams);
	if (Clue)
	{
		Clue->Configure(FText::FromString(ShortName), FText::FromString(Description));
	}
	return Clue;
}

void ARoomDressingActor::BuildClues()
{
	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness;

	// The overturned chair. Lying on its side in open floor, which is the read: it went over
	// backwards and nobody ever picked it up.
	if (AClueActor* Chair = SpawnClue(FVector(95.f, -95.f, 0.f), FRotator(0.f, 34.f, 0.f), TEXT("Examine the chair"),
		TEXT("A kitchen chair, on its side. The dust has settled evenly over it — it went over a very long time ago.")))
	{
		FRoomBuilder ChairBuild(Chair, Chair->GetRootScene());
		// Built upright, then tipped as a whole so the parts stay consistent with each other.
		ChairBuild.Box(FVector(0.f, 0.f, 44.f), FRotator(90.f, 0.f, 0.f), FVector(46.f, 46.f, 5.f), MatDarkWood);
		ChairBuild.Box(FVector(-20.f, 0.f, 20.f), FRotator(90.f, 0.f, 0.f), FVector(48.f, 42.f, 5.f), MatRottenWood);
		for (int32 i = 0; i < 4; ++i)
		{
			const float LegX = (i < 2) ? -18.f : 18.f;
			const float LegY = (i % 2 == 0) ? -18.f : 18.f;
			ChairBuild.Cyl(FVector(LegX + 22.f, LegY, 22.f), FRotator(90.f, 0.f, 0.f), FVector(5.f, 5.f, 44.f), MatRottenWood);
		}
	}

	// A photograph on top of the chest of drawers. Face up, so whoever left it was looking at it.
	if (AClueActor* Photograph = SpawnClue(FVector(-Setup.Width * 0.5f + Setup.WallThickness + 42.f, -110.f, 97.f), FRotator(0.f, 12.f, 0.f), TEXT("Examine the photograph"),
		TEXT("A woman and two children on a doorstep, squinting into the sun. Somebody wiped the dust off this one. Recently is impossible. But somebody did.")))
	{
		FRoomBuilder PhotoBuild(Photograph, Photograph->GetRootScene());
		PhotoBuild.Box(FVector(0.f, 0.f, 1.f), FRotator::ZeroRotator, FVector(28.f, 36.f, 1.2f), MatPhoto);
		PhotoBuild.Mark(FVector(0.f, 0.f, 1.8f), FRotator::ZeroRotator, FVector2D(22.f, 30.f), MatPaper);
	}

	// A letter on the floor near the window, the ink half gone where the rain has reached it.
	if (AClueActor* Letter = SpawnClue(FVector(WidthHalf - 95.f, 105.f, 5.f), FRotator(0.f, 61.f, 0.f), TEXT("Examine the letter"),
		TEXT("Handwriting, pressed hard into the paper. The rain has taken most of it. What is left reads: '...you would have loved the house in summer.'")))
	{
		FRoomBuilder LetterBuild(Letter, Letter->GetRootScene());
		LetterBuild.Box(FVector(0.f, 0.f, 0.6f), FRotator::ZeroRotator, FVector(24.f, 30.f, 1.2f), MatPaper);
		LetterBuild.Box(FVector(6.f, 8.f, 1.6f), FRotator(0.f, 24.f, 0.f), FVector(20.f, 26.f, 1.f), MatPaper, /*bBlockingCollision*/ false);
	}

	// A picture frame that came off the wall, glass-side down.
	if (AClueActor* Frame = SpawnClue(FVector(-150.f, -DepthHalf + 60.f, 5.f), FRotator(0.f, -18.f, 0.f), TEXT("Examine the broken frame"),
		TEXT("The frame is face down in its own glass. Turning it over takes nothing: the picture inside has been removed. The backing pins are bent outward — it was opened in a hurry.")))
	{
		FRoomBuilder FrameBuild(Frame, Frame->GetRootScene());
		FrameBuild.Box(FVector(0.f, 0.f, 2.f), FRotator(0.f, 0.f, 4.f), FVector(42.f, 34.f, 4.f), MatDarkWood);
		for (int32 i = 0; i < 6; ++i)
		{
			FrameBuild.Box(
				FVector(Random.FRandRange(-40.f, 40.f), Random.FRandRange(-34.f, 34.f), 1.f),
				FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
				FVector(Random.FRandRange(5.f, 14.f), Random.FRandRange(4.f, 10.f), 1.f),
				MatGlass,
				/*bBlockingCollision*/ false);
		}
	}

	// The clock, still on the wall, stopped. The time it stopped at means nothing yet.
	if (AClueActor* Clock = SpawnClue(FVector(-40.f, DepthHalf + Setup.WallThickness * 0.5f - 4.f, 215.f), FRotator::ZeroRotator, TEXT("Examine the clock"),
		TEXT("A wall clock, stopped. The glass is starred where something struck it. Not a power cut, then — somebody stopped it, and there is no knowing what hour that was.")))
	{
		FRoomBuilder ClockBuild(Clock, Clock->GetRootScene());
		ClockBuild.Cyl(FVector(0.f, 6.f, 0.f), FRotator(90.f, 0.f, 0.f), FVector(52.f, 52.f, 10.f), MatDarkWood);
		ClockBuild.Cyl(FVector(0.f, 0.f, 0.f), FRotator(90.f, 0.f, 0.f), FVector(42.f, 42.f, 3.f), MatPaper, /*bBlockingCollision*/ false);
		// Hands, frozen where they died.
		ClockBuild.Box(FVector(0.f, -3.f, 9.f), FRotator(0.f, 0.f, 0.f), FVector(2.f, 2.f, 18.f), MatIron, /*bBlockingCollision*/ false);
		ClockBuild.Box(FVector(7.f, -3.f, 4.f), FRotator(0.f, 0.f, 62.f), FVector(2.f, 2.f, 13.f), MatIron, /*bBlockingCollision*/ false);
	}

	// Rusted tools spilled out of a box by the door. Somebody was working on this room.
	if (AClueActor* Tools = SpawnClue(FVector(Setup.DoorOpeningWidth * 0.5f + 55.f, -DepthHalf + 45.f, 5.f), FRotator(0.f, 0.f, 0.f), TEXT("Examine the tools"),
		TEXT("A claw hammer and a handful of bent nails, rusted into one another. They are on this side of the door. Whatever was being fixed, it was being fixed from in here.")))
	{
		FRoomBuilder ToolBuild(Tools, Tools->GetRootScene());
		ToolBuild.Box(FVector(0.f, 0.f, 3.f), FRotator(0.f, 18.f, 0.f), FVector(52.f, 16.f, 6.f), MatRust);
		ToolBuild.Cyl(FVector(-18.f, 4.f, 6.f), FRotator(0.f, 0.f, 90.f), FVector(6.f, 6.f, 34.f), MatDarkWood, /*bBlockingCollision*/ false);
		for (int32 i = 0; i < 7; ++i)
		{
			ToolBuild.Cyl(
				FVector(Random.FRandRange(-30.f, 30.f), Random.FRandRange(-16.f, 16.f), 2.f),
				FRotator(0.f, Random.FRandRange(0.f, 360.f), 90.f),
				FVector(1.6f, 1.6f, Random.FRandRange(8.f, 15.f)),
				MatRust,
				/*bBlockingCollision*/ false);
		}
	}

	// Bottles in the corner, lined up rather than thrown. Someone sat here and drank, methodically.
	if (AClueActor* Bottles = SpawnClue(FVector(WidthHalf - 60.f, -DepthHalf + 70.f, 0.f), FRotator(0.f, 0.f, 0.f), TEXT("Examine the bottles"),
		TEXT("Six empty bottles, stood up in a row against the skirting. Not thrown, not knocked over. Placed. Somebody spent a great many evenings in this room alone.")))
	{
		FRoomBuilder BottleBuild(Bottles, Bottles->GetRootScene());
		for (int32 i = 0; i < 6; ++i)
		{
			const FVector Base(Random.FRandRange(-6.f, 6.f), -40.f + i * 16.f, 0.f);
			const bool bFallen = (i == 4);
			BottleBuild.Cyl(Base + FVector(0.f, 0.f, bFallen ? 6.f : 14.f), bFallen ? FRotator(0.f, 20.f, 90.f) : FRotator::ZeroRotator, FVector(11.f, 11.f, 28.f), MatGlass);
			BottleBuild.Cyl(Base + FVector(0.f, 0.f, bFallen ? 6.f : 33.f), bFallen ? FRotator(0.f, 20.f, 90.f) : FRotator::ZeroRotator, FVector(5.f, 5.f, 14.f), MatGlass, /*bBlockingCollision*/ false);
		}
	}

	// A stack of books nobody has opened in decades, beside the collapsed shelf.
	if (AClueActor* Books = SpawnClue(FVector(150.f, DepthHalf - 95.f, 5.f), FRotator(0.f, 27.f, 0.f), TEXT("Examine the books"),
		TEXT("Ledgers, not novels — columns of dates and figures in the same tight hand as the letter. The dust on top is thick enough to write in. Nobody has.")))
	{
		FRoomBuilder BookBuild(Books, Books->GetRootScene());
		for (int32 i = 0; i < 4; ++i)
		{
			BookBuild.Box(
				FVector(Random.FRandRange(-4.f, 4.f), Random.FRandRange(-4.f, 4.f), 3.f + i * 6.f),
				FRotator(0.f, Random.FRandRange(-14.f, 14.f), 0.f),
				FVector(24.f, 17.f, 6.f),
				i % 2 == 0 ? Cast<UMaterialInterface>(MatDarkWood) : Cast<UMaterialInterface>(MatCloth));
		}
		BookBuild.Mark(FVector(0.f, 0.f, 27.f), FRotator::ZeroRotator, FVector2D(24.f, 17.f), MatDust);
	}

	// The half-open drawer, which is a clue in its own right rather than only furniture.
	SpawnClue(FVector(-Setup.Width * 0.5f + Setup.WallThickness + 82.f, -110.f, 50.f), FRotator::ZeroRotator, TEXT("Search the drawer"),
		TEXT("Empty, except for the shape of what used to be in it, printed in the dust. Something flat and rectangular. A frame, or a photograph."));

	// The footprint trail. The prompt sits over the last print, where the trail simply stops.
	if (AClueActor* Footprints = SpawnClue(FVector(-Setup.Width * 0.5f + Setup.WallThickness + 40.f, -60.f, 6.f), FRotator::ZeroRotator, TEXT("Examine the footprints"),
		TEXT("Bare feet, pressed into dust that has not been disturbed in decades. They cross the room and stop here, at the wall. There is no set going the other way.")))
	{
		FRoomBuilder PrintBuild(Footprints, Footprints->GetRootScene());
		// Invisible collision volume over the last prints: the trail itself is painted on the floor,
		// and a clue still needs something solid for the interaction trace to find.
		UStaticMeshComponent* Volume = PrintBuild.Box(FVector(0.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(60.f, 60.f, 12.f), nullptr);
		if (Volume)
		{
			Volume->SetVisibility(false);
		}
	}

	// The scratch marks by the door.
	if (AClueActor* Scratches = SpawnClue(FVector(-Setup.DoorOpeningWidth * 0.5f - 45.f, -DepthHalf - Setup.WallThickness * 0.5f + 12.f, 108.f), FRotator::ZeroRotator, TEXT("Examine the marks"),
		TEXT("Grooves cut into the plaster, four of them, side by side. They are at the height of a man's hand, and they run toward the door.")))
	{
		FRoomBuilder ScratchBuild(Scratches, Scratches->GetRootScene());
		UStaticMeshComponent* Volume = ScratchBuild.Box(FVector(0.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(70.f, 10.f, 60.f), nullptr);
		if (Volume)
		{
			Volume->SetVisibility(false);
		}
	}

	// The stain on the boards.
	if (AClueActor* Stain = SpawnClue(FVector(-Setup.Width * 0.5f + Setup.WallThickness + 120.f, 40.f, 6.f), FRotator::ZeroRotator, TEXT("Examine the stain"),
		TEXT("Dark, soaked deep into the grain, spread the way a pool spreads rather than a splash. Old enough that it has stopped being red. Not old enough to mean nothing.")))
	{
		FRoomBuilder StainBuild(Stain, Stain->GetRootScene());
		UStaticMeshComponent* Volume = StainBuild.Box(FVector(0.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(90.f, 90.f, 12.f), nullptr);
		if (Volume)
		{
			Volume->SetVisibility(false);
		}
	}
}

void ARoomDressingActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedTime += DeltaTime;

	const float Gust = Storm.IsValid() ? Storm->GetWindGust() : 0.f;
	DustMotes->SetWindStrength(Gust);

	// Loose wallpaper and cobweb strands lift on the same gust that moves the curtains, so the
	// whole room breathes together when the wind comes through the broken panes.
	for (int32 i = 0; i < WindMovedParts.Num(); ++i)
	{
		USceneComponent* Part = WindMovedParts[i];
		if (!Part)
		{
			continue;
		}

		const float Phase = WindPartPhases.IsValidIndex(i) ? WindPartPhases[i] : 0.f;
		const float Flutter = FMath::PerlinNoise1D(ElapsedTime * 1.6f + Phase) * 0.5f + 0.5f;
		const float Lift = FMath::Lerp(1.f, 22.f, Gust) * FMath::Lerp(0.4f, 1.f, Flutter);
		Part->SetRelativeRotation(FRotator(0.f, Part->GetRelativeRotation().Yaw, Lift));
	}

	// The drip. Constant period, so it becomes the room's metronome — the one sound-shaped thing
	// in here that is reliable.
	if (WaterDrop)
	{
		const float FallHeight = DropStartZ - 6.f;
		DropFallTime += DeltaTime;

		// Free fall at a tenth of g: real gravity crosses three metres too fast to register as a
		// drip, and the read here is rhythm, not physics.
		const float Fallen = 0.5f * 490.f * DropFallTime * DropFallTime;
		if (Fallen >= FallHeight)
		{
			DropFallTime = 0.f;
			WaterDrop->SetRelativeLocation(DropOrigin);
		}
		else
		{
			WaterDrop->SetRelativeLocation(FVector(DropOrigin.X, DropOrigin.Y, DropStartZ - Fallen));
		}
	}
}
