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
		North,
		South, // the door wall, at +Y
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

void ARoomDressingActor::CacheMaterials(FRoomBuilder& Build)
{
	// Tints sit at or below 1.0 and only ever darken: the photographs already carry the colour, so
	// a tint above 1 would invent light that is not in the texture. Variants of the same set are
	// how one texture covers several states — paper that still holds its colour, and paper the
	// damp has bleached.
	// Every tint here is a darkening. Poly Haven shoots its library evenly lit and clean, which
	// means a plaster wall arrives at roughly half albedo — daylight-bright in a room whose only
	// honest light source is a flame. Held at around a fifth, the same photographs read as a
	// house that has been shut for decades, and the lantern has somewhere to be the brightest
	// thing in frame.
	MatPlaster = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.44f, 0.42f, 0.38f));
	MatWallpaper = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.66f, 0.63f, 0.58f));
	MatWallpaperFaded = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.42f, 0.40f, 0.36f));
	MatPlasterDark = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.17f, 0.16f, 0.14f));
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.38f, 0.37f, 0.34f));
	MatFloorboards = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.70f, 0.67f, 0.62f));
	MatFloorboardsWorn = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.44f, 0.40f, 0.36f));
	MatRoughWood = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.80f, 0.76f, 0.72f));
	MatPlankWood = Build.Surface(RoomSurfaces::PlankWall, FLinearColor(0.72f, 0.68f, 0.63f));
	MatCloth = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.50f, 0.48f, 0.45f));
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.60f, 0.60f, 0.62f));
	MatRust = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.85f, 0.70f, 0.55f));

	MatMold = Build.Flat(RoomPalette::Mold, 1.f);
	MatPaper = Build.Flat(RoomPalette::Paper, 0.95f);
	MatPhoto = Build.Flat(RoomPalette::Photo, 0.6f);
	MatGlass = Build.Flat(RoomPalette::GlassShard, 0.2f);
	MatBlood = Build.Flat(RoomPalette::DriedBlood, 0.95f);
	MatDust = Build.Flat(RoomPalette::DustFilm, 1.f);
	MatWeb = Build.Flat(RoomPalette::Web, 1.f);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatWater = Build.Flat(RoomPalette::Water, 0.08f);
}

bool ARoomDressingActor::IsFloorSpotClear(const FVector2D& Point, float Radius) const
{
	const float WidthHalf = Setup.Width * 0.5f;
	const float DepthHalf = Setup.Depth * 0.5f;

	// The detective comes round on the floor in the corner and must not do so inside a wardrobe,
	// nor with a crate in front of his face — the first thing he sees has to be the room.
	if (FVector2D::DistSquared(Point, Setup.WakeSpot) < FMath::Square(Radius + 95.f))
	{
		return false;
	}

	// Keep the doorway approach and the window bay walkable.
	if (FMath::Abs(Point.X - Setup.DoorOpeningCenterX) < Setup.DoorOpeningWidth * 0.5f + Radius && Point.Y > DepthHalf - 90.f - Radius)
	{
		return false;
	}
	if (FMath::Abs(Point.Y) < Setup.WindowOpeningWidth * 0.5f && Point.X > WidthHalf - 70.f - Radius)
	{
		return false;
	}

	return FMath::Abs(Point.X) < WidthHalf - Setup.WallThickness - Radius
		&& FMath::Abs(Point.Y) < DepthHalf - Setup.WallThickness - Radius;
}

void ARoomDressingActor::BeginPlay()
{
	Super::BeginPlay();

	// Fixed seed: the layout is random in the sense that it was not hand-placed, but it is the
	// same room every single time the player wakes up in it.
	Random.Initialize(19551104);

	FRoomBuilder Build(this, DressingRoot);
	CacheMaterials(Build);

	DustMotes->ConfigureVolume(
		FVector(Setup.Width * 0.46f, Setup.Depth * 0.46f, Setup.Height * 0.46f),
		FVector(0.f, 0.f, Setup.Height * 0.5f));

	BuildWalls(Build);
	BuildFloor(Build);
	BuildCeiling(Build);
	BuildFurniture(Build);
	BuildDebris(Build);
	BuildTraces(Build);
	BuildClues();
}

void ARoomDressingActor::BuildWalls(FRoomBuilder& Build)
{
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

		Rotation.Yaw += Tilt; // fakes a panel that has come away from the plaster at one edge
		return Build.Mark(Location, Rotation, Size, Mat);
	};

	// The hole in each wall, in that wall's own U/V space: the doorway on the south wall, the
	// window on the east. CenterU/HalfU along the wall, BottomV/TopV up it.
	struct FWallOpening
	{
		bool bExists = false;
		float CenterU = 0.f;
		float HalfU = 0.f;
		float BottomV = 0.f;
		float TopV = 0.f;
	};

	auto OpeningOn = [&](EWallSide Side) -> FWallOpening
	{
		if (Side == EWallSide::South)
		{
			return FWallOpening{ true, Setup.DoorOpeningCenterX, Setup.DoorOpeningWidth * 0.5f, -20.f, 208.f };
		}
		if (Side == EWallSide::East)
		{
			return FWallOpening{ true, 0.f, Setup.WindowOpeningWidth * 0.5f, Setup.WindowSillHeight, Setup.WindowTopHeight };
		}
		return FWallOpening{};
	};

	// Openings that must stay clear on each wall, in that wall's U/V space.
	auto SpotBlocked = [&](EWallSide Side, float U, float V, float HalfU, float HalfV)
	{
		const FWallOpening Opening = OpeningOn(Side);
		return Opening.bExists
			&& FMath::Abs(U - Opening.CenterU) < Opening.HalfU + HalfU
			&& V > Opening.BottomV - HalfV && V < Opening.TopV + HalfV;
	};

	// A panel that cuts itself around the wall's opening, emitting the pieces left over.
	//
	// The full-wall plaster below and the skirting board both run the entire length of their wall,
	// and either one laid straight across a hole bricks it up. That is exactly what was happening:
	// a single slab of plaster over the east wall had sealed the window, and another had sealed
	// the doorway, so the room had neither — the storm could not get in and the brightest thing
	// in the frame was a brown rectangle where the view should have been.
	auto WallFill = [&](EWallSide Side, float U, float V, float SizeU, float SizeV, UMaterialInterface* Mat)
	{
		const FWallOpening Opening = OpeningOn(Side);
		const float Left = U - SizeU * 0.5f;
		const float Right = U + SizeU * 0.5f;
		const float Bottom = V - SizeV * 0.5f;
		const float Top = V + SizeV * 0.5f;

		const bool bOverlaps = Opening.bExists
			&& Opening.CenterU - Opening.HalfU < Right && Opening.CenterU + Opening.HalfU > Left
			&& Opening.BottomV < Top && Opening.TopV > Bottom;

		if (!bOverlaps)
		{
			WallPanel(Side, U, V, SizeU, SizeV, Mat);
			return;
		}

		const float HoleLeft = FMath::Max(Left, Opening.CenterU - Opening.HalfU);
		const float HoleRight = FMath::Min(Right, Opening.CenterU + Opening.HalfU);
		const float HoleBottom = FMath::Max(Bottom, Opening.BottomV);
		const float HoleTop = FMath::Min(Top, Opening.TopV);

		if (HoleLeft > Left)
		{
			WallPanel(Side, (Left + HoleLeft) * 0.5f, V, HoleLeft - Left, SizeV, Mat);
		}
		if (Right > HoleRight)
		{
			WallPanel(Side, (HoleRight + Right) * 0.5f, V, Right - HoleRight, SizeV, Mat);
		}
		if (HoleBottom > Bottom)
		{
			WallPanel(Side, (HoleLeft + HoleRight) * 0.5f, (Bottom + HoleBottom) * 0.5f, HoleRight - HoleLeft, HoleBottom - Bottom, Mat);
		}
		if (Top > HoleTop)
		{
			WallPanel(Side, (HoleLeft + HoleRight) * 0.5f, (HoleTop + Top) * 0.5f, HoleRight - HoleLeft, Top - HoleTop, Mat);
		}
	};

	const EWallSide Sides[4] = { EWallSide::North, EWallSide::South, EWallSide::East, EWallSide::West };

	for (const EWallSide Side : Sides)
	{
		const float WallLength = (Side == EWallSide::North || Side == EWallSide::South) ? Setup.Width : Setup.Depth;
		const float HalfLength = WallLength * 0.5f - Setup.WallThickness;

		// Layer 1: plaster over the whole wall, so wherever the paper is gone there is a real
		// surface underneath rather than the bare shell colour.
		WallFill(Side, 0.f, Setup.Height * 0.5f, WallLength - Setup.WallThickness * 2.f, Setup.Height, MatPlaster);

		// Hairline cracks.
		for (int32 i = 0; i < 5; ++i)
		{
			const float U = Random.FRandRange(-HalfLength, HalfLength);
			const float V = Random.FRandRange(40.f, Setup.Height - 40.f);
			if (SpotBlocked(Side, U, V, 40.f, 40.f))
			{
				continue;
			}
			WallPanel(Side, U, V, Random.FRandRange(1.5f, 3.f), Random.FRandRange(50.f, 160.f), MatVoid, Random.FRandRange(-14.f, 14.f));
		}

		// A piece of covering that has let go along its top edge and hangs away from the wall.
		//
		// This is the whole trick to a wall that reads as *peeling* rather than as patterned: a
		// flat panel of any colour is still a flat wall, but an edge standing a few centimetres
		// proud catches the lantern along its lip and throws a hard shadow behind it. So each one
		// gets a pivot at the tear line and is built hanging from it — the pivot's roll is how far
		// it has curled away, and for some of them the wind keeps moving it.
		auto AddPeel = [&](EWallSide Side, float U, float V, float Width, float Length, float Curl, bool bWindMoved, UMaterialInterface* Mat)
		{
			USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("PeelPivot")));
			Pivot->SetMobility(EComponentMobility::Movable);
			Pivot->AttachToComponent(DressingRoot, FAttachmentTransformRules::KeepRelativeTransform);

			FVector PivotLocation;
			FRotator PivotRotation;
			switch (Side)
			{
			case EWallSide::North: PivotLocation = FVector(U, -DepthHalf + FaceInset + 1.f, V); PivotRotation = FRotator(0.f, 0.f, 0.f); break;
			case EWallSide::South: PivotLocation = FVector(U, DepthHalf - FaceInset - 1.f, V); PivotRotation = FRotator(0.f, 180.f, 0.f); break;
			case EWallSide::East:  PivotLocation = FVector(WidthHalf - FaceInset - 1.f, U, V); PivotRotation = FRotator(0.f, 270.f, 0.f); break;
			default:               PivotLocation = FVector(-WidthHalf + FaceInset + 1.f, U, V); PivotRotation = FRotator(0.f, 90.f, 0.f); break;
			}
			PivotRotation.Roll = Curl;
			Pivot->SetRelativeLocationAndRotation(PivotLocation, PivotRotation);
			Pivot->RegisterComponent();
			AddInstanceComponent(Pivot);

			FRoomBuilder PeelBuild(this, Pivot);
			PeelBuild.Box(FVector(-1.f, 0.f, -Length * 0.5f), FRotator::ZeroRotator, FVector(1.4f, Width, Length), Mat, /*bBlockingCollision*/ false);

			if (bWindMoved)
			{
				WindMovedParts.Add(Pivot);
				WindPartPhases.Add(Random.FRandRange(0.f, 100.f));
			}
		};

		// Layer 2: wallpaper, in strips, with getting on for half of them torn away entirely.
		const float StripWidth = 52.f;
		const int32 StripCount = FMath::FloorToInt((HalfLength * 2.f) / StripWidth);
		for (int32 i = 0; i < StripCount; ++i)
		{
			const float U = -HalfLength + StripWidth * (i + 0.5f);

			if (Random.FRand() < 0.42f)
			{
				continue; // torn off long ago; bare plaster shows here
			}

			// Paper survives from the skirting up to a ragged line — damp comes from below, and the
			// top of the wall is where it lets go first.
			const float TopV = Random.FRandRange(Setup.Height * 0.4f, Setup.Height * 0.98f);
			if (SpotBlocked(Side, U, TopV * 0.5f, StripWidth * 0.5f, TopV * 0.5f))
			{
				continue;
			}

			UMaterialInterface* PaperMat = Random.FRand() < 0.4f ? Cast<UMaterialInterface>(MatWallpaperFaded) : Cast<UMaterialInterface>(MatWallpaper);

			// The strip is not cut off level. A single panel gives a dead straight horizontal edge
			// that reads as wallpaper *hung* to that height; three teeth of differing height read
			// as paper that tore.
			const int32 Teeth = 3;
			const float ToothWidth = (StripWidth - 2.f) / Teeth;
			for (int32 Tooth = 0; Tooth < Teeth; ++Tooth)
			{
				const float ToothTop = TopV * Random.FRandRange(0.78f, 1.04f);
				WallPanel(Side, U - (StripWidth - 2.f) * 0.5f + ToothWidth * (Tooth + 0.5f), ToothTop * 0.5f, ToothWidth - 0.6f, ToothTop, PaperMat);
			}

			// Layer 3: what is hanging off the tear. Most strips have something; about half of
			// those are loose enough for the draught from the broken pane to keep moving them,
			// and the rest have curled where they dried and stayed there.
			if (Random.FRand() < 0.72f)
			{
				const bool bWindMoved = Random.FRand() < 0.5f;
				AddPeel(Side, U + Random.FRandRange(-8.f, 8.f), TopV * Random.FRandRange(0.82f, 0.98f),
					Random.FRandRange(StripWidth * 0.4f, StripWidth - 6.f),
					Random.FRandRange(26.f, 92.f),
					bWindMoved ? Random.FRandRange(4.f, 12.f) : Random.FRandRange(16.f, 46.f),
					bWindMoved,
					MatWallpaperFaded);
			}

			// A corner lifting halfway up the strip — smaller, tighter curls, the kind that start
			// at a seam. These are what fill the wall between the big tears.
			if (Random.FRand() < 0.55f)
			{
				AddPeel(Side, U + Random.FRandRange(-16.f, 16.f), Random.FRandRange(TopV * 0.25f, TopV * 0.8f),
					Random.FRandRange(9.f, 22.f), Random.FRandRange(8.f, 26.f),
					Random.FRandRange(22.f, 58.f), /*bWindMoved*/ false, PaperMat);
			}
		}

		// Flakes of the paint under the paper, lifting off the bare plaster. Small, and there are
		// a lot of them: individually they are nothing, together they are the difference between a
		// wall that is dirty and a wall that is coming apart.
		for (int32 Flake = 0; Flake < 16; ++Flake)
		{
			const float FlakeU = Random.FRandRange(-HalfLength, HalfLength);
			// Biased low and high — damp rises from the floor and comes down from the ceiling, and
			// the middle of a wall is the last part to go.
			const float FlakeV = Random.FRand() < 0.55f
				? Random.FRandRange(22.f, Setup.Height * 0.4f)
				: Random.FRandRange(Setup.Height * 0.62f, Setup.Height - 18.f);

			if (SpotBlocked(Side, FlakeU, FlakeV, 20.f, 20.f))
			{
				continue;
			}

			AddPeel(Side, FlakeU, FlakeV, Random.FRandRange(5.f, 17.f), Random.FRandRange(5.f, 19.f),
				Random.FRandRange(18.f, 64.f), /*bWindMoved*/ false,
				Random.FRand() < 0.5f ? Cast<UMaterialInterface>(MatPlaster) : Cast<UMaterialInterface>(MatWallpaperFaded));
		}

		// What came off, on the floor under where it came off from.
		//
		// The wall being torn is only half the story — a room where the covering has peeled for
		// decades has the pieces lying at the foot of the wall, because nothing has been in here
		// to sweep them up. They are also what makes the peeling read as having *happened* rather
		// than as a texture: the floor is the evidence.
		for (int32 Piece = 0; Piece < 7; ++Piece)
		{
			const float PieceU = Random.FRandRange(-HalfLength, HalfLength);
			const float Distance = Random.FRandRange(9.f, 58.f);

			// Nothing across the threshold — the door has to be able to swing — and nothing on top
			// of the detective while he is still lying there.
			if (SpotBlocked(Side, PieceU, 6.f, 30.f, 6.f))
			{
				continue;
			}

			FVector2D Spot;
			float AlongWallYaw = 0.f;
			switch (Side)
			{
			case EWallSide::North: Spot = FVector2D(PieceU, -DepthHalf + FaceInset + Distance); AlongWallYaw = 0.f; break;
			case EWallSide::South: Spot = FVector2D(PieceU, DepthHalf - FaceInset - Distance); AlongWallYaw = 0.f; break;
			case EWallSide::East:  Spot = FVector2D(WidthHalf - FaceInset - Distance, PieceU); AlongWallYaw = 90.f; break;
			default:               Spot = FVector2D(-WidthHalf + FaceInset + Distance, PieceU); AlongWallYaw = 90.f; break;
			}

			if (FVector2D::DistSquared(Spot, Setup.WakeSpot) < FMath::Square(90.f))
			{
				continue;
			}

			UMaterialInterface* PieceMat = Random.FRand() < 0.55f
				? Cast<UMaterialInterface>(MatWallpaperFaded)
				: Cast<UMaterialInterface>(Random.FRand() < 0.5f ? MatPlaster : MatPlasterDark);

			const float Length = Random.FRandRange(14.f, 46.f);
			const float Width = Random.FRandRange(8.f, 26.f);
			const float Yaw = AlongWallYaw + Random.FRandRange(-38.f, 38.f);

			if (Random.FRand() < 0.3f)
			{
				// Still leaning where it slid down the wall and stopped, one edge on the boards
				// and the rest of it against the skirting.
				const float Lean = Random.FRandRange(58.f, 78.f);
				Build.Box(
					FVector(Spot.X, Spot.Y, 6.f + FMath::Cos(FMath::DegreesToRadians(Lean)) * Length * 0.5f),
					FRotator(Side == EWallSide::East || Side == EWallSide::West ? Lean : 0.f, Yaw,
						Side == EWallSide::East || Side == EWallSide::West ? 0.f : Lean),
					FVector(Width, 1.4f, Length),
					PieceMat,
					/*bBlockingCollision*/ false);
				continue;
			}

			// Lying flat, with the far end curled up off the boards the way dried paper does —
			// that lifted edge is the only part of it the lantern will actually catch.
			Build.Box(
				FVector(Spot.X, Spot.Y, 5.6f),
				FRotator(Random.FRandRange(-4.f, 4.f), Yaw, Random.FRandRange(-4.f, 4.f)),
				FVector(Length, Width, 1.2f),
				PieceMat,
				/*bBlockingCollision*/ false);

			if (Random.FRand() < 0.65f)
			{
				const float CurlLength = Length * Random.FRandRange(0.28f, 0.5f);
				const float CurlAngle = Random.FRandRange(22.f, 55.f);
				const float Radians = FMath::DegreesToRadians(Yaw);
				const float Offset = (Length - CurlLength) * 0.5f;
				Build.Box(
					FVector(
						Spot.X + FMath::Cos(Radians) * Offset,
						Spot.Y + FMath::Sin(Radians) * Offset,
						5.6f + FMath::Sin(FMath::DegreesToRadians(CurlAngle)) * CurlLength * 0.5f),
					FRotator(CurlAngle, Yaw, 0.f),
					FVector(CurlLength, Width * Random.FRandRange(0.8f, 1.f), 1.2f),
					PieceMat,
					/*bBlockingCollision*/ false);
			}
		}

		// And what is behind all of it: plaster the damp has blackened, showing wherever something
		// has come away. Without this the exposed areas are all one clean colour and the peeling
		// has nothing to have exposed.
		for (int32 Patch = 0; Patch < 6; ++Patch)
		{
			const float PatchU = Random.FRandRange(-HalfLength, HalfLength);
			const float PatchV = Random.FRandRange(30.f, Setup.Height - 30.f);
			if (SpotBlocked(Side, PatchU, PatchV, 50.f, 50.f))
			{
				continue;
			}
			WallPanel(Side, PatchU, PatchV, Random.FRandRange(30.f, 90.f), Random.FRandRange(34.f, 110.f), MatPlasterDark, Random.FRandRange(-5.f, 5.f));
		}

		// Blotches. The wallpaper strips above give the wall a vertical grain, and a wall that is
		// *only* striped reads as wallpaper rather than as ruin — these are the irregular patches
		// where whole sheets have come away in one piece and taken the top of the plaster with
		// them, which is what breaks the banding up.
		for (int32 Blotch = 0; Blotch < 4; ++Blotch)
		{
			const float BlotchU = Random.FRandRange(-HalfLength, HalfLength);
			const float BlotchV = Random.FRandRange(50.f, Setup.Height - 30.f);
			if (SpotBlocked(Side, BlotchU, BlotchV, 70.f, 70.f))
			{
				continue;
			}

			// Built as a clutch of overlapping slabs rather than one rectangle: the edge of the
			// cluster is ragged, and a torn edge is the whole point of the effect.
			const int32 Pieces = Random.RandRange(3, 6);
			for (int32 Piece = 0; Piece < Pieces; ++Piece)
			{
				WallPanel(Side,
					BlotchU + Random.FRandRange(-38.f, 38.f),
					BlotchV + Random.FRandRange(-34.f, 34.f),
					Random.FRandRange(26.f, 78.f),
					Random.FRandRange(24.f, 70.f),
					Random.FRand() < 0.3f ? Cast<UMaterialInterface>(MatMold) : Cast<UMaterialInterface>(MatPlasterDark),
					Random.FRandRange(-6.f, 6.f));
			}
		}

		// The ceiling line, where the damp has come down the wall from above and blackened it.
		for (int32 i = 0; i < 4; ++i)
		{
			const float StainU = Random.FRandRange(-HalfLength, HalfLength);
			const float StainHeight = Random.FRandRange(26.f, 70.f);
			WallPanel(Side, StainU, Setup.Height - StainHeight * 0.5f, Random.FRandRange(60.f, 170.f), StainHeight, MatMold);
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

		// Skirting board, half rotted off the wall. Cut around the doorway: skirting does not run
		// across a threshold.
		WallFill(Side, 0.f, 9.f, WallLength - Setup.WallThickness * 2.f, 18.f, MatPlankWood);

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

void ARoomDressingActor::BuildFloor(FRoomBuilder& Build)
{
	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness * 0.5f;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness * 0.5f;

	// Where the boards have given way entirely. Standing next to a hole you cannot see the bottom
	// of does more for the "this floor will not hold you" feeling than any amount of texture.
	// Placed deliberately in the middle-right of the waking view, between the detective and the
	// door: the first thing he has to solve is not the lock, it is how to cross his own floor.
	const FVector2D CollapseCenter(60.f, 90.f);
	const float CollapseRadius = 88.f;

	Build.Box(FVector(CollapseCenter.X, CollapseCenter.Y, -26.f), FRotator::ZeroRotator, FVector(CollapseRadius * 2.2f, CollapseRadius * 2.2f, 24.f), MatVoid, /*bBlockingCollision*/ false);
	Build.Box(FVector(CollapseCenter.X - 30.f, CollapseCenter.Y, -12.f), FRotator::ZeroRotator, FVector(CollapseRadius * 2.4f, 14.f, 12.f), MatRoughWood);
	Build.Box(FVector(CollapseCenter.X + 46.f, CollapseCenter.Y, -10.f), FRotator(0.f, 6.f, 0.f), FVector(CollapseRadius * 2.4f, 12.f, 10.f), MatRoughWood);

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

			if (FVector2D::Distance(Center, CollapseCenter) < CollapseRadius)
			{
				continue; // boards inside the collapse are simply not there
			}
			if (Random.FRand() < 0.05f)
			{
				continue; // a few more missing at random, leaving gaps you can see down into
			}

			// Warped: each board sits at its own slight angle and height, which is what makes an
			// old floor read as rotten rather than merely brown.
			const float Warp = Random.FRandRange(-1.6f, 1.6f);
			const float Lift = Random.FRandRange(-1.2f, 2.4f);
			UStaticMeshComponent* Board = Build.Box(
				FVector(Center.X, Center.Y, Lift),
				FRotator(Random.FRandRange(-0.5f, 0.5f), 0.f, Warp),
				FVector(Length - 1.5f, BoardWidth - 1.5f, 6.f),
				Random.FRand() < 0.3f ? Cast<UMaterialInterface>(MatFloorboardsWorn) : Cast<UMaterialInterface>(MatFloorboards));

			// One board in twenty has lifted at an end — a loose board underfoot.
			if (Board && Random.FRand() < 0.05f)
			{
				Board->AddRelativeRotation(FRotator(Random.FRandRange(1.5f, 3.5f), 0.f, 0.f));
			}
		}
	}

	BuildPuddles(Build);

	// Drifts of dust, in the corners and along the skirting where it actually collects. These used
	// to be metre-and-a-half slabs laid over the open floor, which under a lantern read as sheets
	// of paper dropped everywhere rather than as dust — the boards' own texture already carries
	// the grime, so what is left here is only the build-up at the edges of the room.
	for (int32 i = 0; i < 10; ++i)
	{
		const bool bAlongX = Random.FRand() < 0.5f;
		const float Edge = Random.FRand() < 0.5f ? -1.f : 1.f;
		const FVector2D Spot(
			bAlongX ? Random.FRandRange(-WidthHalf * 0.8f, WidthHalf * 0.8f) : Edge * Random.FRandRange(WidthHalf * 0.72f, WidthHalf * 0.94f),
			bAlongX ? Edge * Random.FRandRange(DepthHalf * 0.72f, DepthHalf * 0.94f) : Random.FRandRange(-DepthHalf * 0.8f, DepthHalf * 0.8f));

		if (FVector2D::Distance(Spot, CollapseCenter) < CollapseRadius)
		{
			continue;
		}

		Build.Mark(FVector(Spot.X, Spot.Y, 4.f), FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
			FVector2D(Random.FRandRange(40.f, 90.f), Random.FRandRange(26.f, 54.f)), MatDust);
	}
}

void ARoomDressingActor::BuildPuddles(FRoomBuilder& Build)
{
	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness * 0.5f;

	// Standing water, at a roughness the rest of the room never goes near. It earns its place
	// twice over: it says the roof is open somewhere above, and it is the only thing down here
	// that reflects — the window, the lightning and the lantern all come back off it.
	const FVector2D Pools[4] = {
		FVector2D(-10.f, 60.f),                  // spreading out of the collapsed boards
		FVector2D(115.f, 150.f),
		FVector2D(WidthHalf - 95.f, 30.f),       // blown in through the broken panes
		FVector2D(WidthHalf - 150.f, -85.f)
	};

	for (const FVector2D& Pool : Pools)
	{
		const int32 Lobes = Random.RandRange(3, 6);
		for (int32 i = 0; i < Lobes; ++i)
		{
			// A pool is not an ellipse; it is a handful of overlapping lobes that found the low
			// spots between warped boards.
			Build.Mark(
				FVector(Pool.X + Random.FRandRange(-34.f, 34.f), Pool.Y + Random.FRandRange(-30.f, 30.f), 5.2f),
				FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
				FVector2D(Random.FRandRange(26.f, 62.f), Random.FRandRange(20.f, 48.f)),
				MatWater);
		}
	}
}

void ARoomDressingActor::BuildCeiling(FRoomBuilder& Build)
{
	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness;
	const float CeilingZ = Setup.Height;

	// The ceiling surface itself, then the beams that show where the plaster has come down.
	Build.Mark(FVector(0.f, 0.f, CeilingZ - 1.f), FRotator(0.f, 0.f, 180.f), FVector2D(WidthHalf * 2.f, DepthHalf * 2.f), MatCeiling);

	for (int32 i = 0; i < 4; ++i)
	{
		const float X = -WidthHalf + (WidthHalf * 2.f) * (i + 0.5f) / 4.f;
		Build.Box(FVector(X, 0.f, CeilingZ - 9.f), FRotator::ZeroRotator, FVector(24.f, DepthHalf * 2.f, 18.f), MatRoughWood);
	}

	// Water damage: overlapping rings of stain, darkest at the centre where it still gets wet.
	const FVector2D StainCenters[3] = { FVector2D(120.f, -80.f), FVector2D(-180.f, 130.f), FVector2D(250.f, 170.f) };
	for (const FVector2D& StainCenter : StainCenters)
	{
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const float Size = Random.FRandRange(70.f, 150.f) * (1.f + Ring * 0.55f);
			Build.Mark(
				FVector(StainCenter.X + Random.FRandRange(-20.f, 20.f), StainCenter.Y + Random.FRandRange(-20.f, 20.f), CeilingZ - 2.f),
				FRotator(0.f, Random.FRandRange(0.f, 360.f), 180.f),
				FVector2D(Size, Size * Random.FRandRange(0.7f, 1.3f)),
				Ring == 0 ? Cast<UMaterialInterface>(MatMold) : Cast<UMaterialInterface>(MatCeiling));
		}
	}

	// The dead bulb: a length of cord, a corroded fitting, and glass that has not carried current
	// in decades. Hung off-centre so it is not the first thing the player's eye lands on.
	const FVector BulbAnchor(-60.f, -40.f, CeilingZ);
	const float CordLength = 52.f;
	Build.Cyl(BulbAnchor + FVector(0.f, 0.f, -CordLength * 0.5f), FRotator(0.f, 0.f, 2.f), FVector(1.6f, 1.6f, CordLength), MatIron, /*bBlockingCollision*/ false);
	Build.Cyl(BulbAnchor + FVector(0.f, 0.f, -CordLength - 5.f), FRotator::ZeroRotator, FVector(9.f, 9.f, 12.f), MatRust, /*bBlockingCollision*/ false);
	// Dark, dirty glass — a bulb that is off, not a bulb glowing faintly. The roughness matters as
	// much as the colour: at 0.15 the sphere caught the lantern in one hot specular dot and became
	// the second brightest thing in the room, which is not what a dead bulb does.
	Build.Sph(BulbAnchor + FVector(0.f, 0.f, -CordLength - 18.f), 20.f, Build.Flat(FLinearColor(0.030f, 0.032f, 0.035f), 0.62f));

	// The lantern hook — iron screwed into a beam, worn where something hung from it for years.
	const FVector HookAnchor(180.f, 60.f, CeilingZ - 18.f);
	Build.Cyl(HookAnchor + FVector(0.f, 0.f, -8.f), FRotator::ZeroRotator, FVector(2.4f, 2.4f, 16.f), MatIron, /*bBlockingCollision*/ false);
	Build.Cyl(HookAnchor + FVector(0.f, 5.f, -18.f), FRotator(0.f, 0.f, 75.f), FVector(2.2f, 2.2f, 14.f), MatIron, /*bBlockingCollision*/ false);

	// Water dripping from the worst of the stains into a puddle that never dries.
	DropOrigin = FVector(StainCenters[0].X, StainCenters[0].Y, CeilingZ - 6.f);
	DropStartZ = DropOrigin.Z;
	WaterDrop = Build.Sph(DropOrigin, 3.2f, MatWater);
	Build.Mark(FVector(DropOrigin.X, DropOrigin.Y, 4.5f), FRotator::ZeroRotator, FVector2D(46.f, 38.f), MatWater);
}

void ARoomDressingActor::BuildFurniture(FRoomBuilder& Build)
{
	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness;

	// The room is composed for one view: the detective's, from where he wakes. Everything below is
	// placed for how it falls in that frame — a wardrobe filling the left edge at a raking angle,
	// a bookcase further down the same side facing him square on, the window between them with the
	// table under it, and the locked door off to the right. It has to survive being walked around
	// afterwards, but it is built to be *first seen* from one spot.

	// The wardrobe, against the wall he wakes up beside. Close enough to the eye that it reads as
	// a dark mass before it reads as furniture, and turned a few degrees off square — nobody ever
	// pushed a wardrobe back against a wall and got it parallel.
	Build.Prop(RoomProps::Cabinet, FVector(74.f, -DepthHalf + 30.f, 0.f), FRotator(0.f, 6.f, 0.f), 176.f);

	// The bookcase on the window wall, left of the opening, facing straight back down the room.
	// It leans: one of its shelves has gone, and it has taken the carcass with it.
	Build.Prop(RoomProps::Bookshelf, FVector(WidthHalf - 24.f, -232.f, 0.f), FRotator(0.f, 92.f, -2.f), 192.f);
	Build.Prop(RoomProps::ShelfBooks, FVector(WidthHalf - 32.f, -248.f, 124.f), FRotator(0.f, 84.f, 0.f), 26.f, /*bBlockingCollision*/ false);
	Build.Prop(RoomProps::ShelfBooks, FVector(WidthHalf - 32.f, -212.f, 66.f), FRotator(0.f, 107.f, 0.f), 24.f, /*bBlockingCollision*/ false);
	// A small frame stood on one of the shelves rather than hung — the one thing in this room
	// somebody chose to keep where they would see it.
	Build.Prop(RoomProps::PictureFrame, FVector(WidthHalf - 30.f, -196.f, 96.f), FRotator(0.f, 96.f, 4.f), 26.f, /*bBlockingCollision*/ false);

	// A couple of books that came off the shelf and were never picked up.
	Build.Prop(RoomProps::LooseBooks, FVector(WidthHalf - 78.f, -186.f, 4.f), FRotator(0.f, 27.f, 0.f), 14.f, /*bBlockingCollision*/ false);

	// The table under the window — the one surface the storm lights for free, and so the one the
	// eye goes to after the lantern.
	Build.Prop(RoomProps::Table, FVector(WidthHalf - 34.f, 18.f, 0.f), FRotator(0.f, 92.f, 0.f), 74.f);

	// Papers left on it, gone soft with damp and stuck down where the rain has reached them.
	for (int32 i = 0; i < 5; ++i)
	{
		Build.Mark(
			FVector(WidthHalf - 34.f + Random.FRandRange(-12.f, 12.f), 18.f + Random.FRandRange(-26.f, 26.f), 75.f),
			FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
			FVector2D(Random.FRandRange(18.f, 27.f), Random.FRandRange(22.f, 31.f)),
			MatPaper);
	}

	// A crate shoved into the far corner behind him, out of the opening shot: it is there for the
	// walk back, not for the wake-up.
	Build.Prop(RoomProps::Crate, FVector(-WidthHalf + 62.f, DepthHalf - 74.f, 0.f), FRotator(0.f, 34.f, 0.f), 46.f);
}

void ARoomDressingActor::BuildDebris(FRoomBuilder& Build)
{
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
			bPlaster ? Cast<UMaterialInterface>(MatPlaster) : Cast<UMaterialInterface>(MatRoughWood),
			/*bBlockingCollision*/ false);
	}

	// Loose papers, blown into drifts against whatever stopped them.
	for (int32 i = 0; i < 11; ++i)
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
}

void ARoomDressingActor::BuildTraces(FRoomBuilder& Build)
{
	const float WidthHalf = Setup.Width * 0.5f - Setup.WallThickness;
	const float DepthHalf = Setup.Depth * 0.5f - Setup.WallThickness;

	// Scratch marks gouged into the plaster beside the door, at roughly the height of a hand.
	// Left unexplained: the detective can see them, and nothing tells him what made them.
	for (int32 i = 0; i < 7; ++i)
	{
		const float X = Setup.DoorOpeningCenterX - Setup.DoorOpeningWidth * 0.5f - 25.f - Random.FRandRange(0.f, 40.f);
		const float Z = 108.f + Random.FRandRange(-14.f, 14.f);
		Build.Mark(
			FVector(X, DepthHalf - Setup.WallThickness * 0.5f, Z),
			FRotator(0.f, Random.FRandRange(-22.f, 22.f), 90.f),
			FVector2D(Random.FRandRange(16.f, 42.f), 1.6f),
			MatVoid);
	}

	// What is left of a bloodstain, soaked into the boards near the west wall. Decades of dust
	// have taken almost all of it; only the deepest part of the soak is still readable.
	const FVector2D BloodCenter(-WidthHalf + 150.f, 55.f);
	for (int32 i = 0; i < 5; ++i)
	{
		Build.Mark(
			FVector(BloodCenter.X + Random.FRandRange(-26.f, 26.f), BloodCenter.Y + Random.FRandRange(-26.f, 26.f), 5.f),
			FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
			FVector2D(Random.FRandRange(18.f, 54.f), Random.FRandRange(14.f, 44.f)),
			MatBlood);
	}

	// Footprints, pressed into decades of dust — and this is the part that does not sit right:
	// they cross the floor and stop at a blank wall. Nothing walked in, and nothing walked out.
	const FVector2D TrailStart(120.f, 120.f);
	const FVector2D TrailEnd(-WidthHalf + 30.f, 170.f);
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

	// The overturned chair, lying on its side in open floor: it went over backwards and nobody
	// ever picked it up.
	if (AClueActor* Chair = SpawnClue(FVector(-30.f, 120.f, 0.f), FRotator(0.f, 34.f, 0.f), TEXT("Examine the chair"),
		TEXT("A kitchen chair, on its side. The dust has settled evenly over it — it went over a very long time ago.")))
	{
		FRoomBuilder ChairBuild(Chair, Chair->GetRootScene());
		// Tipped onto its back: rolled 88 degrees and lifted so it rests on the floor rather than
		// sinking half-through it.
		ChairBuild.Prop(RoomProps::Chair, FVector(0.f, 0.f, 24.f), FRotator(0.f, 0.f, 88.f), 92.f);
	}

	// A photograph on the cabinet. Face up, so whoever left it was looking at it.
	if (AClueActor* Photograph = SpawnClue(FVector(68.f, -DepthHalf + 36.f, 178.f), FRotator(0.f, 96.f, 0.f), TEXT("Examine the photograph"),
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
	if (AClueActor* Frame = SpawnClue(FVector(-95.f, 45.f, 4.f), FRotator(0.f, -18.f, 0.f), TEXT("Examine the broken frame"),
		TEXT("The frame is face down in its own glass. Turning it over takes nothing: the picture inside has been removed. The backing pins are bent outward — it was opened in a hurry.")))
	{
		FRoomBuilder FrameBuild(Frame, Frame->GetRootScene());
		FrameBuild.Prop(RoomProps::PictureFrame, FVector(0.f, 0.f, 2.f), FRotator(88.f, 22.f, 0.f), 38.f);
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
	if (AClueActor* Clock = SpawnClue(FVector(-40.f, DepthHalf + Setup.WallThickness * 0.5f - 6.f, 215.f), FRotator(0.f, 180.f, 0.f), TEXT("Examine the clock"),
		TEXT("A wall clock, stopped. The glass is starred where something struck it. Not a power cut, then — somebody stopped it, and there is no knowing what hour that was.")))
	{
		FRoomBuilder ClockBuild(Clock, Clock->GetRootScene());
		ClockBuild.Prop(RoomProps::WallClock, FVector::ZeroVector, FRotator::ZeroRotator, 44.f);
	}

	// Rusted tools spilled out of a box by the door. Somebody was working on this room.
	if (AClueActor* Tools = SpawnClue(FVector(Setup.DoorOpeningCenterX - 105.f, DepthHalf - 45.f, 5.f), FRotator::ZeroRotator, TEXT("Examine the tools"),
		TEXT("A claw hammer and a handful of bent nails, rusted into one another. They are on this side of the door. Whatever was being fixed, it was being fixed from in here.")))
	{
		FRoomBuilder ToolBuild(Tools, Tools->GetRootScene());
		ToolBuild.Box(FVector(0.f, 0.f, 3.f), FRotator(0.f, 18.f, 0.f), FVector(52.f, 16.f, 6.f), MatRust);
		ToolBuild.Cyl(FVector(-18.f, 4.f, 6.f), FRotator(0.f, 0.f, 90.f), FVector(6.f, 6.f, 34.f), MatRoughWood, /*bBlockingCollision*/ false);
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
	if (AClueActor* Bottles = SpawnClue(FVector(WidthHalf - 60.f, -DepthHalf + 74.f, 0.f), FRotator::ZeroRotator, TEXT("Examine the bottles"),
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
	if (AClueActor* Books = SpawnClue(FVector(WidthHalf - 78.f, -186.f, 4.f), FRotator(0.f, 27.f, 0.f), TEXT("Examine the books"),
		TEXT("Ledgers, not novels — columns of dates and figures in the same tight hand as the letter. The dust on top is thick enough to write in. Nobody has.")))
	{
		FRoomBuilder BookBuild(Books, Books->GetRootScene());
		BookBuild.Prop(RoomProps::LooseBooks, FVector::ZeroVector, FRotator(0.f, 14.f, 0.f), 16.f);
	}

	// The half-open drawer, a clue in its own right rather than only furniture.
	SpawnClue(FVector(68.f, -DepthHalf + 76.f, 62.f), FRotator::ZeroRotator, TEXT("Search the drawer"),
		TEXT("Empty, except for the shape of what used to be in it, printed in the dust. Something flat and rectangular. A frame, or a photograph."));

	// The footprint trail. The prompt sits over the last print, where the trail simply stops.
	if (AClueActor* Footprints = SpawnClue(FVector(-WidthHalf + 40.f, 170.f, 6.f), FRotator::ZeroRotator, TEXT("Examine the footprints"),
		TEXT("Bare feet, pressed into dust that has not been disturbed in decades. They cross the room and stop here, at the wall. There is no set going the other way.")))
	{
		FRoomBuilder PrintBuild(Footprints, Footprints->GetRootScene());
		// Invisible collision volume over the last prints: the trail itself is painted on the
		// floor, and a clue still needs something solid for the interaction trace to find.
		if (UStaticMeshComponent* Volume = PrintBuild.Box(FVector(0.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(60.f, 60.f, 12.f), nullptr))
		{
			Volume->SetVisibility(false);
		}
	}

	// The scratch marks by the door.
	if (AClueActor* Scratches = SpawnClue(FVector(Setup.DoorOpeningCenterX - Setup.DoorOpeningWidth * 0.5f - 45.f, DepthHalf + Setup.WallThickness * 0.5f - 12.f, 108.f), FRotator::ZeroRotator, TEXT("Examine the marks"),
		TEXT("Grooves cut into the plaster, four of them, side by side. They are at the height of a man's hand, and they run toward the door.")))
	{
		FRoomBuilder ScratchBuild(Scratches, Scratches->GetRootScene());
		if (UStaticMeshComponent* Volume = ScratchBuild.Box(FVector::ZeroVector, FRotator::ZeroRotator, FVector(70.f, 10.f, 60.f), nullptr))
		{
			Volume->SetVisibility(false);
		}
	}

	// The stain on the boards.
	if (AClueActor* Stain = SpawnClue(FVector(-WidthHalf + 150.f, 55.f, 6.f), FRotator::ZeroRotator, TEXT("Examine the stain"),
		TEXT("Dark, soaked deep into the grain, spread the way a pool spreads rather than a splash. Old enough that it has stopped being red. Not old enough to mean nothing.")))
	{
		FRoomBuilder StainBuild(Stain, Stain->GetRootScene());
		if (UStaticMeshComponent* Volume = StainBuild.Box(FVector(0.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(90.f, 90.f, 12.f), nullptr))
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

		// Free fall at a fifth of g: real gravity crosses three metres too fast to register as a
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
