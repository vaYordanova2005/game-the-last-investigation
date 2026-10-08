#include "CellarActor.h"
#include "RoomBuildLibrary.h"
#include "RoomDressingActor.h"
#include "HallDoorActor.h"
#include "StormWindowActor.h" // RoomDressingActor.h's inline SetStorm needs the complete type
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	/** Collision that only a walking man meets: the interaction trace and the camera pass through. */
	void CellarPawnOnly(UStaticMeshComponent* Part)
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

	/** Height of the handrail over the pitch line. */
	constexpr float CellarRailHeight = 88.f;
}

ACellarActor::ACellarActor()
{
	// Ticks only to tell the storm whether the player is down here (see IsUnderground).
	PrimaryActorTick.bCanEverTick = true;

	CellarRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CellarRoot"));
	SetRootComponent(CellarRoot);
	CellarRoot->SetMobility(EComponentMobility::Movable);
}

void ACellarActor::Configure(const FCellarSetup& InSetup)
{
	Setup = InSetup;
}

TArray<ACellarActor::FCellarRoom> ACellarActor::Rooms() const
{
	// West from the foot of the stair. The bedroom is the first door on the right, placed so its
	// opening is where Room01's door is along its wall; the other rooms are laid out from it, a
	// brick's length apart, and the doors on the left are staggered against the ones on the right
	// so no door looks straight into another. The wine cellar is the room that was S2, widened
	// west and deepened south: its east wall where S2's was, its door near its own east end.
	const float BedCentreX = BedroomDoorX() - DoorOpeningCenterX;
	const float BedWestOuter = BedCentreX - (RoomWidth + WallThickness) * 0.5f;
	const float NorthTwoX = BedWestOuter - 10.f - (600.f + WallThickness) * 0.5f;
	const float SouthOneX = FootX() - 42.f - (640.f + WallThickness) * 0.5f;
	const float WineX = SouthOneX - (640.f + WallThickness) * 0.5f - 10.f - (AWineCellarActor::Width + WallThickness) * 0.5f;
	FCellarRoom Wine{ WineX, AWineCellarActor::Width, AWineCellarActor::Depth, false, WineX + AWineCellarActor::DoorX, false, 6303 };
	Wine.bWine = true;
	return {
		{ BedCentreX, RoomWidth, RoomDepth, true, BedroomDoorX(), true, 6000 },
		{ NorthTwoX, 600.f, 500.f, true, NorthTwoX - 110.f, false, 6101 },
		{ SouthOneX, 640.f, 540.f, false, SouthOneX - 100.f, false, 6202 },
		Wine,
	};
}

FVector ACellarActor::RoomCentre(const FCellarRoom& Room) const
{
	// The door wall's corridor face is the corridor's own face, north or south.
	const float Y = Room.bNorth
		? Setup.ShaftNorthY - WallThickness * 0.5f - Room.Depth * 0.5f
		: Setup.ShaftSouthY + WallThickness * 0.5f + Room.Depth * 0.5f;
	return FVector(Room.CentreX, Y, FloorZ());
}

float ACellarActor::PassageWestX() const
{
	// The wine cellar runs on west under the ground past where the corridor stops, so only its
	// door decides how far the corridor has to go for it.
	float West = BedroomDoorX();
	for (const FCellarRoom& Room : Rooms())
	{
		West = FMath::Min(West, Room.bWine ? Room.DoorX - DoorHalf(Room) - 60.f : Room.CentreX - (Room.Width + WallThickness) * 0.5f);
	}
	return West - 30.f;
}

bool ACellarActor::DecalHitsDoorway(TConstArrayView<FCellarRoom> Candidates, bool bNorthWall, float X, float HalfAlong, float Bottom) const
{
	// A decal projects straight through: one that reaches a doorway smears down the jambs, and over
	// the leaf as it swings, while standing still on it. A hand's width of margin is for the leaf
	// at the hinge jamb, which is inside the projection's reach as soon as it opens.
	const float Margin = 15.f;
	for (const FCellarRoom& Room : Candidates)
	{
		if (Room.bNorth == bNorthWall
			&& FMath::Abs(X - Room.DoorX) < DoorHalf(Room) + HalfAlong + Margin
			&& Bottom < FloorZ() + DoorHeight(Room) + Margin)
		{
			return true;
		}
	}
	return false;
}

void ACellarActor::BeginPlay()
{
	Super::BeginPlay();

	FRoomBuilder Build(this, CellarRoot);
	CacheMaterials(Build);
	BuildStairWell(Build);
	BuildStair(Build);
	for (const FCellarRoom& Room : Rooms())
	{
		if (Room.bWine)
		{
			SpawnWineCellar(Room);
		}
		else
		{
			BuildRoomShell(Build, Room);
			if (Room.bBedroom)
			{
				SpawnBedroom(Room);
				BuildMaidsCorner(Build, Room);
			}
			else
			{
				BuildBareRoom(Build, Room);
			}
		}
		SpawnDoor(Room);
	}
}

bool ACellarActor::IsUnderground(const FVector& LocalPoint) const
{
	// Up in the well, between the door and the far end of the hole in the hall floor: the strip
	// behind the end wall, and nowhere in the hall (the statue's alcove is south of it).
	if (LocalPoint.X < Setup.DoorWallX && LocalPoint.X > Setup.WellWestX
		&& LocalPoint.Y > Setup.ShaftNorthY && LocalPoint.Y < Setup.ShaftSouthY
		&& LocalPoint.Z < Setup.GroundZ + 300.f)
	{
		return true;
	}
	// Anywhere below the hall's floor slab is the cellar: nothing else in the house is down there.
	return LocalPoint.Z < Setup.GroundZ - 30.f && LocalPoint.X < Setup.DoorWallX;
}

void ACellarActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		const FVector Eye = GetActorTransform().InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation());
		AStormWindowActor::SetViewUnderground(IsUnderground(Eye));
	}
}

void ACellarActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AStormWindowActor::SetViewUnderground(false);
	Super::EndPlay(EndPlayReason);
}

void ACellarActor::CacheMaterials(FRoomBuilder& Build)
{
	// The well is bare brick: nobody plastered the way down to a cellar. Darker than the house's
	// exposed brick upstairs, because nothing down here has ever been lit by anything but a lamp
	// somebody carried.
	MatBrick = Build.Surface(RoomSurfaces::Substrate, FLinearColor(0.150f, 0.130f, 0.112f));
	MatTread = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.40f, 0.36f, 0.32f));
	MatTimber = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.290f, 0.276f, 0.258f));
	MatFloor = Build.Surface(RoomSurfaces::Stone, FLinearColor(0.30f, 0.27f, 0.24f));
	// green_metal_rust needs its corrected tint or it comes out green paint (see the dressing).
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatRubble = Build.Surface(RoomSurfaces::Substrate, FLinearColor(0.120f, 0.104f, 0.090f));

	// The maid's things. Straw and twig are the rough-wood photograph, whose grain stretched along
	// a broom head reads as fibre; the straw gone the grey-ochre of anything kept in a cellar.
	// Rough: at the photograph's own roughness a smooth broom head took a sheen and read as tin.
	MatStraw = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.66f, 0.48f, 0.22f), 1.6f);
	MatTwig = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.30f, 0.20f, 0.12f), 1.6f);
	MatHandle = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.38f, 0.32f, 0.26f));
	// Galvanised tin: the rusted-iron photograph on the tint that neutralises its green paint (see
	// MatIron), a shade lighter, since zinc is the pale metal and iron the dark one. Two instances,
	// because the turned bodies reset their instance's tiling and the boxes must not share it.
	MatZinc = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.02f, 0.54f, 0.85f), 0.8f);
	MatTin = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.02f, 0.54f, 0.851f), 0.8f);
	MatGrime = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.07f, 0.058f, 0.045f), 1.3f);
	MatBristle = Build.Flat(FLinearColor(0.030f, 0.026f, 0.022f), 0.95f);
}

void ACellarActor::BuildStairWell(FRoomBuilder& Build)
{
	const float T = 10.f;
	const float West = PassageWestX() - T;
	const float Top = Setup.TopLandingX;
	const TArray<FCellarRoom> AllRooms = Rooms();

	// A wall built in pieces between break points along X, each piece as high as the well is
	// there: up to the hall floor where the floor is open over the stair, up to the passage
	// ceiling west of that, and starting above a room's own door wall where the two would be one.
	auto Pieces = [&](float Y0, float Y1, bool bNorth)
	{
		TArray<float> Breaks = { West, Top, Setup.WellWestX };
		for (const FCellarRoom& Room : AllRooms)
		{
			if (Room.bNorth == bNorth)
			{
				Breaks.Add(Room.CentreX - (Room.Width + WallThickness) * 0.5f);
				Breaks.Add(Room.CentreX + (Room.Width + WallThickness) * 0.5f);
			}
		}
		Breaks.RemoveAll([&](float X) { return X < West || X > Top; });
		Breaks.Sort();
		for (int32 i = 0; i + 1 < Breaks.Num(); ++i)
		{
			const float X0 = Breaks[i];
			const float X1 = Breaks[i + 1];
			const float Mid = (X0 + X1) * 0.5f;
			const bool bOpenAbove = Mid > Setup.WellWestX;
			// South of the well the hall's slab comes down to GroundZ - 22 over the whole length;
			// on the north side it is cut back to the hall's wall wherever the well is open.
			const float Z1 = bOpenAbove ? (bNorth ? Setup.GroundZ : Setup.GroundZ - 22.f) : PassageCeilingZ();
			const bool bOverRoom = AllRooms.ContainsByPredicate([&](const FCellarRoom& Room)
			{
				return Room.bNorth == bNorth && FMath::Abs(Mid - Room.CentreX) < (Room.Width + WallThickness) * 0.5f;
			});
			const float Z0 = bOverRoom ? FloorZ() + RoomHeight : FloorZ();
			if (X1 - X0 > 0.5f && Z1 - Z0 > 0.5f)
			{
				Build.Box(FVector(Mid, (Y0 + Y1) * 0.5f, (Z0 + Z1) * 0.5f), FRotator::ZeroRotator, FVector(X1 - X0, Y1 - Y0, Z1 - Z0), MatBrick);
			}
		}
	};
	Pieces(Setup.ShaftNorthY - T, Setup.ShaftNorthY, true);
	Pieces(Setup.ShaftSouthY, Setup.ShaftSouthY + T, false);

	// The far end of the passage. Built pitched ninety, with the height along its local X: the
	// builder lays the larger repeat count along local X, and a wall taller than it is wide built
	// square came out as the brick photograph stretched into stripes (the kitchen's chimney piers).
	const float EndHeight = PassageCeilingZ() - FloorZ();
	const float EndWidth = Setup.ShaftSouthY - Setup.ShaftNorthY + T * 2.f;
	Build.Box(FVector(West + T * 0.5f, (Setup.ShaftNorthY + Setup.ShaftSouthY) * 0.5f, FloorZ() + EndHeight * 0.5f), FRotator(90.f, 0.f, 0.f),
		FVector(EndHeight, EndWidth, T), MatBrick);

	// The passage ceiling, under the hall's slab, from the end wall to where the well opens.
	Build.Box(FVector((West + Setup.WellWestX) * 0.5f, (Setup.ShaftNorthY + Setup.ShaftSouthY) * 0.5f, PassageCeilingZ() + 2.f), FRotator::ZeroRotator,
		FVector(Setup.WellWestX - West, EndWidth, 4.f), MatTimber);
	// Joists across it, which is what the underside of a floor is from below.
	for (float X = Setup.WellWestX - 30.f; X > West + 10.f; X -= 46.f)
	{
		Build.Box(FVector(X, (Setup.ShaftNorthY + Setup.ShaftSouthY) * 0.5f, PassageCeilingZ() - 8.f), FRotator::ZeroRotator,
			FVector(9.f, EndWidth - 2.f, 16.f), MatTimber, /*bBlockingCollision*/ false);
	}

	// Above the hall floor, the well runs up under the north flight and on under the half-landing.
	// Under the flight the hall's spandrel closes it off; under the landing it is walled here, to
	// the south (the rest of the dark under the landing) and at its west end.
	const float NookHeight = Setup.LandingSoffitZ - Setup.GroundZ;
	if (Setup.LandingEdgeX > Setup.WellWestX)
	{
		Build.Box(FVector((Setup.WellWestX + Setup.LandingEdgeX) * 0.5f, Setup.ShaftSouthY + T * 0.5f, Setup.GroundZ + NookHeight * 0.5f), FRotator::ZeroRotator,
			FVector(Setup.LandingEdgeX - Setup.WellWestX, T, NookHeight), MatBrick);
	}
	const float NookWidth = Setup.ShaftSouthY + T - (Setup.ShaftNorthY - T);
	Build.Box(FVector(Setup.WellWestX - T * 0.5f, (Setup.ShaftNorthY + Setup.ShaftSouthY) * 0.5f, Setup.GroundZ + NookHeight * 0.5f), FRotator(0.f, 90.f, 0.f),
		FVector(NookWidth, T, NookHeight), MatBrick);

	// The passage floor, flagged, under the whole run so nothing below the stair is ever open.
	Build.Box(FVector((West + Top) * 0.5f, (Setup.ShaftNorthY + Setup.ShaftSouthY) * 0.5f, FloorZ() - 5.f), FRotator::ZeroRotator,
		FVector(Top - West, EndWidth, 10.f), MatFloor);

	// Damp: the well is below ground, and the wet comes through the brick from the earth outside.
	const FLinearColor Damp(0.40f, 0.34f, 0.27f);
	FRandomStream Random(1104);
	for (int32 i = 0; i < 9; ++i)
	{
		// Every number drawn into a local first, in order: drawn inside the call's arguments the
		// order is up to the compiler, and a skipped stain has to draw them all the same (09-29).
		const bool bNorth = (i % 2) == 0;
		const float X = Random.FRandRange(West + 30.f, Top - 30.f);
		const float Z = Random.FRandRange(FloorZ() + 20.f, FloorZ() + 140.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const FVector2D Size(Random.FRandRange(60.f, 140.f), Random.FRandRange(80.f, 180.f));
		const float Opacity = Random.FRandRange(0.45f, 0.7f);
		// The footprint turned by its roll, measured along the wall and up it.
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		const float HalfAlong = (Cos * Size.X + Sin * Size.Y) * 0.5f;
		const float HalfUp = (Sin * Size.X + Cos * Size.Y) * 0.5f;
		if (DecalHitsDoorway(AllRooms, bNorth, X, HalfAlong, Z - HalfUp))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(X, bNorth ? Setup.ShaftNorthY + 2.f : Setup.ShaftSouthY - 2.f, Z),
			FRotator(0.f, bNorth ? -90.f : 90.f, Roll), Size, Damp, Opacity, 1.2f);
	}
	// And water standing on the flags at the foot of the stair, where it runs down to.
	Build.Stain(RoomSurfaces::Floorboards, FVector(FootX() - 40.f, (Setup.ShaftNorthY + Setup.ShaftSouthY) * 0.5f, FloorZ() + 4.f),
		FRotator(-90.f, 0.f, 30.f), FVector2D(110.f, 70.f), FLinearColor(0.12f, 0.12f, 0.12f), 0.8f, 1.2f, 0.1f);
}

void ACellarActor::BuildStair(FRoomBuilder& Build)
{
	const float Rise = this->Rise();
	const float Top = Setup.TopLandingX;
	const float StringerT = 4.f;
	const float Y0 = Setup.ShaftNorthY + StringerT;
	const float Y1 = Setup.ShaftSouthY - StringerT;
	const float MidY = (Setup.ShaftNorthY + Setup.ShaftSouthY) * 0.5f;
	FRandomStream Wear(18551104);

	// Plain boards on two strings, the way a cellar stair was always built: no nosing, no runner,
	// and the risers closed only because the dark under an open one is a worse thing to look into.
	// Visual only — what is walked on is the ramp below, as on the hall's flights.
	for (int32 i = 0; i < Risers - 1; ++i)
	{
		const float X0 = Top - Going * (i + 1);
		const float TreadZ = Setup.GroundZ - Rise * (i + 1);
		// Worn hollow down the middle and a few of them cracked or sagging a degree.
		const float Sag = Wear.FRand() < 0.25f ? Wear.FRandRange(-1.4f, 1.4f) : 0.f;
		Build.Box(FVector(X0 + Going * 0.5f, MidY, TreadZ - 2.f), FRotator(0.f, 0.f, Sag), FVector(Going + 1.f, Y1 - Y0, 4.f),
			Wear.FRand() < 0.35f ? MatTimber.Get() : MatTread.Get(), /*bBlockingCollision*/ false);
	}
	for (int32 i = 0; i < Risers; ++i)
	{
		const float X = Top - Going * i;
		const float RiserTop = Setup.GroundZ - Rise * i;
		Build.Box(FVector(X - 1.f, MidY, RiserTop - Rise * 0.5f), FRotator::ZeroRotator, FVector(2.f, Y1 - Y0, Rise), MatTimber, /*bBlockingCollision*/ false);
	}

	// The strings, along both walls, on the pitch line.
	const FVector From(Top, 0.f, Setup.GroundZ);
	const FVector To(FootX() - Going, 0.f, FloorZ());
	const FVector Dir = (To - From).GetSafeNormal();
	const FRotator Pitch = Dir.Rotation();
	const float Length = FVector::Dist(From, To);
	for (const float Y : { Setup.ShaftNorthY + StringerT * 0.5f, Setup.ShaftSouthY - StringerT * 0.5f })
	{
		const FVector Mid = (From + To) * 0.5f + FVector(0.f, Y, -6.f);
		Build.Box(Mid, Pitch, FVector(Length, StringerT, 26.f), MatTimber, /*bBlockingCollision*/ false);
	}

	// The ramp that is walked on: the pitch line through the nosings, from the top landing to one
	// going past the bottom riser (the hall's rule, 09-27).
	const FVector Normal = Pitch.RotateVector(FVector::UpVector);
	CellarPawnOnly(Build.Box((From + To) * 0.5f + FVector(0.f, MidY, 0.f) - Normal * 5.f, Pitch,
		FVector(Length + 8.f, Setup.ShaftSouthY - Setup.ShaftNorthY, 10.f), MatVoid));

	// A rail along the south wall on iron brackets: a round pole, gone dark where hands held it.
	const FVector RailFrom = From + FVector(0.f, Setup.ShaftSouthY - 7.f, CellarRailHeight);
	const FVector RailTo = To + FVector(0.f, Setup.ShaftSouthY - 7.f, CellarRailHeight);
	Build.Cyl((RailFrom + RailTo) * 0.5f, FRotationMatrix::MakeFromZ(RailTo - RailFrom).Rotator(), FVector(5.f, 5.f, FVector::Dist(RailFrom, RailTo)), MatTimber, /*bBlockingCollision*/ false);
	const int32 Brackets = 4;
	for (int32 i = 0; i < Brackets; ++i)
	{
		const FVector At = FMath::Lerp(RailFrom, RailTo, (i + 0.5f) / Brackets);
		Build.Box(At + FVector(0.f, 4.f, -4.f), FRotator::ZeroRotator, FVector(2.f, 7.f, 2.f), MatIron, /*bBlockingCollision*/ false);
		Build.Box(At + FVector(0.f, 6.5f, -6.f), FRotator::ZeroRotator, FVector(2.f, 2.f, 8.f), MatIron, /*bBlockingCollision*/ false);
	}
}

void ACellarActor::BuildRoomShell(FRoomBuilder& Build, const FCellarRoom& Room)
{
	// Room01's kind of shell, solid slabs: the bedroom's dressing lays its plaster and boards over
	// the inner faces, and in a bare room the brick is the finish. Brick on the outside too, because
	// the outside of the door wall is the corridor.
	const FVector C = RoomCentre(Room);
	const float WH = Room.Width * 0.5f;
	const float DH = Room.Depth * 0.5f;
	const float H = RoomHeight;
	const float T = WallThickness;
	const float Side = Room.bNorth ? 1.f : -1.f;   // which way the door wall is from the centre
	const float DoorU = Room.DoorX - C.X;
	const float Half = DoorHalf(Room);
	const float DoorH = DoorHeight(Room);

	// The floor and the ceiling stop at the middle of the door wall: past it is the corridor's
	// wall, and a face shared with it would flicker. In the bedroom the floor is a backstop under
	// the dressing's boards (it shows black in the gaps of the subfloor, as Room01's does); in a
	// bare room it is the flags themselves.
	const FVector SlabSize(Room.Width + T, Room.Depth + T * 0.5f, 10.f);
	const FVector SlabShift(0.f, -Side * T * 0.25f, 0.f);
	if (Room.bBedroom)
	{
		Build.Box(C + SlabShift + FVector(0.f, 0.f, -45.f), FRotator::ZeroRotator, SlabSize, MatShell);
		Build.Box(C + SlabShift + FVector(0.f, 0.f, H + 5.f), FRotator::ZeroRotator, SlabSize, MatShell);
	}
	else
	{
		Build.Box(C + SlabShift + FVector(0.f, 0.f, -5.f), FRotator::ZeroRotator, SlabSize, MatFloor);
		Build.Box(C + SlabShift + FVector(0.f, 0.f, H + 5.f), FRotator::ZeroRotator, SlabSize, MatTimber);
	}

	// The back wall, and the two side walls yawed so their length lies on local X.
	Build.Box(C + FVector(0.f, -Side * DH, H * 0.5f), FRotator::ZeroRotator, FVector(Room.Width + T, T, H), MatBrick);
	Build.Box(C + FVector(WH, 0.f, H * 0.5f), FRotator(0.f, 90.f, 0.f), FVector(Room.Depth - T, T, H), MatBrick);
	Build.Box(C + FVector(-WH, 0.f, H * 0.5f), FRotator(0.f, 90.f, 0.f), FVector(Room.Depth - T, T, H), MatBrick);

	// The door wall, round the opening.
	const float LeftWidth = WH + T * 0.5f + DoorU - Half;
	const float RightWidth = WH + T * 0.5f - DoorU - Half;
	const float WallY = Side * DH;
	Build.Box(C + FVector(-WH - T * 0.5f + LeftWidth * 0.5f, WallY, H * 0.5f), FRotator::ZeroRotator, FVector(LeftWidth, T, H), MatBrick);
	Build.Box(C + FVector(WH + T * 0.5f - RightWidth * 0.5f, WallY, H * 0.5f), FRotator::ZeroRotator, FVector(RightWidth, T, H), MatBrick);
	Build.Box(C + FVector(DoorU, WallY, (DoorH + H) * 0.5f), FRotator::ZeroRotator, FVector(Half * 2.f, T, H - DoorH), MatBrick);

	// Rough timber lining the opening, a lintel, and a step worn hollow.
	for (const float Jamb : { -1.f, 1.f })
	{
		Build.Box(C + FVector(DoorU + Jamb * (Half - 3.f), WallY, DoorH * 0.5f), FRotator::ZeroRotator,
			FVector(6.f, T + 2.f, DoorH), MatTimber);
	}
	Build.Box(C + FVector(DoorU, WallY, DoorH - 4.f), FRotator::ZeroRotator, FVector(Half * 2.f, T + 2.f, 8.f), MatTimber, /*bBlockingCollision*/ false);
	Build.Box(C + FVector(DoorU, WallY, 1.f), FRotator::ZeroRotator, FVector(Half * 2.f - 12.f, T + 2.f, 2.f), MatTimber);
}

void ACellarActor::BuildBareRoom(FRoomBuilder& Build, const FCellarRoom& Room)
{
	// Nothing in it yet but what a cellar has: joists under the floor above, rising damp, the
	// mortar going, cobwebs, and what has come off the walls lying at the foot of them.
	FRandomStream Random(Room.Seed);
	const FVector C = RoomCentre(Room);
	const float H = RoomHeight;
	const float InX = Room.Width * 0.5f - WallThickness * 0.5f;
	const float InY = Room.Depth * 0.5f - WallThickness * 0.5f;

	// Joists across the short span.
	for (float X = -InX + 30.f; X < InX - 10.f; X += 52.f)
	{
		Build.Box(C + FVector(X, 0.f, H - 9.f), FRotator::ZeroRotator, FVector(10.f, InY * 2.f, 18.f), MatTimber, /*bBlockingCollision*/ false);
	}

	// Damp and cracks on each wall: low and dark, coming up out of the floor, and the odd crack.
	const FLinearColor Damp(0.40f, 0.34f, 0.27f);
	struct FFace { FVector Point; FRotator Aim; float Half; };
	const FFace Faces[4] = {
		{ FVector(0.f, -InY + 2.f, 0.f), FRotator(0.f, -90.f, 0.f), InX },
		{ FVector(0.f, InY - 2.f, 0.f), FRotator(0.f, 90.f, 0.f), InX },
		{ FVector(InX - 2.f, 0.f, 0.f), FRotator(0.f, 0.f, 0.f), InY },
		{ FVector(-InX + 2.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f), InY },
	};
	for (const FFace& Face : Faces)
	{
		const FVector Along = Face.Aim.RotateVector(FVector(0.f, 1.f, 0.f));
		// The door wall is the one on the corridor's side; the decals on it are kept off the doorway.
		const bool bDoorWall = FMath::Abs(Face.Point.X) < 1.f && (Face.Point.Y > 0.f) == Room.bNorth;
		for (int32 i = 0; i < 4; ++i)
		{
			const float U = Random.FRandRange(-Face.Half + 40.f, Face.Half - 40.f);
			const float Height = Random.FRandRange(60.f, 150.f);
			const float Width = Random.FRandRange(80.f, 180.f);
			const float Opacity = Random.FRandRange(0.5f, 0.75f);
			const FVector At = C + Face.Point + Along * U + FVector(0.f, 0.f, Height * 0.4f);
			if (bDoorWall && DecalHitsDoorway(MakeArrayView(&Room, 1), Room.bNorth, At.X, Width * 0.5f, At.Z - Height * 0.5f))
			{
				continue;
			}
			Build.Stain(RoomSurfaces::Damp, At, Face.Aim, FVector2D(Width, Height), Damp * 0.8f, Opacity, 1.2f);
		}
		for (int32 i = 0; i < 2; ++i)
		{
			const float U = Random.FRandRange(-Face.Half + 50.f, Face.Half - 50.f);
			const float Z = Random.FRandRange(80.f, H - 60.f);
			const FVector2D Size(Random.FRandRange(80.f, 160.f), Random.FRandRange(100.f, 200.f));
			const float Opacity = Random.FRandRange(0.6f, 0.9f);
			const float Sharpness = Random.FRandRange(16.f, 26.f);
			const FVector At = C + Face.Point + Along * U + FVector(0.f, 0.f, Z);
			if (bDoorWall && DecalHitsDoorway(MakeArrayView(&Room, 1), Room.bNorth, At.X, Size.X * 0.5f, At.Z - Size.Y * 0.5f))
			{
				continue;
			}
			Build.Crack(At, Face.Aim, Size, Opacity, Sharpness);
		}
	}

	// Cobwebs slung across the upper corners, as upstairs: a plane, never a box.
	for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f) })
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const float Inset = 24.f + i * 22.f;
			Build.Add(FRoomShapes::Plane(), C + FVector(Corner.X * (InX - Inset), Corner.Y * (InY - Inset), H - 20.f - i * 6.f),
				FRotator(0.f, Corner.X * Corner.Y > 0.f ? 45.f : -45.f, 180.f), FVector(Inset * 1.6f, Inset * 1.6f, 1.f), MatWeb, /*bBlockingCollision*/ false);
		}
	}

	// Brick and mortar fallen at the foot of the walls, and a puddle where the floor is lowest.
	for (int32 i = 0; i < 40; ++i)
	{
		const bool bAlongX = Random.FRand() < 0.5f;
		const float Edge = Random.FRand() < 0.5f ? -1.f : 1.f;
		const FVector2D Spot = bAlongX
			? FVector2D(Random.FRandRange(-InX + 10.f, InX - 10.f), Edge * (InY - Random.FRandRange(4.f, 40.f)))
			: FVector2D(Edge * (InX - Random.FRandRange(4.f, 40.f)), Random.FRandRange(-InY + 10.f, InY - 10.f));
		const float Size = Random.FRandRange(2.f, 11.f);
		const FRotator Rot(Random.FRandRange(-25.f, 25.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-25.f, 25.f));
		// Not in the doorway.
		const bool bDoorSide = Room.bNorth ? Spot.Y > InY - 60.f : Spot.Y < -InY + 60.f;
		if (bDoorSide && FMath::Abs(C.X + Spot.X - Room.DoorX) < DoorHalf(Room) + 70.f)
		{
			continue;
		}
		Build.Box(C + FVector(Spot.X, Spot.Y, Size * 0.3f), Rot, FVector(Size * 1.5f, Size, Size * 0.6f), MatRubble, /*bBlockingCollision*/ false);
	}
	Build.Stain(RoomSurfaces::Floorboards, C + FVector(Random.FRandRange(-InX * 0.5f, InX * 0.5f), Random.FRandRange(-InY * 0.4f, InY * 0.4f), 4.f),
		FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)), FVector2D(Random.FRandRange(90.f, 150.f), Random.FRandRange(60.f, 100.f)),
		FLinearColor(0.12f, 0.12f, 0.12f), 0.8f, 1.2f, 0.1f);
}

void ACellarActor::SpawnBedroom(const FCellarRoom& Room)
{
	// Room01's dressing with its contents on: the same seed, so the walls are Room01's walls and
	// the furniture stands where it stood. Translated, never turned — the dressing places its
	// clue markers by adding to its own location — which is why the bedroom is on the north side,
	// with its door wall at +Y as Room01's is.
	const FTransform Transform(FRotator::ZeroRotator, GetActorTransform().TransformPosition(RoomCentre(Room)));
	Bedroom = GetWorld()->SpawnActorDeferred<ARoomDressingActor>(ARoomDressingActor::StaticClass(), Transform, this);
	if (Bedroom)
	{
		FRoomDressingSetup RoomSetup;
		RoomSetup.Width = Room.Width;
		RoomSetup.Depth = Room.Depth;
		RoomSetup.Height = RoomHeight;
		RoomSetup.WallThickness = WallThickness;
		RoomSetup.DoorOpeningWidth = DoorOpeningWidth;
		RoomSetup.DoorOpeningCenterX = Room.DoorX - Room.CentreX;
		RoomSetup.bWakeSpot = false;
		RoomSetup.bWindow = false;
		RoomSetup.bHook = false;
		RoomSetup.bContents = true;
		// The maid's room: no chair lying near a hook that is not here, and her brooms and pails
		// where the armchair stood (BuildMaidsCorner).
		RoomSetup.bOverturnedChair = false;
		RoomSetup.bArmchair = false;
		Bedroom->Configure(RoomSetup);
		Bedroom->FinishSpawning(Transform);
	}
}

void ACellarActor::BuildMaidsCorner(FRoomBuilder& Build, const FCellarRoom& Room)
{
	// The corner where the west wall meets the door wall, where the armchair stood in Room01: two
	// brooms leant into it, a galvanised pail standing in front of them with its bail dropped
	// against its side, a second pail knocked over with the bail come off it, and a dustpan with
	// the hand brush still in it. Everything is laid out in the dressing's frame (its origin is the
	// room's centre on the floor): the west wall's inner face is at -Width/2 + T/2, the door wall's
	// at +Depth/2 - T/2. Kept above Y 190, clear of where the footprints stop (-350, 170).
	const FVector C = RoomCentre(Room);
	const float WestFace = -Room.Width * 0.5f + WallThickness * 0.5f;
	const float DoorFace = Room.Depth * 0.5f - WallThickness * 0.5f;
	// The boards stand anywhere from flush to seven centimetres proud of the room's floor (each is
	// lifted, pitched and warped on its own), so nothing here can rest at one fixed height: laid at
	// the average, the dustpan and the loose bail went half under the higher boards. What a thing
	// rests on is the highest board under its footprint, found by tracing down onto the dressing's
	// boards, which the bedroom built (and gave collision) before this runs. Boards is the fallback.
	const float Boards = 3.5f;
	FRandomStream Random(1955);
	auto RestOn = [&](const FVector2D& Spot, float Radius)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MaidsCorner), false, this);
		float Top = -1.f;
		// The middle, and two rings round it: the boards are 21cm strips, and a footprint that
		// reaches a strip no sample lands on can go under it (the loose bail's far end did).
		TArray<FVector2D, TInlineAllocator<17>> Samples = { FVector2D::ZeroVector };
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector2D Dir(FMath::Cos(i * PI / 4.f), FMath::Sin(i * PI / 4.f));
			Samples.Add(Dir * Radius);
			Samples.Add(Dir * Radius * 0.5f);
		}
		for (const FVector2D& Offset : Samples)
		{
			const FVector Local = C + FVector(Spot.X + Offset.X, Spot.Y + Offset.Y, 0.f);
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorTransform().TransformPosition(Local + FVector(0.f, 0.f, 40.f)),
				GetActorTransform().TransformPosition(Local + FVector(0.f, 0.f, -10.f)), ECC_Visibility, Params))
			{
				Top = FMath::Max(Top, GetActorTransform().InverseTransformPosition(Hit.ImpactPoint).Z - C.Z);
			}
		}
		return Top >= 0.f ? Top : Boards;
	};

	// A scene component to build a tilted thing in its own frame.
	auto Pivot = [&](const FVector& Local, const FRotator& Rotation, const TCHAR* Name)
	{
		USceneComponent* P = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
		P->SetMobility(EComponentMobility::Movable);
		P->AttachToComponent(CellarRoot, FAttachmentTransformRules::KeepRelativeTransform);
		P->SetRelativeLocationAndRotation(C + Local, Rotation);
		P->RegisterComponent();
		AddInstanceComponent(P);
		return P;
	};
	// A rod between two points.
	auto Rod = [](FRoomBuilder& B, const FVector& From, const FVector& To, float Diameter, UMaterialInterface* Mat)
	{
		const FVector Span = To - From;
		B.Cyl((From + To) * 0.5f, FRotationMatrix::MakeFromZ(Span).Rotator(), FVector(Diameter, Diameter, Span.Size()), Mat, /*bBlockingCollision*/ false);
	};

	// A broom leant on a wall: built standing on local Z from the middle of the bottom of its head,
	// then tipped by Lean towards the wall that Yaw turns local +X onto. The edge of the head on
	// the wall's side is the one left on the floor, so the pivot is raised by how far that edge
	// would otherwise go under. The top of the handle touches the plaster.
	auto Leant = [&](float Length, float Lean, float HeadHalfDepth, float Yaw, const FVector2D& WallPoint, float WallDistance, const TCHAR* Name)
	{
		const float S = FMath::Sin(FMath::DegreesToRadians(Lean));
		const FVector Out = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(1.f, 0.f, 0.f));
		const FVector2D Foot = WallPoint - FVector2D(Out.X, Out.Y) * (Length * S + WallDistance);
		return Pivot(FVector(Foot.X, Foot.Y, RestOn(Foot, 12.f) + HeadHalfDepth * S), FRotator(-Lean, Yaw, 0.f), Name);
	};

	// The corn broom, against the west wall: a flat fan of straw sewn into rows, tied hard round the
	// handle at the shoulder, and splayed and broken along the edge it swept with.
	{
		const float Length = 150.f;
		USceneComponent* P = Leant(Length, 14.f, 3.25f, 180.f, FVector2D(WestFace, DoorFace - 26.f), 1.4f, TEXT("CornBroom"));
		FRoomBuilder B(this, P);
		// The fan: a cone flattened to the head's thickness, the point lost inside the binding, and
		// the straw laid over both faces of it. The cone alone was a smooth tin funnel: what says
		// straw is the lines of it, running from the shoulder and spreading to the sweeping edge.
		B.Add(FRoomShapes::Cone(), FVector(0.f, 0.f, 17.f), FRotator::ZeroRotator, FVector(6.5f, 30.f, 34.f), MatTwig, /*bBlockingCollision*/ false);
		for (const float Face : { -1.f, 1.f })
		{
			const int32 Straws = 24;
			for (int32 i = 0; i < Straws; ++i)
			{
				const float U = (i + 0.5f) / Straws * 2.f - 1.f + Random.FRandRange(-0.02f, 0.02f);
				const float TopZ = Random.FRandRange(20.f, 24.f);
				const float BottomZ = Random.FRandRange(0.2f, 1.6f);
				const FVector Top(Face * (3.25f * (1.f - TopZ / 34.f) + 0.25f), U * 15.f * (1.f - TopZ / 34.f), TopZ);
				const FVector Bottom(Face * (3.25f * (1.f - BottomZ / 34.f) + 0.25f), U * 15.f * (1.f - BottomZ / 34.f) + Random.FRandRange(-0.6f, 0.6f), BottomZ);
				Rod(B, Top, Bottom, Random.FRandRange(0.45f, 0.7f), MatStraw);
			}
		}
		// Two rows of stitching across it, each the width of the fan where it runs.
		for (const float Z : { 9.f, 14.5f })
		{
			const float Taper = 1.f - Z / 34.f;
			B.Cyl(FVector(0.f, 0.f, Z), FRotator::ZeroRotator, FVector(6.5f * Taper + 0.7f, 30.f * Taper + 0.7f, 0.9f), MatTwig, /*bBlockingCollision*/ false);
		}
		// The shoulder, wired tight round the handle.
		B.Cyl(FVector(0.f, 0.f, 21.f), FRotator::ZeroRotator, FVector(4.4f, 13.f, 5.f), MatStraw, /*bBlockingCollision*/ false);
		B.Cyl(FVector(0.f, 0.f, 25.5f), FRotator::ZeroRotator, FVector(3.8f, 8.f, 4.f), MatStraw, /*bBlockingCollision*/ false);
		B.Cyl(FVector(0.f, 0.f, 27.8f), FRotator::ZeroRotator, FVector(3.6f, 3.6f, 1.6f), MatTin, /*bBlockingCollision*/ false);
		Rod(B, FVector(0.f, 0.f, 22.f), FVector(0.f, 0.f, Length), 2.8f, MatHandle);
		B.Sph(FVector(0.f, 0.f, Length), 3.f, MatHandle);
		// Straws broken out of the sweeping edge, every one at its own angle.
		for (int32 i = 0; i < 12; ++i)
		{
			const float Y = Random.FRandRange(-14.f, 14.f);
			const FVector From(Random.FRandRange(-2.f, 2.f), Y, Random.FRandRange(2.f, 5.f));
			const FVector To = From + FVector(Random.FRandRange(-5.f, 5.f), Y * 0.25f + Random.FRandRange(-3.f, 3.f), -Random.FRandRange(1.f, 2.5f));
			Rod(B, From, To, 0.45f, MatStraw);
		}
	}

	// The besom, against the door wall: twigs bound in a cone round the end of the handle, with
	// the ones that have worked loose splaying out round the foot.
	{
		const float Length = 138.f;
		USceneComponent* P = Leant(Length, 18.f, 12.f, 90.f, FVector2D(WestFace + 72.f, DoorFace), 1.5f, TEXT("Besom"));
		FRoomBuilder B(this, P);
		// A dark core under the twigs, then the twigs themselves: each its own line from the binding
		// out to the foot, which is what reads as a bundle rather than as a cone.
		B.Add(FRoomShapes::Cone(), FVector(0.f, 0.f, 23.f), FRotator::ZeroRotator, FVector(22.f, 22.f, 46.f), MatBristle, /*bBlockingCollision*/ false);
		for (int32 i = 0; i < 44; ++i)
		{
			const float A = (i + Random.FRandRange(0.f, 0.8f)) / 44.f * 2.f * PI;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
			const float TopZ = Random.FRandRange(36.f, 40.f);
			const float BottomZ = Random.FRandRange(0.3f, 2.5f);
			const FVector Top = Dir * (11.f * (1.f - TopZ / 46.f) + 0.3f) + FVector(0.f, 0.f, TopZ);
			const FVector Bottom = Dir * (11.f * (1.f - BottomZ / 46.f) + Random.FRandRange(0.3f, 1.6f)) + FVector(0.f, 0.f, BottomZ);
			Rod(B, Top, Bottom, Random.FRandRange(0.6f, 0.95f), MatTwig);
		}
		for (const float Z : { 30.f, 37.f })
		{
			const float D = 24.f * (1.f - Z / 46.f) + 0.8f;
			B.Cyl(FVector(0.f, 0.f, Z), FRotator::ZeroRotator, FVector(D, D, 1.4f), MatHandle, /*bBlockingCollision*/ false);
		}
		Rod(B, FVector(0.f, 0.f, 32.f), FVector(0.f, 0.f, Length), 3.f, MatHandle);
		for (int32 i = 0; i < 18; ++i)
		{
			const float A = Random.FRandRange(0.f, 2.f * PI);
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
			const float Start = Random.FRandRange(10.f, 18.f);
			const FVector From = Dir * (12.f * (1.f - Start / 46.f) - 0.5f) + FVector(0.f, 0.f, Start);
			const FVector To = Dir * Random.FRandRange(11.f, 15.f) + FVector(0.f, 0.f, Random.FRandRange(0.5f, 3.f));
			Rod(B, From, To, Random.FRandRange(0.5f, 0.8f), MatTwig);
		}
	}

	// The pail. Turned (FRoomBuilder::Lathe): a cylinder has no inside and no rim, and a bucket is
	// little else. Tapered, two pressed beads round it, the top rolled over a wire.
	const float PailHeight = 26.4f;
	auto Wall = [PailHeight](float Z) { return 12.6f + 2.7f * Z / PailHeight; };
	TArray<FVector2D> Pail = { { 0.f, 0.f }, { 12.2f, 0.f }, { 12.2f, 0.f }, { 12.6f, 0.7f } };
	for (const float Bead : { 7.6f, 17.6f })
	{
		Pail.Add({ Wall(Bead - 0.6f), Bead - 0.6f });
		Pail.Add({ Wall(Bead) + 0.35f, Bead });
		Pail.Add({ Wall(Bead + 0.6f), Bead + 0.6f });
	}
	Pail.Append({ { Wall(PailHeight), PailHeight }, { 15.75f, 26.7f }, { 15.85f, 27.3f }, { 15.4f, 27.7f }, { 15.05f, 27.2f },
		{ Wall(PailHeight) - 0.35f, 26.4f }, { Wall(1.2f) - 0.35f, 1.2f }, { 0.f, 1.0f } });
	const float EarZ = 23.5f;
	const float EarRadius = Wall(EarZ) + 0.9f;

	// The bail: a wire arc between the ears, Fall degrees down from upright about the line through
	// them. At 100 its grip rests against the side, which is where a bail goes when it is let go;
	// at 90 it lies flat.
	auto Bail = [&](FRoomBuilder& B, float Fall, const FVector& Centre)
	{
		const float SinF = FMath::Sin(FMath::DegreesToRadians(Fall));
		const float CosF = FMath::Cos(FMath::DegreesToRadians(Fall));
		auto At = [&](float T) { return Centre + FVector(EarRadius * FMath::Cos(T), -EarRadius * FMath::Sin(T) * SinF, EarRadius * FMath::Sin(T) * CosF); };
		const int32 Steps = 16;
		for (int32 i = 0; i < Steps; ++i)
		{
			Rod(B, At(PI * i / Steps), At(PI * (i + 1) / Steps), 0.8f, MatIron);
		}
		// The wooden grip in the middle of it.
		Rod(B, At(PI * 0.5f) - FVector(4.5f, 0.f, 0.f), At(PI * 0.5f) + FVector(4.5f, 0.f, 0.f), 2.f, MatHandle);
	};

	// The one standing up, in front of the brooms, with what was left in it dried to a crust.
	{
		const FVector2D Spot(WestFace + 122.f, DoorFace - 37.f);
		USceneComponent* P = Pivot(FVector(Spot.X, Spot.Y, RestOn(Spot, 11.f)), FRotator(0.f, 30.f, 0.f), TEXT("Pail"));
		FRoomBuilder B(this, P);
		B.Lathe(FVector::ZeroVector, FRotator::ZeroRotator, Pail, 36, MatZinc, RoomSurfaces::RustedIron.TexelSizeCm * 0.3f);
		// Walked inwards so it faces up, shrunk off the tin all round.
		const TArray<FVector2D> Crust = { { 12.4f, 2.4f }, { 11.6f, 3.0f }, { 6.f, 3.4f }, { 0.f, 3.5f } };
		B.Lathe(FVector::ZeroVector, FRotator::ZeroRotator, Crust, 28, MatGrime, 30.f);
		for (const float Side : { -1.f, 1.f })
		{
			B.Box(FVector(Side * (Wall(EarZ) + 0.4f), 0.f, EarZ), FRotator::ZeroRotator, FVector(1.2f, 2.6f, 3.4f), MatTin, /*bBlockingCollision*/ false);
		}
		Bail(B, 100.f, FVector(0.f, 0.f, EarZ));
		CellarPawnOnly(Build.Box(C + FVector(Spot.X, Spot.Y, 15.f), FRotator::ZeroRotator, FVector(34.f, 34.f, 30.f), MatVoid));
	}

	// The one knocked over, its mouth to the room. Lying on its side a tapered pail rests on its rim
	// and the edge of its bottom, so its axis rises towards the mouth by the taper.
	{
		const float Tilt = FMath::RadiansToDegrees(FMath::Atan2(15.85f - 12.6f, 27.3f));
		const FVector2D Spot(WestFace + 18.f, DoorFace - 79.f);
		const float Yaw = -20.f;
		const FVector Axis = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(1.f, 0.f, 0.f));
		const float Floor = RestOn(Spot + FVector2D(Axis.X, Axis.Y) * 13.f, 12.f);
		USceneComponent* P = Pivot(FVector(Spot.X, Spot.Y, Floor + 12.6f * FMath::Cos(FMath::DegreesToRadians(Tilt))), FRotator(Tilt - 90.f, Yaw, 0.f), TEXT("PailOver"));
		FRoomBuilder B(this, P);
		B.Lathe(FVector::ZeroVector, FRotator::ZeroRotator, Pail, 36, MatZinc, RoomSurfaces::RustedIron.TexelSizeCm * 0.3f);
		// The ears on local Y, which stays level when the pail is tipped: on X one would be under it.
		for (const float Side : { -1.f, 1.f })
		{
			B.Box(FVector(0.f, Side * (Wall(EarZ) + 0.4f), EarZ), FRotator::ZeroRotator, FVector(2.6f, 1.2f, 3.4f), MatTin, /*bBlockingCollision*/ false);
		}
		const FVector Mouth = C + FVector(Spot.X, Spot.Y, Floor) + Axis * 30.f;
		CellarPawnOnly(Build.Box(C + FVector(Spot.X, Spot.Y, 16.f) + Axis * 13.f, FRotator(0.f, Yaw, 0.f), FVector(30.f, 34.f, 32.f), MatVoid));
		// What ran out of it, long dried into the boards: far enough out from the mouth that the
		// decal's reach stops short of the tin.
		Build.Stain(RoomSurfaces::Damp, Mouth + Axis * 30.f + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, Yaw + 90.f), FVector2D(44.f, 44.f),
			FLinearColor(0.05f, 0.045f, 0.04f), 0.6f, 1.2f);
	}

	// Its bail, come off and lying on the boards beside it.
	{
		// Propped a hair off the boards by its grip.
		const FVector2D Spot(WestFace + 160.f, DoorFace - 70.f);
		USceneComponent* P = Pivot(FVector(Spot.X, Spot.Y, RestOn(Spot, 17.f) + 1.f), FRotator(0.f, 64.f, 0.f), TEXT("LooseBail"));
		FRoomBuilder B(this, P);
		Bail(B, 90.f, FVector::ZeroVector);
	}

	// A tin dustpan, the hand brush left in it.
	{
		const FVector2D Spot(WestFace + 155.f, DoorFace - 110.f);
		USceneComponent* P = Pivot(FVector(Spot.X, Spot.Y, RestOn(Spot, 11.f)), FRotator(0.f, 152.f, 0.f), TEXT("Dustpan"));
		FRoomBuilder B(this, P);
		// Pan: a floor that runs out to a lip at the front (+X), a back, and sides falling to the lip.
		B.Box(FVector(0.f, 0.f, 0.2f), FRotator::ZeroRotator, FVector(23.f, 25.f, 0.4f), MatTin, /*bBlockingCollision*/ false);
		B.Box(FVector(-11.3f, 0.f, 3.6f), FRotator::ZeroRotator, FVector(0.5f, 25.f, 7.f), MatTin, /*bBlockingCollision*/ false);
		const float SideSlope = FMath::RadiansToDegrees(FMath::Atan2(7.f, 23.f));
		for (const float Side : { -1.f, 1.f })
		{
			B.Box(FVector(-1.f, Side * 12.3f, 1.4f), FRotator(-SideSlope, 0.f, 0.f), FVector(24.f, 0.5f, 3.6f), MatTin, /*bBlockingCollision*/ false);
		}
		Rod(B, FVector(-11.5f, 0.f, 4.5f), FVector(-24.f, 0.f, 7.5f), 1.8f, MatTin);
		// The hand brush, lying in the pan bristles down.
		B.Box(FVector(2.f, 1.5f, 2.f), FRotator(0.f, 12.f, 0.f), FVector(18.f, 4.2f, 3.2f), MatBristle, /*bBlockingCollision*/ false);
		B.Box(FVector(2.f, 1.5f, 4.6f), FRotator(0.f, 12.f, 0.f), FVector(21.f, 5.f, 2.2f), MatHandle, /*bBlockingCollision*/ false);
		B.Sph(FVector(-9.2f, -0.6f, 4.8f), 3.2f, MatHandle);
	}
}

void ACellarActor::SpawnWineCellar(const FCellarRoom& Room)
{
	// In the cellar's frame, translated to the room's centre on its floor, like the bedroom.
	const FTransform Transform(FRotator::ZeroRotator, GetActorTransform().TransformPosition(RoomCentre(Room)));
	WineCellar = GetWorld()->SpawnActorDeferred<AWineCellarActor>(AWineCellarActor::StaticClass(), Transform, this);
	if (WineCellar)
	{
		WineCellar->FinishSpawning(Transform);
	}
}

void ACellarActor::SpawnDoor(const FCellarRoom& Room)
{
	// A door standing a crack open, that goes the rest of the way when pushed — the bedroom's too,
	// which is as wide and as tall as Room01's doorway. AHallDoorActor's
	// local +X is the corridor side and its leaf runs along local +Y from the hinge: yaw 90 on a
	// north room (hinge at the east jamb), -90 on a south one (hinge at the west jamb), and a
	// positive swing takes it into the room.
	const float Half = DoorHalf(Room);
	const float Face = Room.bNorth ? Setup.ShaftNorthY : Setup.ShaftSouthY;
	const FVector Hinge = Room.bNorth
		? FVector(Room.DoorX + Half - DoorHingeInset, Face - DoorHingeProud, FloorZ())
		: FVector(Room.DoorX - Half + DoorHingeInset, Face + DoorHingeProud, FloorZ());
	const FTransform Transform(FRotator(0.f, Room.bNorth ? 90.f : -90.f, 0.f), GetActorTransform().TransformPosition(Hinge));
	if (AHallDoorActor* Door = GetWorld()->SpawnActorDeferred<AHallDoorActor>(AHallDoorActor::StaticClass(), Transform, this))
	{
		FHallDoorSetup DoorSetup;
		DoorSetup.Width = Half * 2.f - DoorLeafClearance;
		DoorSetup.Height = DoorHeight(Room) - 8.f;
		DoorSetup.AjarYaw = 4.f;
		DoorSetup.OpenYaw = DoorOpenYaw;
		DoorSetup.Seed = Room.Seed;
		DoorSetup.WoodTint = FLinearColor(0.20f, 0.20f, 0.19f);
		// The bedroom's is a house door, panelled like the ones upstairs; the rest are cellar doors.
		DoorSetup.bSixPanel = Room.bBedroom;
		DoorSetup.bRotHole = (Room.Seed % 2) == 0;
		if (Room.bWine)
		{
			// Oak boards in iron, darker than the plank doors: it was made to keep something in.
			DoorSetup.bIronBound = true;
			DoorSetup.bRotHole = false;
			DoorSetup.WoodTint = FLinearColor(0.14f, 0.13f, 0.12f);
		}
		Door->Configure(DoorSetup);
		Door->FinishSpawning(Transform);
		Doors.Add(Door);
	}
}
