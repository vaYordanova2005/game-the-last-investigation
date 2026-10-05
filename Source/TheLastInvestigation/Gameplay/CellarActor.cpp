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
	// opening is where Room01's door is along its wall; the bare rooms are laid out from it, a
	// brick's length apart, and the two on the left are staggered against the two on the right so
	// no door looks straight into another.
	const float BedCentreX = BedroomDoorX() - DoorOpeningCenterX;
	const float BedWestOuter = BedCentreX - (RoomWidth + WallThickness) * 0.5f;
	const float NorthTwoX = BedWestOuter - 10.f - (600.f + WallThickness) * 0.5f;
	const float SouthOneX = FootX() - 42.f - (640.f + WallThickness) * 0.5f;
	const float SouthTwoX = SouthOneX - (640.f + WallThickness) * 0.5f - 10.f - (600.f + WallThickness) * 0.5f;
	return {
		{ BedCentreX, RoomWidth, RoomDepth, true, BedroomDoorX(), true, 6000 },
		{ NorthTwoX, 600.f, 500.f, true, NorthTwoX - 110.f, false, 6101 },
		{ SouthOneX, 640.f, 540.f, false, SouthOneX - 100.f, false, 6202 },
		{ SouthTwoX, 600.f, 540.f, false, SouthTwoX + 90.f, false, 6303 },
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
	float West = BedroomDoorX();
	for (const FCellarRoom& Room : Rooms())
	{
		West = FMath::Min(West, Room.CentreX - (Room.Width + WallThickness) * 0.5f);
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
		BuildRoomShell(Build, Room);
		if (Room.bBedroom)
		{
			SpawnBedroom(Room);
		}
		else
		{
			BuildBareRoom(Build, Room);
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
		Bedroom->Configure(RoomSetup);
		Bedroom->FinishSpawning(Transform);
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
		? FVector(Room.DoorX + Half - 6.f, Face - 2.6f, FloorZ())
		: FVector(Room.DoorX - Half + 6.f, Face + 2.6f, FloorZ());
	const FTransform Transform(FRotator(0.f, Room.bNorth ? 90.f : -90.f, 0.f), GetActorTransform().TransformPosition(Hinge));
	if (AHallDoorActor* Door = GetWorld()->SpawnActorDeferred<AHallDoorActor>(AHallDoorActor::StaticClass(), Transform, this))
	{
		FHallDoorSetup DoorSetup;
		DoorSetup.Width = Half * 2.f - 12.f;
		DoorSetup.Height = DoorHeight(Room) - 8.f;
		DoorSetup.AjarYaw = 4.f;
		DoorSetup.OpenYaw = 84.f;
		DoorSetup.Seed = Room.Seed;
		DoorSetup.WoodTint = FLinearColor(0.20f, 0.20f, 0.19f);
		// The bedroom's is a house door, panelled like the ones upstairs; the rest are cellar doors.
		DoorSetup.bSixPanel = Room.bBedroom;
		DoorSetup.bRotHole = (Room.Seed % 2) == 0;
		Door->Configure(DoorSetup);
		Door->FinishSpawning(Transform);
		Doors.Add(Door);
	}
}
