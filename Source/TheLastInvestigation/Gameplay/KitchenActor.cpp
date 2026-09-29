#include "KitchenActor.h"
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
	/** The painted band round the lower walls, and the rail that finishes it. */
	constexpr float KitchenDado = 128.f;

	/**
	 * The hall's chequered floor, laid finer. A kitchen floor was laid in smaller tiles than an
	 * entrance hall's, and at the hall's two-metre repeat a tile here was the size of a paving slab.
	 */
	const FRoomSurface KitchenTileSet{ TEXT("checkered_pavement_tiles"), 120.f };

	/** Collision that only a walking man meets: the interaction trace and the camera pass through. */
	void KitchenPawnOnly(UStaticMeshComponent* Part)
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

	/** A child scene component to build a group of parts in its own frame: the rack of pans. */
	USceneComponent* KitchenPivot(AActor* Owner, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation, const TCHAR* Name)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(Owner, MakeUniqueObjectName(Owner, USceneComponent::StaticClass(), Name));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		Pivot->SetRelativeLocationAndRotation(Location, Rotation);
		Pivot->RegisterComponent();
		Owner->AddInstanceComponent(Pivot);
		return Pivot;
	}

	/** A rod from A to B: a cylinder stands along its own Z, so the rod's direction is made the axis. */
	UStaticMeshComponent* KitchenRod(FRoomBuilder& Build, const FVector& A, const FVector& B, float Diameter, UMaterialInterface* Mat)
	{
		const FVector Step = B - A;
		return Build.Cyl((A + B) * 0.5f, FRotationMatrix::MakeFromZ(Step.GetSafeNormal()).Rotator(), FVector(Diameter, Diameter, Step.Size()), Mat, false);
	}

	/** A yaw that lays a part's local +X along Dir. */
	float KitchenYawOf(const FVector& Dir)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	}
}

AKitchenActor::AKitchenActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);
	RoomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void AKitchenActor::Configure(const FKitchenSetup& InSetup, AStormWindowActor* InLeadStorm)
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
		{ EWall::South, Setup.DoorX, Setup.DoorHalf, FloorZ(), FloorZ() + Setup.DoorHeight },
		// The two windows.
		{ EWall::North, WindowX(0), WindowWidth * 0.5f, FloorZ() + WindowSill, FloorZ() + WindowTop },
		{ EWall::North, WindowX(1), WindowWidth * 0.5f, FloorZ() + WindowSill, FloorZ() + WindowTop },
		// The chimney breast stands against the west wall rather than through it.
		{ EWall::West, HearthY(), BreastWidth * 0.5f, FloorZ(), CeilingZ(), /*bThroughWall*/ false },
	};
}

void AKitchenActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning this room never relays the hall, the living room or the bedroom.
	Random.Initialize(19861104);

	FRoomBuilder Build(this, RoomRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildFloor(Build);
	BuildWallFinish(Build);
	BuildCeiling(Build);
	BuildRange(Build);
	BuildCounters(Build);
	BuildSink(Build);
	BuildDresser(Build);
	BuildLarder(Build);
	BuildTable(Build);
	BuildPanRack(Build);
	BuildDamage(Build);
	BuildDebris(Build);

	SpawnWindows();
	BuildClues();
}

void AKitchenActor::CacheMaterials(FRoomBuilder& Build)
{
	// Whatever the living room has too is at the living room's values, so crossing the hall does
	// not change the house. New surfaces keep to the same rule: a tenth reflectance or under, and
	// warm, because the only fill in here is the cold light through two windows.
	MatPlaster = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f));
	// The lower walls were painted — a washable cream distemper, as every kitchen of the house's
	// age was — and it has faded and chalked. The cracked-concrete photograph (linear 0.517, 0.448,
	// 0.351) lands at about (0.10, 0.085, 0.053): cream gone to the colour of weak tea.
	MatDistemper = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.20f, 0.19f, 0.15f));
	MatWallpaper = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.160f, 0.132f, 0.108f));
	MatBrick = Build.Surface(RoomSurfaces::Substrate, FLinearColor(0.30f, 0.26f, 0.22f));
	// The hall's tint, a shade down: the white squares are the brightest thing a lantern finds on a
	// floor, and a kitchen floor is never as clean as a hall's.
	MatTiles = Build.Surface(KitchenTileSet, FLinearColor(0.58f, 0.56f, 0.54f), 1.1f);
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.19f, 0.18f, 0.17f));
	MatBeam = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.200f, 0.190f, 0.180f));
	// The cupboards are painted a grey that was once smart, over boarded pine. The paint is carried
	// on the planks photograph (linear 0.120, 0.073, 0.044), not on the plaster: painted on the
	// same cracked-concrete photograph as the walls, the dresser and the wall behind it were one
	// surface in two tints, and nothing separated them but a line. Solved to a grey a shade lighter
	// than the plaster at about (0.10, 0.10, 0.093) — still a touch warm, since a neutral grey in
	// front of a cold window comes back blue (the bedroom's 09-18 note).
	MatPaint = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.84f, 1.37f, 2.12f), 1.1f);
	MatPaintDark = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.34f, 0.55f, 0.84f), 1.2f);
	// The backs of the plate racks, painted dark green to show the china off, as dressers were: pale
	// plates against grey boards were one value, and a rack of plates is read by its plates.
	MatRackBack = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.15f, 0.44f, 0.52f), 1.2f);
	// Scrubbed deal for the worktops and the table: paler than the oak everywhere else, because a
	// kitchen table was scoured with sand and soda every day until the day it was not.
	MatScrubbed = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.320f, 0.300f, 0.270f));
	MatOakDark = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.190f, 0.180f, 0.170f));
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	// Blacklead long gone: the range is rust over a black that is only black in the corners.
	MatCastIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.34f, 0.18f, 0.28f), 1.1f);
	MatCopper = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.1f, 0.30f, 0.18f), 0.6f);
	MatBrass = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	// The living room's teacup: old china on the cracked plaster photograph, the cracks reading as
	// crazed glaze. Ivory at about 0.13 rather than the teacup's 0.07: at 0.07 a rack of plates was
	// the value of the paint it stood on and the plaster behind that.
	MatChina = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.276f, 0.296f, 0.323f), 0.9f);
	MatChinaDusty = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.197f, 0.216f, 0.249f), 1.1f);
	// The transfer-printed band round the everyday plates, faded blue. A flat colour, like the clock
	// dial's: a printed band is a flat thing by design.
	MatChinaBand = Build.Flat(FLinearColor(0.020f, 0.030f, 0.068f), 0.45f);
	// The mixing bowl: buff earthenware, its own instance because the bowl is a generated mesh
	// whose UVs are already in repeats (FRoomBuilder::Lathe).
	MatBowl = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.290f, 0.232f, 0.180f), 0.8f);
	// Jars are dusty glass, a good deal less clear than a window, and still nearly nothing.
	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.16f, 0.12f);
	MatPaper = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.464f, 0.245f, 0.108f));
	MatPaperDamp = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.260f, 0.142f, 0.065f));
	// A tea towel that was never washed again: the curtains' grey, dirtier.
	MatCloth = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.25f, 0.16f, 0.095f));
	MatRubble = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.145f, 0.127f, 0.110f));
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);
	MatShadow = Build.Flat(FLinearColor(0.002f, 0.002f, 0.002f), 1.f);
	MatSoot = Build.Surface(RoomSurfaces::Substrate, FLinearColor(0.05f, 0.045f, 0.042f));
	MatInk = Build.Flat(FLinearColor(0.018f, 0.016f, 0.020f), 0.9f);
	MatRedInk = Build.Flat(FLinearColor(0.085f, 0.012f, 0.010f), 0.8f);
	// What twenty years does to fruit: not a rotten apple, a brown wrinkled stone the size of a
	// walnut. Rot is shape and colour, never mess.
	MatRot = Build.Flat(FLinearColor(0.040f, 0.022f, 0.011f), 0.85f);
	MatRotDark = Build.Flat(FLinearColor(0.014f, 0.011f, 0.008f), 0.95f);
	MatMould = Build.Flat(FLinearColor(0.085f, 0.085f, 0.072f), 1.f);
	MatPreserve = Build.Flat(FLinearColor(0.028f, 0.007f, 0.005f), 0.3f);
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers, the hall's: U runs along a wall (X on north/south, Y on east/west), Z is
// world height, and a wall's normal points into the room.
// ---------------------------------------------------------------------------------------------

float AKitchenActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector AKitchenActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector AKitchenActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

float AKitchenActor::FacingYaw(EWall Wall)
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

bool AKitchenActor::IsOnOpening(EWall Wall, float U, float Z, float HalfU, float HalfZ) const
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

TArray<FBox2D> AKitchenActor::CutAround(EWall Wall, float U0, float U1, float Z0, float Z1, bool bThroughWallOnly) const
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

void AKitchenActor::WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud)
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

void AKitchenActor::WallBox(FRoomBuilder& Build, EWall Wall, float U, float Z, float SizeU, float SizeZ, float Depth, float ProudBase, UMaterialInterface* Mat)
{
	const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
	Build.Box(WallPoint(Wall, U, Z, ProudBase + Depth * 0.5f), FRotator::ZeroRotator,
		bAlongX ? FVector(SizeU, Depth, SizeZ) : FVector(Depth, SizeU, SizeZ), Mat, false);
}

void AKitchenActor::AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const
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

bool AKitchenActor::IsFloorSpotClear(float X, float Y, float Radius) const
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
// Small reusable pieces.
// ---------------------------------------------------------------------------------------------

void AKitchenActor::CupboardDoor(FRoomBuilder& Build, const FVector& Hinge, const FVector& Along, const FVector& Out, float Width, float Height, float OpenYaw)
{
	// Turning a door open by A about its hinge swings its free edge from Along towards Out, and its
	// face from Out towards -Along.
	const float A = FMath::DegreesToRadians(OpenYaw);
	const FVector Dir = Along * FMath::Cos(A) + Out * FMath::Sin(A);
	const FVector Face = Out * FMath::Cos(A) - Along * FMath::Sin(A);
	const FRotator Turn(0.f, KitchenYawOf(Dir), 0.f);
	const float Thick = 2.2f;
	const FVector Mid = Hinge + Dir * (Width * 0.5f) + Face * (Thick * 0.5f);

	Build.Box(Mid, Turn, FVector(Width, Thick, Height), MatPaint, false);
	// A sunk field inside a raised frame: the frame stands proud rather than the field being cut.
	const float Rail = 6.f;
	const FVector Proud = Face * (Thick * 0.5f + 0.5f);
	Build.Box(Mid + Proud + FVector(0.f, 0.f, Height * 0.5f - Rail * 0.5f), Turn, FVector(Width, 1.f, Rail), MatPaint, false);
	Build.Box(Mid + Proud - FVector(0.f, 0.f, Height * 0.5f - Rail * 0.5f), Turn, FVector(Width, 1.f, Rail), MatPaint, false);
	Build.Box(Mid + Proud + Dir * (Width * 0.5f - Rail * 0.5f), Turn, FVector(Rail, 1.f, Height), MatPaint, false);
	Build.Box(Mid + Proud - Dir * (Width * 0.5f - Rail * 0.5f), Turn, FVector(Rail, 1.f, Height), MatPaint, false);
	// The handle: a rusted iron knob near the free edge, a third of the way down.
	const FVector Knob = Mid + Dir * (Width * 0.5f - 7.f) + Face * (Thick * 0.5f + 1.8f) + FVector(0.f, 0.f, Height * 0.5f > 30.f ? Height * 0.5f - 18.f : 0.f);
	Build.Sph(Knob, 3.f, MatIron);
	Build.Cyl(Knob - Face * 1.2f, FRotationMatrix::MakeFromZ(Face).Rotator(), FVector(1.4f, 1.4f, 2.4f), MatIron, false);
	// A rust run down the paint from the knob.
	Build.Stain(RoomSurfaces::Damp, Knob - FVector(0.f, 0.f, 6.f) + Face * 2.f, FRotationMatrix::MakeFromX(-Face).Rotator(), FVector2D(3.f, 12.f),
		FLinearColor(0.16f, 0.07f, 0.03f), 0.55f, 1.1f);
}

void AKitchenActor::StandingPlate(FRoomBuilder& Build, const FVector& Foot, const FVector& Out, float Diameter, float Lean, UMaterialInterface* Mat, UMaterialInterface* Band)
{
	// Leaning back by Lean degrees, the plate's face tilts up and its top goes back towards the wall.
	const float L = FMath::DegreesToRadians(Lean);
	const FVector Normal = Out * FMath::Cos(L) + FVector::UpVector * FMath::Sin(L);
	const FVector Up = FVector::UpVector * FMath::Cos(L) - Out * FMath::Sin(L);
	const FRotator Rot = FRotationMatrix::MakeFromZ(Normal).Rotator();
	const FVector Middle = Foot + Up * (Diameter * 0.5f);
	Build.Cyl(Middle, Rot, FVector(Diameter, Diameter, 1.1f), Mat, false);
	if (Band)
	{
		// The printed band: a ring of colour inside the rim, left by laying the well over a disc of it.
		Build.Cyl(Middle + Normal * 0.6f, Rot, FVector(Diameter * 0.88f, Diameter * 0.88f, 0.2f), Band, false);
		Build.Cyl(Middle + Normal * 0.75f, Rot, FVector(Diameter * 0.68f, Diameter * 0.68f, 0.2f), Mat, false);
	}
	// The well of the plate, a shade darker, a hair in front of the rim.
	Build.Cyl(Middle + Normal * 0.9f, Rot, FVector(Diameter * 0.56f, Diameter * 0.56f, 0.2f), MatChinaDusty, false);
}

void AKitchenActor::Jar(FRoomBuilder& Build, const FVector& Base, float Diameter, float Height, float Fill, UMaterialInterface* Contents)
{
	if (Contents && Fill > 0.f)
	{
		const float Level = Height * Fill;
		Build.Cyl(Base + FVector(0.f, 0.f, Level * 0.5f + 0.3f), FRotator::ZeroRotator, FVector(Diameter - 1.2f, Diameter - 1.2f, Level), Contents, false);
	}
	Build.Cyl(Base + FVector(0.f, 0.f, Height * 0.5f), FRotator::ZeroRotator, FVector(Diameter, Diameter, Height), MatGlass, false);
	// The lid, rusted on.
	Build.Cyl(Base + FVector(0.f, 0.f, Height + 0.7f), FRotator::ZeroRotator, FVector(Diameter * 0.86f, Diameter * 0.86f, 1.6f), MatIron, false);
}

// ---------------------------------------------------------------------------------------------

void AKitchenActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float Z0 = FloorZ() - 20.f;
	const float Z1 = CeilingZ() + 10.f;

	// Solid, colliding and never seen, as in the hall. The south wall is the hall's north wall and
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
	Run(EWall::North, WestX() - T, EastX() + T, NorthY() - T * 0.5f);
	Run(EWall::East, NorthY() - T, SouthY() + T, EastX() + T * 0.5f);
	Run(EWall::West, NorthY() - T, SouthY() + T, WestX() - T * 0.5f);

	const FVector Span(EastX() - WestX() + T * 2.f, RoomDepth + T * 2.f, 20.f);
	Build.Box(FVector(MidX(), MidY(), FloorZ() - 14.f), FRotator::ZeroRotator, Span, MatShell);
	Build.Box(FVector(MidX(), MidY(), CeilingZ() + 10.f), FRotator::ZeroRotator, Span, MatShell);
}

void AKitchenActor::BuildFloor(FRoomBuilder& Build)
{
	const float F = FloorZ();

	// Black and white tiles, wall to wall. It collides; everything laid on it does not.
	Build.Box(FVector(MidX(), MidY(), F - 2.f), FRotator::ZeroRotator, FVector(EastX() - WestX(), RoomDepth, 4.f), MatTiles);

	// A few tiles gone altogether, by the sink and in front of the range, where water and heat got
	// under them: the screed shows, darker, and the broken pieces lie round the hole. The holes are
	// projected rather than cut, so their outline is a broken edge and not a square.
	struct FHole { float X; float Y; float SizeX; float SizeY; float Roll; };
	const FHole Holes[] = {
		{ WindowX(0) + 40.f, NorthY() + 110.f, 34.f, 26.f, 20.f },
		{ WindowX(0) - 70.f, NorthY() + 150.f, 22.f, 18.f, -35.f },
		{ BreastX() + 80.f, HearthY() + 50.f, 30.f, 24.f, 60.f },
		{ MidX() + 210.f, SouthY() - 170.f, 20.f, 16.f, 10.f },
	};
	FRandomStream Chips(5219);
	for (const FHole& Hole : Holes)
	{
		Build.Stain(RoomSurfaces::Substrate, FVector(Hole.X, Hole.Y, F + 4.f), FRotator(-90.f, 0.f, Hole.Roll), FVector2D(Hole.SizeY, Hole.SizeX),
			FLinearColor(0.12f, 0.105f, 0.09f), 1.f, 0.7f);
		Build.Crack(FVector(Hole.X, Hole.Y, F + 4.f), FRotator(-90.f, 0.f, Hole.Roll + 40.f), FVector2D(Hole.SizeY * 2.6f, Hole.SizeX * 2.6f), 0.95f, 30.f);
		for (int32 i = 0; i < 4; ++i)
		{
			const float A = Chips.FRandRange(0.f, 2.f * PI);
			const float R = Chips.FRandRange(14.f, 34.f);
			Build.Box(FVector(Hole.X + FMath::Cos(A) * R, Hole.Y + FMath::Sin(A) * R, F + 0.6f), FRotator(Chips.FRandRange(-4.f, 4.f), Chips.FRandRange(0.f, 360.f), Chips.FRandRange(-4.f, 4.f)),
				FVector(Chips.FRandRange(3.f, 8.f), Chips.FRandRange(2.f, 6.f), 1.1f), MatTiles, false);
		}
	}

	// Cracked tiles, not missing: hairlines across the floor where the house has moved.
	for (int32 i = 0; i < 9; ++i)
	{
		const float X = Random.FRandRange(WestX() + 90.f, EastX() - 90.f);
		const float Y = Random.FRandRange(NorthY() + 90.f, SouthY() - 90.f);
		Build.Crack(FVector(X, Y, F + 4.f), FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(40.f, 110.f), Random.FRandRange(40.f, 110.f)), Random.FRandRange(0.7f, 1.f), Random.FRandRange(26.f, 38.f));
	}
}

void AKitchenActor::BuildWallFinish(FRoomBuilder& Build)
{
	const float Z0 = FloorZ();
	const float Z1 = CeilingZ();
	const float Dado = Z0 + KitchenDado;

	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Walls[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), SouthY() },
	};

	for (const FRun& R : Walls)
	{
		// Plaster over every face, floor to ceiling, and the painted band over the lower part.
		WallFill(Build, R.Wall, R.U0, R.U1, Z0, Z1, MatPlaster);
		WallFill(Build, R.Wall, R.U0, R.U1, Z0, Dado, MatDistemper, 0.3f);

		// A plain board skirting, and a wooden rail finishing the paint.
		for (const FBox2D& Piece : CutAround(R.Wall, R.U0, R.U1, Z0, Dado + 4.f))
		{
			const FVector2D C = Piece.GetCenter();
			const FVector2D S = Piece.GetSize();
			if (Piece.Min.Y <= Z0 + 1.f && S.X > 1.f)
			{
				WallBox(Build, R.Wall, C.X, Z0 + 8.f, S.X, 16.f, 2.f, 0.3f, MatOakDark);
			}
			if (Piece.Max.Y >= Dado && S.X > 1.f)
			{
				WallBox(Build, R.Wall, C.X, Dado, S.X, 5.f, 2.6f, 0.3f, MatOakDark);
			}
		}

		// Wallpaper over the rail on the two walls with nothing high against them, and only where it
		// has held: one run on each, never the whole wall (the bedroom's 09-18 note). Kitchens were
		// papered last and stripped first; what is left is the odd width by the door.
		if (R.Wall == EWall::South || R.Wall == EWall::East)
		{
			const float StripWidth = 53.f;
			const float RunStart = R.Wall == EWall::South ? Setup.DoorX + 90.f : SouthY() - 20.f - 4.f * StripWidth;
			for (int32 Strip = 0; Strip < 4; ++Strip)
			{
				const float A = RunStart + Strip * StripWidth;
				if (A + StripWidth > R.U1 - 5.f)
				{
					break;
				}
				const float Bottom = Dado + 6.f + Random.FRandRange(0.f, 30.f);
				const float Top = Z1 - 40.f - Random.FRandRange(0.f, 70.f) - (Strip == 3 ? 90.f : 0.f);
				WallFill(Build, R.Wall, A, A + StripWidth - 0.3f, Bottom, Top, MatWallpaper, 0.5f);
			}
		}

		// A plain cornice: a kitchen was not a room anybody was shown.
		for (const FBox2D& Piece : CutAround(R.Wall, R.U0, R.U1, Z1 - 12.f, Z1))
		{
			WallBox(Build, R.Wall, Piece.GetCenter().X, Piece.GetCenter().Y, Piece.GetSize().X, Piece.GetSize().Y, 8.f, 0.f, MatPlaster);
		}
	}

	// The door from the hall, cased on this side.
	{
		const float U = Setup.DoorX;
		const float H = Z0 + Setup.DoorHeight;
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, EWall::South, U + S * (Setup.DoorHalf + 5.f), (Z0 + H + 10.f) * 0.5f, 10.f, H + 10.f - Z0, 2.6f, 0.3f, MatOakDark);
		}
		WallBox(Build, EWall::South, U, H + 5.f, Setup.DoorHalf * 2.f + 20.f, 10.f, 2.6f, 0.3f, MatOakDark);
	}

	// An architrave round each window, with a deep sill board over the worktop under it.
	for (int32 i = 0; i < 2; ++i)
	{
		const float U = WindowX(i);
		const float Sill = Z0 + WindowSill;
		const float Top = Z0 + WindowTop;
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, EWall::North, U + S * (WindowWidth * 0.5f + 6.f), (Sill + Top + 12.f) * 0.5f, 12.f, Top + 12.f - Sill, 2.8f, 0.f, MatOakDark);
		}
		WallBox(Build, EWall::North, U, Top + 6.f, WindowWidth + 24.f, 12.f, 2.8f, 0.f, MatOakDark);
		WallBox(Build, EWall::North, U, Sill - 2.f, WindowWidth + 30.f, 4.f, 6.f, 0.f, MatOakDark);
	}
}

void AKitchenActor::BuildCeiling(FRoomBuilder& Build)
{
	const float C = CeilingZ();
	Build.Mark(FVector(MidX(), MidY(), C), FRotator(0.f, 0.f, 180.f), FVector2D(EastX() - WestX(), RoomDepth), MatCeiling);

	// Beams across the room, north to south, the joists of the floor over it left bare, as kitchen
	// ceilings were: hooks were screwed into them for everything that had to hang.
	const int32 Beams = 6;
	for (int32 i = 0; i < Beams; ++i)
	{
		const float X = WestX() + (EastX() - WestX()) * (i + 0.5f) / Beams;
		Build.Box(FVector(X, MidY(), C - 11.f), FRotator::ZeroRotator, FVector(18.f, RoomDepth, 22.f), MatBeam, false);
	}

	// Water through the ceiling: blooms spreading from the window wall, and one dark over the sink.
	for (int32 i = 0; i < 7; ++i)
	{
		const float Y = (i < 3) ? NorthY() + Random.FRandRange(30.f, 240.f) : Random.FRandRange(NorthY() + 60.f, SouthY() - 60.f);
		Build.Stain(RoomSurfaces::Damp, FVector(Random.FRandRange(WestX() + 60.f, EastX() - 60.f), Y, C - 2.f), FRotator(90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(110.f, 240.f), Random.FRandRange(80.f, 180.f)), FLinearColor(0.32f, 0.27f, 0.21f), Random.FRandRange(0.35f, 0.6f), 1.2f);
	}
	Build.Stain(RoomSurfaces::Damp, FVector(WindowX(0), NorthY() + 120.f, C - 2.f), FRotator(90.f, 0.f, 70.f), FVector2D(260.f, 200.f),
		FLinearColor(0.20f, 0.16f, 0.12f), 0.7f, 1.2f);
	// Smoke from forty years of cooking, a brown cloud over the range that never came off.
	Build.Stain(RoomSurfaces::Damp, FVector(BreastX() + 110.f, HearthY(), C - 2.f), FRotator(90.f, 0.f, 0.f), FVector2D(320.f, 260.f),
		FLinearColor(0.10f, 0.08f, 0.06f), 0.55f, 1.3f);
	Build.Crack(FVector(MidX() + 120.f, MidY() + 100.f, C - 2.f), FRotator(90.f, 0.f, 20.f), FVector2D(300.f, 160.f), 0.8f, 16.f);

	// Cobwebs in the four upper corners, and slung between the beams.
	const FVector2D Corners[4] = { { WestX(), NorthY() }, { EastX(), NorthY() }, { WestX(), SouthY() }, { EastX(), SouthY() } };
	for (const FVector2D& Corner : Corners)
	{
		const float SX = Corner.X < MidX() ? 1.f : -1.f;
		const float SY = Corner.Y < MidY() ? 1.f : -1.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Inset = 24.f + i * 20.f;
			Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SX * Inset, Corner.Y + SY * Inset, C - 28.f - i * 8.f),
				FRotator(0.f, SX * SY > 0.f ? 45.f : -45.f, 180.f), FVector(Inset * 1.7f, Inset * 1.7f, 1.f), MatWeb, false);
		}
	}
	for (int32 i = 0; i < 6; ++i)
	{
		const float X = WestX() + (EastX() - WestX()) * (Random.RandRange(0, Beams - 2) + 1) / Beams;
		Build.Add(FRoomShapes::Plane(), FVector(X + 9.f + Random.FRandRange(10.f, 30.f), Random.FRandRange(NorthY() + 80.f, SouthY() - 80.f), C - 20.f),
			FRotator(0.f, 0.f, 90.f + Random.FRandRange(-20.f, 20.f)), FVector(Random.FRandRange(30.f, 60.f), 20.f, 1.f), MatWeb, false);
	}
}

void AKitchenActor::BuildRange(FRoomBuilder& Build)
{
	// A brick chimney breast in the middle of the west wall, and in the arch of it the old iron
	// range: the house's first kitchen and the heart of it, cold now. The family cooked on gas like
	// everyone else by the end, but the range was never taken out, and on it are the kettle and the
	// pan that were being used the evening this room stopped.
	const float F = FloorZ();
	const float C = CeilingZ();
	const float H = HearthY();
	const float Face = BreastX();
	const float HalfW = BreastWidth * 0.5f;
	const float HalfA = AlcoveWidth * 0.5f;
	const float ArchTop = F + AlcoveHeight;
	const float MidDepthX = (WestX() + Face) * 0.5f;

	// The piers and the stack over the arch. Each is turned so its local X runs up it: the builder
	// lays the larger repeat count along local X, and a pier four metres tall with its length on Z
	// came out as the brick photograph stretched into vertical stripes.
	const FRotator Upright(90.f, 0.f, 0.f);
	Build.Box(FVector(MidDepthX, H - (HalfW + HalfA) * 0.5f, (F + C) * 0.5f), Upright, FVector(C - F, HalfW - HalfA, BreastDepth), MatBrick);
	Build.Box(FVector(MidDepthX, H + (HalfW + HalfA) * 0.5f, (F + C) * 0.5f), Upright, FVector(C - F, HalfW - HalfA, BreastDepth), MatBrick);
	Build.Box(FVector(MidDepthX, H, (ArchTop + C) * 0.5f), Upright, FVector(C - ArchTop, AlcoveWidth, BreastDepth), MatBrick);
	// The back and the cheeks of the alcove, black with a century of soot.
	Build.Box(FVector(WestX() + 2.f, H, (F + ArchTop) * 0.5f), FRotator::ZeroRotator, FVector(4.f, AlcoveWidth, AlcoveHeight), MatSoot, false);
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(MidDepthX, H + S * (HalfA - 1.f), (F + ArchTop) * 0.5f), FRotator::ZeroRotator, FVector(BreastDepth, 2.f, AlcoveHeight), MatSoot, false);
	}
	Build.Box(FVector(MidDepthX, H, ArchTop - 1.f), FRotator::ZeroRotator, FVector(BreastDepth, AlcoveWidth, 2.f), MatSoot, false);
	// A timber lintel over the arch, and soot licked up the brick above it.
	Build.Box(FVector(Face + 2.f, H, ArchTop + 9.f), FRotator::ZeroRotator, FVector(6.f, AlcoveWidth + 30.f, 18.f), MatOakDark, false);
	Build.Stain(RoomSurfaces::Damp, FVector(Face + 6.f, H, ArchTop + 40.f), FRotator(0.f, 180.f, 0.f), FVector2D(150.f, 90.f), FLinearColor(0.05f, 0.045f, 0.04f), 0.7f, 1.2f);

	// The range: an iron box across the alcove, a hotplate on top, an oven either side of the fire.
	const float Depth = 60.f;
	const float Width = AlcoveWidth - 12.f;
	const float Top = F + 84.f;
	const float FrontX = Face + 8.f;
	const float BodyX = FrontX - Depth * 0.5f;
	Build.Box(FVector(BodyX, H, F + 4.f), FRotator::ZeroRotator, FVector(Depth + 4.f, Width + 4.f, 8.f), MatSoot);
	Build.Box(FVector(BodyX, H, (F + 8.f + Top) * 0.5f), FRotator::ZeroRotator, FVector(Depth, Width, Top - F - 8.f), MatCastIron);
	Build.Box(FVector(BodyX + 1.f, H, Top + 2.f), FRotator::ZeroRotator, FVector(Depth + 4.f, Width + 6.f, 4.f), MatCastIron, false);
	// The hotplate's lids, over the fire.
	for (const float Y : { -24.f, 24.f })
	{
		Build.Cyl(FVector(BodyX + 6.f, H + Y, Top + 4.3f), FRotator::ZeroRotator, FVector(24.f, 24.f, 0.8f), MatSoot, false);
		Build.Box(FVector(BodyX + 6.f, H + Y, Top + 4.9f), FRotator::ZeroRotator, FVector(8.f, 1.2f, 0.8f), MatCastIron, false);
	}
	// Two oven doors and the fire door between them, each with its brass latch.
	const float OvenY = Width * 0.5f - 30.f;
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(FrontX + 0.8f, H + S * OvenY, F + 46.f), FRotator::ZeroRotator, FVector(1.6f, 48.f, 50.f), MatCastIron, false);
		Build.Box(FVector(FrontX + 2.f, H + S * OvenY, F + 46.f), FRotator::ZeroRotator, FVector(0.8f, 36.f, 38.f), MatCastIron, false);
		Build.Box(FVector(FrontX + 3.2f, H + S * (OvenY - 17.f), F + 56.f), FRotator::ZeroRotator, FVector(2.f, 3.f, 10.f), MatBrass, false);
	}
	Build.Box(FVector(FrontX + 0.8f, H, F + 52.f), FRotator::ZeroRotator, FVector(1.6f, 34.f, 30.f), MatCastIron, false);
	for (int32 i = 0; i < 6; ++i)
	{
		// The fire door's grille, and behind it nothing but black.
		Build.Box(FVector(FrontX + 1.8f, H - 12.5f + i * 5.f, F + 52.f), FRotator::ZeroRotator, FVector(1.f, 1.6f, 22.f), MatIron, false);
	}
	Build.Box(FVector(FrontX + 0.2f, H, F + 52.f), FRotator::ZeroRotator, FVector(0.5f, 28.f, 22.f), MatShadow, false);
	// The ash pan, pulled half out and never emptied.
	Build.Box(FVector(FrontX + 6.f, H, F + 16.f), FRotator(0.f, 3.f, 0.f), FVector(22.f, 32.f, 8.f), MatCastIron, false);
	Build.Box(FVector(FrontX + 6.f, H, F + 20.2f), FRotator(0.f, 3.f, 0.f), FVector(19.f, 29.f, 0.5f), MatRubble, false);
	// A brass rail along the front, for the tea towels that are not on it.
	KitchenRod(Build, FVector(FrontX + 7.f, H - Width * 0.5f, Top - 10.f), FVector(FrontX + 7.f, H + Width * 0.5f, Top - 10.f), 2.2f, MatBrass);
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(FrontX + 3.5f, H + S * (Width * 0.5f - 2.f), Top - 10.f), FRotator::ZeroRotator, FVector(7.f, 1.6f, 1.6f), MatBrass, false);
	}
	// The flue, up out of the back of the hotplate and into the brick.
	Build.Cyl(FVector(WestX() + 16.f, H + 30.f, (Top + ArchTop) * 0.5f), FRotator::ZeroRotator, FVector(15.f, 15.f, ArchTop - Top), MatCastIron, false);
	Build.Cyl(FVector(WestX() + 16.f, H + 30.f, Top + 26.f), FRotator::ZeroRotator, FVector(18.f, 18.f, 3.f), MatCastIron, false);

	// On the hotplate: the kettle, and a pan with its lid half off and a black crust in the bottom.
	{
		const FVector K(BodyX + 6.f, H - 24.f, Top + 4.7f);
		Build.Cyl(K + FVector(0.f, 0.f, 8.f), FRotator::ZeroRotator, FVector(22.f, 22.f, 16.f), MatCastIron, false);
		Build.Add(FRoomShapes::Sphere(), K + FVector(0.f, 0.f, 16.f), FRotator::ZeroRotator, FVector(20.f, 20.f, 8.f), MatCastIron, false);
		Build.Sph(K + FVector(0.f, 0.f, 20.5f), 3.f, MatBrass);
		KitchenRod(Build, K + FVector(8.f, 0.f, 6.f), K + FVector(18.f, 0.f, 16.f), 3.2f, MatCastIron);
		// The bail handle: over the top, side to side.
		KitchenRod(Build, K + FVector(0.f, -9.f, 16.f), K + FVector(0.f, -6.f, 27.f), 1.1f, MatIron);
		KitchenRod(Build, K + FVector(0.f, -6.f, 27.f), K + FVector(0.f, 6.f, 27.f), 1.1f, MatIron);
		KitchenRod(Build, K + FVector(0.f, 6.f, 27.f), K + FVector(0.f, 9.f, 16.f), 1.1f, MatIron);
	}
	{
		const FVector P(BodyX + 6.f, H + 24.f, Top + 4.7f);
		Build.Cyl(P + FVector(0.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(22.f, 22.f, 12.f), MatCopper, false);
		Build.Cyl(P + FVector(0.f, 0.f, 12.05f), FRotator::ZeroRotator, FVector(20.f, 20.f, 0.1f), MatRotDark, false);
		KitchenRod(Build, P + FVector(0.f, 10.f, 10.f), P + FVector(0.f, 30.f, 12.f), 2.2f, MatCopper);
		Build.Cyl(P + FVector(5.f, -3.f, 13.5f), FRotator(0.f, 0.f, 9.f), FVector(22.f, 22.f, 1.f), MatCopper, false);
		Build.Sph(P + FVector(5.f, -3.f, 15.f), 3.f, MatCopper);
	}

	// The mantel shelf over the arch, on iron brackets, and a rail under it hung with what was used
	// every day: a frying pan, the copper saucepans in a row, a ladle, a skimmer.
	const float Shelf = F + ShelfHeight;
	Build.Box(FVector(Face + 12.f, H, Shelf - 2.f), FRotator::ZeroRotator, FVector(24.f, BreastWidth + 16.f, 4.f), MatOakDark);
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(Face + 7.f, H + S * (HalfW - 20.f), Shelf - 12.f), FRotator(0.f, 0.f, 0.f), FVector(14.f, 2.f, 16.f), MatIron, false);
	}
	Build.Stain(RoomSurfaces::Damp, FVector(Face + 12.f, H, Shelf + 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(BreastWidth, 22.f), FLinearColor(0.40f, 0.38f, 0.35f), 0.42f, 1.2f);

	const float RailZ = Shelf - 22.f;
	const float RailX = Face + 5.f;
	KitchenRod(Build, FVector(RailX, H - HalfW + 12.f, RailZ), FVector(RailX, H + HalfW - 12.f, RailZ), 1.6f, MatIron);
	struct FHung { float Y; float Diameter; float Depth; float HandleLength; bool bCopper; };
	const FHung Hung[] = {
		{ -110.f, 30.f, 4.f, 22.f, false },   // the frying pan
		{ -70.f, 20.f, 11.f, 14.f, true },
		{ -38.f, 17.f, 9.f, 13.f, true },
		{ -10.f, 14.f, 8.f, 12.f, true },
		{ 62.f, 24.f, 12.f, 16.f, true },
		{ 104.f, 26.f, 4.f, 20.f, false },
	};
	for (const FHung& Pan : Hung)
	{
		// An S-hook, the handle hanging straight down from it, and the pan flat against the brick.
		const FVector Hook(RailX, H + Pan.Y, RailZ);
		Build.Box(Hook - FVector(0.f, 0.f, 3.f), FRotator::ZeroRotator, FVector(0.8f, 0.8f, 6.f), MatIron, false);
		UMaterialInterface* Metal = Pan.bCopper ? MatCopper.Get() : MatCastIron.Get();
		const float HandleTop = RailZ - 6.f;
		const float HandleBottom = HandleTop - Pan.HandleLength;
		Build.Box(FVector(RailX + Pan.Depth * 0.5f, H + Pan.Y, (HandleTop + HandleBottom) * 0.5f), FRotator::ZeroRotator, FVector(1.2f, 2.6f, Pan.HandleLength), Metal, false);
		Build.Cyl(FVector(RailX + Pan.Depth * 0.5f + 0.5f, H + Pan.Y, HandleBottom - Pan.Diameter * 0.5f), FRotator(90.f, 0.f, 0.f), FVector(Pan.Diameter, Pan.Diameter, Pan.Depth), Metal, false);
	}
	// The ladle and the skimmer: long handles and small bowls.
	for (const float Y : { 18.f, 34.f })
	{
		Build.Box(FVector(RailX + 2.f, H + Y, RailZ - 22.f), FRotator::ZeroRotator, FVector(0.8f, 1.6f, 34.f), MatIron, false);
		Build.Add(FRoomShapes::Sphere(), FVector(RailX + 4.f, H + Y, RailZ - 43.f), FRotator::ZeroRotator, FVector(9.f, 10.f, 8.f), MatIron, false);
	}

	// On the shelf: a row of tins and a stoneware jug, and the photograph (BuildClues).
	struct FTin { float Y; float D; float Hgt; };
	const FTin Tins[] = { { -118.f, 11.f, 16.f }, { -104.f, 9.f, 13.f }, { -90.f, 10.f, 18.f }, { 70.f, 12.f, 14.f } };
	for (const FTin& Tin : Tins)
	{
		Build.Cyl(FVector(Face + 12.f, H + Tin.Y, Shelf + Tin.Hgt * 0.5f), FRotator::ZeroRotator, FVector(Tin.D, Tin.D, Tin.Hgt), MatIron, false);
		Build.Cyl(FVector(Face + 12.f, H + Tin.Y, Shelf + Tin.Hgt + 0.5f), FRotator::ZeroRotator, FVector(Tin.D + 0.6f, Tin.D + 0.6f, 1.2f), MatIron, false);
	}
	Build.Cyl(FVector(Face + 12.f, H + 100.f, Shelf + 9.f), FRotator::ZeroRotator, FVector(14.f, 14.f, 18.f), MatChinaDusty, false);
	Build.Add(FRoomShapes::Sphere(), FVector(Face + 12.f, H + 100.f, Shelf + 18.f), FRotator::ZeroRotator, FVector(14.f, 14.f, 7.f), MatChinaDusty, false);
	Build.Box(FVector(Face + 12.f, H + 108.5f, Shelf + 11.f), FRotator::ZeroRotator, FVector(1.6f, 3.f, 10.f), MatChinaDusty, false);

	// The breast and the range, and the working space in front of them the debris keeps out of.
	Footprints.Add(FBox2D(FVector2D(WestX(), H - HalfW - 10.f), FVector2D(Face + 40.f, H + HalfW + 10.f)));
}

void AKitchenActor::BuildCounters(FRoomBuilder& Build)
{
	// Worktops on painted cupboards along the window wall and along the door wall west of the door.
	// Each run is a row of bays: a shut bay is a solid carcass behind a door; a bay whose door has
	// come open is built hollow, so what was left in it can be seen.
	const float F = FloorZ();
	const float Top = F + CounterHeight;
	const float Bay = 60.f;
	UMaterialInterface* Earthen = Build.Flat(FLinearColor(0.07f, 0.045f, 0.028f), 0.9f);

	struct FRunSpec { EWall Wall; float U0; float U1; };
	// The sink sits in a gap in the north run under the west window (BuildSink).
	const float SinkHalf = 44.f;
	const FRunSpec Runs[] = {
		{ EWall::North, WestX(), WindowX(0) - SinkHalf },
		{ EWall::North, WindowX(0) + SinkHalf, EastX() },
		{ EWall::South, WestX(), SouthCounterEndX() },
	};

	int32 BayIndex = 0;
	for (const FRunSpec& R : Runs)
	{
		const FVector N = WallNormal(R.Wall);
		const FVector Along = (R.Wall == EWall::North || R.Wall == EWall::South) ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
		const float Length = R.U1 - R.U0;
		const int32 Bays = FMath::Max(1, FMath::FloorToInt(Length / Bay));
		const float BayWidth = Length / Bays;

		// The worktop, one board the length of the run, and the dark recess of the plinth.
		const FVector TopMid = WallPoint(R.Wall, (R.U0 + R.U1) * 0.5f, Top - 2.f, CounterDepth * 0.5f + 1.f);
		Build.Box(TopMid, FRotator::ZeroRotator, R.Wall == EWall::North || R.Wall == EWall::South ? FVector(Length, CounterDepth + 2.f, 4.f) : FVector(CounterDepth + 2.f, Length, 4.f), MatScrubbed, false);
		const FVector PlinthMid = WallPoint(R.Wall, (R.U0 + R.U1) * 0.5f, F + 5.f, (CounterDepth - 6.f) * 0.5f);
		Build.Box(PlinthMid, FRotator::ZeroRotator, R.Wall == EWall::North || R.Wall == EWall::South ? FVector(Length, CounterDepth - 6.f, 10.f) : FVector(CounterDepth - 6.f, Length, 10.f), MatShadow, false);
		// What stops a man walking into it.
		KitchenPawnOnly(Build.Box(WallPoint(R.Wall, (R.U0 + R.U1) * 0.5f, F + CounterHeight * 0.5f, CounterDepth * 0.5f), FRotator::ZeroRotator,
			R.Wall == EWall::North || R.Wall == EWall::South ? FVector(Length, CounterDepth, CounterHeight) : FVector(CounterDepth, Length, CounterHeight), MatVoid));

		const float CarcassDepth = CounterDepth - 4.f;
		const float DoorBottom = F + 12.f;
		const float DoorTop = Top - 16.f;
		for (int32 b = 0; b < Bays; ++b, ++BayIndex)
		{
			const float U0 = R.U0 + b * BayWidth;
			const float U = U0 + BayWidth * 0.5f;
			const bool bOpen = (BayIndex % 5 == 2) || (BayIndex % 7 == 5);
			const float Z0 = F + 10.f;
			const float Z1 = Top - 4.f;

			// The drawer rail under the worktop, with a drawer to each bay.
			const FVector DrawerFace = WallPoint(R.Wall, U, Top - 10.f, CarcassDepth + 0.8f);
			Build.Box(DrawerFace, FRotator::ZeroRotator, R.Wall == EWall::North || R.Wall == EWall::South ? FVector(BayWidth - 3.f, 1.6f, 11.f) : FVector(1.6f, BayWidth - 3.f, 11.f), MatPaint, false);
			Build.Sph(DrawerFace + N * 1.8f, 2.6f, MatBrass);

			if (!bOpen)
			{
				Build.Box(WallPoint(R.Wall, U, (Z0 + Z1) * 0.5f, CarcassDepth * 0.5f), FRotator::ZeroRotator,
					R.Wall == EWall::North || R.Wall == EWall::South ? FVector(BayWidth, CarcassDepth, Z1 - Z0) : FVector(CarcassDepth, BayWidth, Z1 - Z0), MatPaint, false);
				CupboardDoor(Build, WallPoint(R.Wall, U0 + 2.f, (DoorBottom + DoorTop) * 0.5f, CarcassDepth), Along, N, BayWidth - 4.f, DoorTop - DoorBottom, BayIndex % 4 == 1 ? 6.f : 0.f);
				continue;
			}

			// A hollow bay: back, floor, a shelf, the two sides, and dark paint inside.
			const bool bAlongX = R.Wall == EWall::North || R.Wall == EWall::South;
			auto Panel = [&](float PU, float PZ, float PProud, float SU, float SZ, float SDepth)
			{
				Build.Box(WallPoint(R.Wall, PU, PZ, PProud), FRotator::ZeroRotator, bAlongX ? FVector(SU, SDepth, SZ) : FVector(SDepth, SU, SZ), MatPaintDark, false);
			};
			Panel(U, (Z0 + Z1) * 0.5f, 1.f, BayWidth, Z1 - Z0, 2.f);
			Panel(U, Z0 + 1.f, CarcassDepth * 0.5f, BayWidth, 2.f, CarcassDepth);
			Panel(U, (Z0 + Z1) * 0.5f + 4.f, CarcassDepth * 0.5f - 4.f, BayWidth - 4.f, 2.f, CarcassDepth - 8.f);
			Panel(U, Z1 - 1.f, CarcassDepth * 0.5f, BayWidth, 2.f, CarcassDepth);
			Panel(U0 + 1.f, (Z0 + Z1) * 0.5f, CarcassDepth * 0.5f, 2.f, Z1 - Z0, CarcassDepth);
			Panel(U0 + BayWidth - 1.f, (Z0 + Z1) * 0.5f, CarcassDepth * 0.5f, 2.f, Z1 - Z0, CarcassDepth);
			CupboardDoor(Build, WallPoint(R.Wall, U0 + 2.f, (DoorBottom + DoorTop) * 0.5f, CarcassDepth), Along, N, BayWidth - 4.f, DoorTop - DoorBottom, BayIndex % 2 ? 58.f : 24.f);

			// What was left in it: a stack of plates below, jars and a crock on the shelf, a web.
			const float ShelfZ = (Z0 + Z1) * 0.5f + 5.f;
			for (int32 p = 0; p < 6; ++p)
			{
				Build.Cyl(WallPoint(R.Wall, U - 8.f, Z0 + 2.8f + p * 1.3f, CarcassDepth * 0.5f), FRotator::ZeroRotator, FVector(24.f, 24.f, 1.2f), MatChinaDusty, false);
			}
			Build.Cyl(WallPoint(R.Wall, U + 16.f, Z0 + 12.f, CarcassDepth * 0.5f - 6.f), FRotator::ZeroRotator, FVector(18.f, 18.f, 20.f), Earthen, false);
			Jar(Build, WallPoint(R.Wall, U - 14.f, ShelfZ, CarcassDepth * 0.5f - 8.f), 9.f, 14.f, 0.3f, MatRot);
			Jar(Build, WallPoint(R.Wall, U, ShelfZ, CarcassDepth * 0.5f - 6.f), 8.f, 11.f, 0.f, nullptr);
			Jar(Build, WallPoint(R.Wall, U + 13.f, ShelfZ, CarcassDepth * 0.5f - 10.f), 10.f, 16.f, 0.55f, MatPreserve);
			Build.Add(FRoomShapes::Plane(), WallPoint(R.Wall, U + BayWidth * 0.3f, Z1 - 8.f, CarcassDepth * 0.6f),
				FRotator(0.f, bAlongX ? 0.f : 90.f, 90.f), FVector(BayWidth * 0.5f, 14.f, 1.f), MatWeb, false);
		}

		const FVector2D FootA = FVector2D(WallPoint(R.Wall, R.U0, 0.f, 0.f));
		const FVector2D FootB = FVector2D(WallPoint(R.Wall, R.U1, 0.f, CounterDepth + 10.f));
		Footprints.Add(FBox2D(FVector2D(FMath::Min(FootA.X, FootB.X), FMath::Min(FootA.Y, FootB.Y)), FVector2D(FMath::Max(FootA.X, FootB.X), FMath::Max(FootA.Y, FootB.Y))));
	}

	// Over the north run: an open plate rack between the windows, a wall cupboard east of them with
	// its door hanging open, and a shelf of jars west of them.
	const float RackU = MidX();
	const float RackW = WindowSpacing - WindowWidth - 40.f;
	const float RackZ0 = F + 150.f;
	const float RackZ1 = F + 270.f;
	WallFill(Build, EWall::North, RackU - RackW * 0.5f, RackU + RackW * 0.5f, RackZ0, RackZ1, MatRackBack, 0.6f);
	for (const float S : { -1.f, 1.f })
	{
		WallBox(Build, EWall::North, RackU + S * (RackW * 0.5f - 1.5f), (RackZ0 + RackZ1) * 0.5f, 3.f, RackZ1 - RackZ0, 26.f, 0.f, MatPaint);
	}
	WallBox(Build, EWall::North, RackU, RackZ1 + 3.f, RackW + 10.f, 6.f, 30.f, 0.f, MatPaint);
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float Z = RackZ0 + Row * 40.f;
		WallBox(Build, EWall::North, RackU, Z + 1.f, RackW - 6.f, 2.f, 24.f, 0.f, MatPaint);
		// A rail across the front to keep the plates in.
		WallBox(Build, EWall::North, RackU, Z + 7.f, RackW - 6.f, 1.4f, 1.4f, 22.f, MatPaint);
		const int32 Plates = 8 - Row * 2;
		for (int32 p = 0; p < Plates; ++p)
		{
			// Gaps where a plate has gone, and plates of two sizes.
			if ((p + Row) % 5 == 3)
			{
				continue;
			}
			const float Dia = Row == 0 ? 26.f : (Row == 1 ? 22.f : 18.f);
			const float U = RackU - RackW * 0.5f + 14.f + p * (RackW - 28.f) / FMath::Max(1, Plates - 1);
			StandingPlate(Build, WallPoint(EWall::North, U, Z + 2.f, 12.f), FVector(0.f, 1.f, 0.f), Dia, 14.f + (p % 3) * 2.f, p % 4 == 0 ? MatChinaDusty.Get() : MatChina.Get(),
				(p + Row) % 3 == 1 ? nullptr : MatChinaBand.Get());
		}
	}

	// The wall cupboard east of the east window, door hanging open on one hinge.
	{
		const float U0 = WindowX(1) + WindowWidth * 0.5f + 30.f;
		const float U1 = EastX() - 10.f;
		const float Z0 = F + 160.f;
		const float Z1 = F + 262.f;
		const float Depth = 32.f;
		auto Panel = [&](float PU, float PZ, float PProud, float SU, float SZ, float SDepth, UMaterialInterface* Mat)
		{
			Build.Box(WallPoint(EWall::North, PU, PZ, PProud), FRotator::ZeroRotator, FVector(SU, SDepth, SZ), Mat, false);
		};
		const float U = (U0 + U1) * 0.5f;
		Panel(U, (Z0 + Z1) * 0.5f, 1.f, U1 - U0, Z1 - Z0, 2.f, MatPaintDark);
		Panel(U, Z0 + 1.f, Depth * 0.5f, U1 - U0, 2.f, Depth, MatPaint);
		Panel(U, Z1 - 1.f, Depth * 0.5f, U1 - U0, 2.f, Depth, MatPaint);
		Panel(U, (Z0 + Z1) * 0.5f, Depth * 0.5f - 2.f, U1 - U0 - 4.f, 1.6f, Depth - 4.f, MatPaintDark);
		Panel(U0 + 1.f, (Z0 + Z1) * 0.5f, Depth * 0.5f, 2.f, Z1 - Z0, Depth, MatPaint);
		Panel(U1 - 1.f, (Z0 + Z1) * 0.5f, Depth * 0.5f, 2.f, Z1 - Z0, Depth, MatPaint);
		// Hinged on its east side, swung round towards the window: the one hinge left has let it drop
		// a finger's width, so it hangs a degree out of its frame.
		CupboardDoor(Build, WallPoint(EWall::North, U1 - 2.f, (Z0 + Z1) * 0.5f - 1.5f, Depth), FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), U1 - U0 - 4.f, Z1 - Z0 - 4.f, 78.f);
		const float Mid = (Z0 + Z1) * 0.5f - 1.f;
		for (int32 i = 0; i < 4; ++i)
		{
			Jar(Build, WallPoint(EWall::North, U0 + 22.f + i * 26.f, Z0 + 2.f, Depth * 0.5f), 10.f, 15.f + (i % 2) * 4.f, i == 2 ? 0.f : 0.25f + i * 0.15f, i % 2 ? MatPreserve.Get() : MatRot.Get());
		}
		for (int32 i = 0; i < 3; ++i)
		{
			// Cups on the upper shelf, one on its side.
			const FVector Cup = WallPoint(EWall::North, U0 + 30.f + i * 40.f, Mid + 0.8f, Depth * 0.5f);
			if (i == 1)
			{
				Build.Cyl(Cup + FVector(0.f, 0.f, 4.f), FRotator(0.f, 0.f, 90.f), FVector(8.f, 8.f, 7.f), MatChina, false);
			}
			else
			{
				Build.Cyl(Cup + FVector(0.f, 0.f, 3.5f), FRotator::ZeroRotator, FVector(8.f, 8.f, 7.f), MatChina, false);
				Build.Box(Cup + FVector(4.5f, 0.f, 4.f), FRotator::ZeroRotator, FVector(1.6f, 0.8f, 4.f), MatChina, false);
			}
		}
		Build.Add(FRoomShapes::Plane(), WallPoint(EWall::North, U0 + 16.f, Z1 - 12.f, Depth * 0.5f), FRotator(0.f, 45.f, 90.f), FVector(30.f, 20.f, 1.f), MatWeb, false);
	}

	// A shelf of empty jars over the worktop west of the sink window.
	{
		const float U0 = WestX() + 12.f;
		const float U1 = WindowX(0) - WindowWidth * 0.5f - 24.f;
		const float Z = F + 178.f;
		WallBox(Build, EWall::North, (U0 + U1) * 0.5f, Z, U1 - U0, 3.f, 24.f, 0.f, MatOakDark);
		for (const float U : { U0 + 18.f, U1 - 18.f })
		{
			WallBox(Build, EWall::North, U, Z - 9.f, 2.f, 16.f, 18.f, 0.f, MatIron);
		}
		const int32 Count = FMath::FloorToInt((U1 - U0 - 20.f) / 18.f);
		for (int32 i = 0; i < Count; ++i)
		{
			if (i % 4 == 2)
			{
				continue;
			}
			Jar(Build, WallPoint(EWall::North, U0 + 16.f + i * 18.f, Z + 1.5f, 12.f), 11.f, 16.f + (i % 3) * 3.f, i % 5 == 0 ? 0.2f : 0.f, MatRot);
		}
	}

	// On the south run: a bread crock, a coffee mill, a pestle and mortar. The recipe book is a clue
	// (BuildClues), and so is what is pinned over it.
	{
		const float Y = SouthY() - CounterDepth * 0.5f;
		Build.Cyl(FVector(WestX() + 50.f, Y, Top + 16.f), FRotator::ZeroRotator, FVector(34.f, 34.f, 32.f), Earthen, false);
		Build.Cyl(FVector(WestX() + 50.f, Y, Top + 33.f), FRotator(0.f, 0.f, 4.f), FVector(36.f, 36.f, 3.f), Earthen, false);
		Build.Sph(FVector(WestX() + 50.f, Y, Top + 36.f), 6.f, Earthen);
		// The coffee mill: a wooden box, a brass hopper, the crank.
		const FVector Mill(WestX() + 130.f, Y + 8.f, Top);
		Build.Box(Mill + FVector(0.f, 0.f, 8.f), FRotator(0.f, 12.f, 0.f), FVector(15.f, 15.f, 16.f), MatOakDark, false);
		Build.Cyl(Mill + FVector(0.f, 0.f, 18.f), FRotator::ZeroRotator, FVector(12.f, 12.f, 5.f), MatBrass, false);
		KitchenRod(Build, Mill + FVector(0.f, 0.f, 20.f), Mill + FVector(0.f, 0.f, 26.f), 1.2f, MatIron);
		KitchenRod(Build, Mill + FVector(0.f, 0.f, 26.f), Mill + FVector(9.f, 3.f, 26.f), 1.2f, MatIron);
		Build.Cyl(Mill + FVector(9.f, 3.f, 29.f), FRotator::ZeroRotator, FVector(2.2f, 2.2f, 5.f), MatOakDark, false);
		const FVector Mortar(SouthCounterEndX() - 40.f, Y + 4.f, Top);
		Build.Cyl(Mortar + FVector(0.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(16.f, 16.f, 12.f), MatChinaDusty, false);
		Build.Cyl(Mortar + FVector(0.f, 0.f, 12.05f), FRotator::ZeroRotator, FVector(12.f, 12.f, 0.1f), MatShadow, false);
		KitchenRod(Build, Mortar + FVector(-1.f, 0.f, 9.f), Mortar + FVector(-7.f, 6.f, 21.f), 3.f, MatChinaDusty);
		Build.Stain(RoomSurfaces::Damp, FVector(WestX() + 200.f, Y, Top + 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(CounterDepth, 380.f), FLinearColor(0.40f, 0.38f, 0.35f), 0.4f, 1.3f);
	}
}

void AKitchenActor::BuildSink(FRoomBuilder& Build)
{
	// A deep glazed stoneware sink in the gap in the north run, under the window so whoever stood at
	// it looked out at the trees: on brick piers, open underneath, the pipes on show. Dishes were
	// left in it, and the tap was not turned quite off — the stain of it is down the glaze.
	const float F = FloorZ();
	const float U = WindowX(0);
	const float Rim = F + CounterHeight + 2.f;
	const float Y = NorthY() + CounterDepth * 0.5f;
	const float W = 76.f;
	const float D = 50.f;
	const float Deep = 26.f;
	const float Base = Rim - Deep;

	Build.Box(FVector(U, Y, Base + 2.f), FRotator::ZeroRotator, FVector(W, D, 4.f), MatChina);
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(U, Y + S * (D * 0.5f - 2.f), Base + Deep * 0.5f), FRotator::ZeroRotator, FVector(W, 4.f, Deep), MatChina);
		Build.Box(FVector(U + S * (W * 0.5f - 2.f), Y, Base + Deep * 0.5f), FRotator::ZeroRotator, FVector(4.f, D - 8.f, Deep), MatChina);
	}
	// Grime in the bottom, the tap's rust run down the back, and a dark tide line.
	Build.Stain(RoomSurfaces::Damp, FVector(U, Y, Base + 10.f), FRotator(-90.f, 0.f, 0.f), FVector2D(D - 8.f, W - 8.f), FLinearColor(0.10f, 0.08f, 0.06f), 0.75f, 1.1f, 0.4f);
	Build.Stain(RoomSurfaces::Damp, FVector(U + 10.f, Y - D * 0.5f + 12.f, Base + Deep * 0.5f), FRotator(0.f, -90.f, 0.f), FVector2D(8.f, Deep), FLinearColor(0.16f, 0.07f, 0.03f), 0.7f, 1.1f);

	// The piers it stands on, and the gap under it.
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(U + S * (W * 0.5f - 8.f), Y, (F + Base) * 0.5f), FRotator(90.f, 0.f, 0.f), FVector(Base - F, D - 6.f, 16.f), MatBrick);
	}
	KitchenPawnOnly(Build.Box(FVector(U, Y, (F + Rim) * 0.5f), FRotator::ZeroRotator, FVector(W + 10.f, CounterDepth, Rim - F), MatVoid));

	// The taps: two brass pillars off the supply pipes, rising behind the sink.
	for (const float S : { -1.f, 1.f })
	{
		const FVector Tap(U + S * 16.f, NorthY() + 4.f, Rim);
		Build.Cyl(Tap + FVector(0.f, 0.f, 9.f), FRotator::ZeroRotator, FVector(3.2f, 3.2f, 18.f), MatBrass, false);
		KitchenRod(Build, Tap + FVector(0.f, 0.f, 16.f), Tap + FVector(0.f, 14.f, 12.f), 2.4f, MatBrass);
		Build.Box(Tap + FVector(0.f, 0.f, 20.f), FRotator(0.f, S * 20.f, 0.f), FVector(9.f, 1.4f, 1.4f), MatBrass, false);
		Build.Box(Tap + FVector(0.f, 0.f, 20.f), FRotator(0.f, S * 20.f + 90.f, 0.f), FVector(9.f, 1.4f, 1.4f), MatBrass, false);
		// The supply pipe, down the wall under the sink and into the floor.
		KitchenRod(Build, FVector(Tap.X, NorthY() + 4.f, F), FVector(Tap.X, NorthY() + 4.f, Rim), 2.6f, MatIron);
	}
	// The waste: out of the bottom of the sink, down, a trap — and the lower pipe come away from it
	// and lying on the tiles in the stain it left.
	KitchenRod(Build, FVector(U + 20.f, Y + 6.f, Base), FVector(U + 20.f, Y + 6.f, Base - 22.f), 5.f, MatIron);
	Build.Add(FRoomShapes::Sphere(), FVector(U + 20.f, Y + 6.f, Base - 24.f), FRotator::ZeroRotator, FVector(7.f, 7.f, 7.f), MatIron, false);
	KitchenRod(Build, FVector(U + 20.f, Y + 6.f, Base - 24.f), FVector(U + 20.f, NorthY() + 3.f, Base - 24.f), 5.f, MatIron);
	KitchenRod(Build, FVector(U - 8.f, Y + 24.f, F + 3.f), FVector(U + 28.f, Y + 12.f, F + 2.6f), 5.f, MatIron);
	Build.Stain(RoomSurfaces::Damp, FVector(U + 6.f, Y + 30.f, F + 4.f), FRotator(-90.f, 0.f, 30.f), FVector2D(90.f, 130.f), FLinearColor(0.10f, 0.085f, 0.07f), 0.8f, 1.2f, 0.3f);

	// In the sink: plates stacked in the water that is not there any more, a cup, a pan.
	for (int32 i = 0; i < 4; ++i)
	{
		Build.Cyl(FVector(U - 12.f + i * 0.6f, Y + 2.f, Base + 4.6f + i * 1.3f), FRotator(0.f, 0.f, 4.f + i * 2.f), FVector(24.f, 24.f, 1.2f), i % 2 ? MatChina.Get() : MatChinaDusty.Get(), false);
	}
	Build.Cyl(FVector(U + 18.f, Y - 8.f, Base + 8.f), FRotator(0.f, 0.f, 70.f), FVector(8.f, 8.f, 7.f), MatChina, false);
	Build.Cyl(FVector(U + 16.f, Y + 12.f, Base + 9.f), FRotator(0.f, 30.f, 12.f), FVector(18.f, 18.f, 9.f), MatCopper, false);

	// The draining board to the east of it: grooved wood, and a rack of plates still standing to dry.
	const float BoardU = U + W * 0.5f + 40.f;
	Build.Box(FVector(BoardU, Y, Rim - 0.5f), FRotator(0.f, 0.f, 0.f), FVector(80.f, CounterDepth - 4.f, 1.6f), MatScrubbed, false);
	for (int32 i = 0; i < 7; ++i)
	{
		Build.Box(FVector(BoardU - 30.f + i * 10.f, Y, Rim + 0.35f), FRotator::ZeroRotator, FVector(1.2f, CounterDepth - 12.f, 0.2f), MatShadow, false);
	}
	for (int32 i = 0; i < 5; ++i)
	{
		StandingPlate(Build, FVector(BoardU - 20.f + i * 8.f, Y + 6.f, Rim + 0.5f), FVector(1.f, 0.f, 0.f), 24.f, 12.f, i % 2 ? MatChina.Get() : MatChinaDusty.Get());
	}
	// The rack's wire, front and back, which the plates lean on.
	for (const float S : { -1.f, 1.f })
	{
		KitchenRod(Build, FVector(BoardU - 26.f, Y + 6.f + S * 9.f, Rim + 12.f), FVector(BoardU + 18.f, Y + 6.f + S * 9.f, Rim + 12.f), 0.6f, MatIron);
	}
}

void AKitchenActor::BuildDresser(FRoomBuilder& Build)
{
	// The dresser, built into the east wall: cupboards and drawers under a scrubbed top, and over it
	// an open rack of shelves with the everyday china on them. Painted the grey of the rest, and
	// worn back to the pine on every edge a hand ever touched.
	const float F = FloorZ();
	const float Y0 = DresserY() - DresserWidth * 0.5f;
	const float Y1 = DresserY() + DresserWidth * 0.5f;
	const float Top = F + CounterHeight;
	const float Depth = DresserDepth;
	const FVector Out(-1.f, 0.f, 0.f);

	// The base: a solid carcass, except the one bay whose door has come open.
	const int32 Bays = 4;
	const float BayW = DresserWidth / Bays;
	for (int32 b = 0; b < Bays; ++b)
	{
		const float U0 = Y0 + b * BayW;
		const float U = U0 + BayW * 0.5f;
		const bool bOpen = b == 1;
		if (!bOpen)
		{
			Build.Box(WallPoint(EWall::East, U, (F + 10.f + Top - 4.f) * 0.5f, Depth * 0.5f), FRotator::ZeroRotator, FVector(Depth, BayW, Top - 14.f - F), MatPaint, false);
			CupboardDoor(Build, WallPoint(EWall::East, U0 + 3.f, F + 42.f, Depth), FVector(0.f, 1.f, 0.f), Out, BayW - 6.f, 58.f, 0.f);
		}
		else
		{
			auto Panel = [&](float PU, float PZ, float PProud, float SU, float SZ, float SDepth)
			{
				Build.Box(WallPoint(EWall::East, PU, PZ, PProud), FRotator::ZeroRotator, FVector(SDepth, SU, SZ), MatPaintDark, false);
			};
			const float Z0 = F + 10.f;
			const float Z1 = Top - 4.f;
			Panel(U, (Z0 + Z1) * 0.5f, 1.f, BayW, Z1 - Z0, 2.f);
			Panel(U, Z0 + 1.f, Depth * 0.5f, BayW, 2.f, Depth);
			Panel(U, Z1 - 1.f, Depth * 0.5f, BayW, 2.f, Depth);
			Panel(U, F + 44.f, Depth * 0.5f - 3.f, BayW - 4.f, 2.f, Depth - 6.f);
			Panel(U0 + 1.f, (Z0 + Z1) * 0.5f, Depth * 0.5f, 2.f, Z1 - Z0, Depth);
			Panel(U0 + BayW - 1.f, (Z0 + Z1) * 0.5f, Depth * 0.5f, 2.f, Z1 - Z0, Depth);
			// Hinged at its south edge and hanging open towards the table.
			CupboardDoor(Build, WallPoint(EWall::East, U0 + BayW - 3.f, F + 42.f, Depth), FVector(0.f, -1.f, 0.f), Out, BayW - 6.f, 58.f, 64.f);
			// Pans nested in the bottom, a tureen on the shelf.
			for (int32 i = 0; i < 3; ++i)
			{
				Build.Cyl(WallPoint(EWall::East, U - 20.f + i * 16.f, Z0 + 2.f + (12.f - i * 2.f) * 0.5f, Depth * 0.5f), FRotator::ZeroRotator,
					FVector(24.f - i * 3.f, 24.f - i * 3.f, 12.f - i * 2.f), i == 1 ? MatCastIron.Get() : MatCopper.Get(), false);
			}
			Build.Add(FRoomShapes::Sphere(), WallPoint(EWall::East, U + 4.f, F + 54.f, Depth * 0.5f), FRotator::ZeroRotator, FVector(30.f, 22.f, 18.f), MatChina, false);
			Build.Add(FRoomShapes::Plane(), WallPoint(EWall::East, U, Z1 - 10.f, Depth * 0.6f), FRotator(0.f, 90.f, 90.f), FVector(40.f, 14.f, 1.f), MatWeb, false);
		}
		// A drawer over each bay, with two brass pulls, the second one from the south pulled out.
		const bool bPulled = b == 2;
		const FVector DrawerAt = WallPoint(EWall::East, U, Top - 10.f, Depth + (bPulled ? 16.f : 0.8f));
		Build.Box(DrawerAt, FRotator::ZeroRotator, FVector(1.6f, BayW - 4.f, 13.f), MatPaint, false);
		for (const float S : { -1.f, 1.f })
		{
			Build.Sph(DrawerAt + Out * 1.8f + FVector(0.f, S * BayW * 0.25f, 0.f), 2.4f, MatBrass);
		}
		if (bPulled)
		{
			// Its sides and what is in it: the cutlery, jumbled, gone black.
			for (const float S : { -1.f, 1.f })
			{
				Build.Box(WallPoint(EWall::East, U + S * (BayW * 0.5f - 3.f), Top - 12.f, Depth + 1.f), FRotator::ZeroRotator, FVector(30.f, 1.4f, 9.f), MatPaintDark, false);
			}
			Build.Box(WallPoint(EWall::East, U, Top - 16.f, Depth + 1.f), FRotator::ZeroRotator, FVector(30.f, BayW - 6.f, 1.f), MatPaintDark, false);
			FRandomStream Spoons(611);
			for (int32 i = 0; i < 9; ++i)
			{
				const FVector At = WallPoint(EWall::East, U + Spoons.FRandRange(-BayW * 0.35f, BayW * 0.35f), Top - 15.f, Depth + Spoons.FRandRange(-10.f, 10.f));
				Build.Box(At, FRotator(0.f, Spoons.FRandRange(-30.f, 30.f), 0.f), FVector(18.f, 1.2f, 0.4f), MatIron, false);
			}
		}
	}
	// The top, with its worn front edge.
	Build.Box(WallPoint(EWall::East, DresserY(), Top - 2.f, Depth * 0.5f + 2.f), FRotator::ZeroRotator, FVector(Depth + 4.f, DresserWidth + 6.f, 4.f), MatScrubbed, false);
	Build.Box(WallPoint(EWall::East, DresserY(), F + 5.f, Depth * 0.5f - 3.f), FRotator::ZeroRotator, FVector(Depth - 6.f, DresserWidth, 10.f), MatShadow, false);
	KitchenPawnOnly(Build.Box(WallPoint(EWall::East, DresserY(), F + CounterHeight * 0.5f, Depth * 0.5f), FRotator::ZeroRotator, FVector(Depth, DresserWidth, CounterHeight), MatVoid));

	// The rack: a boarded back, sides, three shelves with a rail, and a cornice.
	const float RackDepth = 26.f;
	const float Z0 = Top + 12.f;
	const float Z1 = F + 262.f;
	WallFill(Build, EWall::East, Y0 + 4.f, Y1 - 4.f, Top, Z1, MatRackBack, 0.6f);
	for (const float U : { Y0 + 2.f, Y1 - 2.f })
	{
		WallBox(Build, EWall::East, U, (Top + Z1) * 0.5f, 4.f, Z1 - Top, RackDepth, 0.f, MatPaint);
	}
	WallBox(Build, EWall::East, DresserY(), Z1 + 4.f, DresserWidth + 12.f, 8.f, RackDepth + 6.f, 0.f, MatPaint);
	const float ShelfZ[3] = { Z0 + 20.f, Z0 + 66.f, Z0 + 112.f };
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float Z = ShelfZ[Row];
		WallBox(Build, EWall::East, DresserY(), Z - 1.f, DresserWidth - 8.f, 2.4f, RackDepth - 2.f, 0.f, MatPaint);
		WallBox(Build, EWall::East, DresserY(), Z + 6.f, DresserWidth - 8.f, 1.4f, 1.4f, RackDepth - 4.f, MatPaint);
		const int32 Plates = 10;
		for (int32 p = 0; p < Plates; ++p)
		{
			if ((p * 7 + Row * 3) % 11 == 4 || (Row == 1 && p == 6))
			{
				continue;
			}
			const float Dia = Row == 2 ? 20.f : (p % 3 == 0 ? 28.f : 25.f);
			const float U = Y0 + 20.f + p * (DresserWidth - 40.f) / (Plates - 1);
			// The everyday set is banded; the odd plain one is what got broken and replaced.
			StandingPlate(Build, WallPoint(EWall::East, U, Z + 0.2f, 12.f), Out, Dia, 13.f + (p % 3) * 2.5f, (p + Row) % 4 == 0 ? MatChinaDusty.Get() : MatChina.Get(),
				(p * 5 + Row) % 7 == 3 ? nullptr : MatChinaBand.Get());
		}
	}
	// Dust along the front of each shelf, and a web from the cornice down to the top shelf. Only the
	// strip in front of the plates: a decal aimed down projects onto everything in its box, and one
	// the depth of the shelf laid the dust up the lower half of every plate standing on it, which is
	// what sank the plates into the boards behind them. Aimed down on the east wall, roll 0 puts
	// the first size along the wall (world Y).
	for (const float Z : ShelfZ)
	{
		Build.Stain(RoomSurfaces::Damp, WallPoint(EWall::East, DresserY(), Z + 2.f, 20.5f), FRotator(-90.f, 0.f, 0.f), FVector2D(DresserWidth, 7.f), FLinearColor(0.40f, 0.38f, 0.35f), 0.35f, 1.2f);
	}
	Build.Add(FRoomShapes::Plane(), WallPoint(EWall::East, Y1 - 30.f, Z1 - 16.f, 14.f), FRotator(0.f, 90.f, 90.f), FVector(50.f, 30.f, 1.f), MatWeb, false);

	Footprints.Add(FBox2D(FVector2D(EastX() - Depth - 30.f, Y0 - 10.f), FVector2D(EastX(), Y1 + 10.f)));
}

void AKitchenActor::BuildLarder(FRoomBuilder& Build)
{
	// The larder cupboard in the corner by the door: tall, shelved, its door long gone, and on
	// its shelves the store a household keeps — jars of what was bottled from the garden, tins,
	// a crock of flour gone solid, a sack slumped on the floor of it.
	const float F = FloorZ();
	const float Y = LarderY();
	const float W = 110.f;
	const float Depth = 55.f;
	const float H = 232.f;
	const float Y0 = Y - W * 0.5f;

	auto Panel = [&](float PU, float PZ, float PProud, float SU, float SZ, float SDepth, UMaterialInterface* Mat)
	{
		Build.Box(WallPoint(EWall::East, PU, PZ, PProud), FRotator::ZeroRotator, FVector(SDepth, SU, SZ), Mat, false);
	};
	Panel(Y, F + H * 0.5f, 1.f, W, H, 2.f, MatPaintDark);
	Panel(Y0 + 1.5f, F + H * 0.5f, Depth * 0.5f, 3.f, H, Depth, MatPaint);
	Panel(Y0 + W - 1.5f, F + H * 0.5f, Depth * 0.5f, 3.f, H, Depth, MatPaint);
	Panel(Y, F + H - 2.f, Depth * 0.5f, W, 4.f, Depth, MatPaint);
	Panel(Y, F + 5.f, Depth * 0.5f, W, 10.f, Depth, MatPaint);
	Panel(Y, F + H + 4.f, Depth * 0.5f + 2.f, W + 8.f, 8.f, Depth + 4.f, MatPaint);
	const float Shelves[4] = { F + 62.f, F + 108.f, F + 152.f, F + 194.f };
	for (const float Z : Shelves)
	{
		Panel(Y, Z, Depth * 0.5f - 2.f, W - 6.f, 2.4f, Depth - 4.f, MatPaintDark);
	}
	// No door: a two-metre leaf standing open across the corner was a wall of its own, and it hid
	// the one cupboard in the room with anything worth looking at in it. What is left of it is the
	// two hinges, rusted on to the carcass.
	for (const float Z : { F + 30.f, F + H - 30.f })
	{
		Build.Box(WallPoint(EWall::East, Y0 + 1.f, Z, Depth + 0.6f), FRotator::ZeroRotator, FVector(1.2f, 3.2f, 9.f), MatIron, false);
	}
	KitchenPawnOnly(Build.Box(WallPoint(EWall::East, Y, F + H * 0.5f, Depth * 0.5f), FRotator::ZeroRotator, FVector(Depth, W, H), MatVoid));

	// The stores. Bottled fruit gone to black syrup, jars with a crust in the bottom, tins.
	FRandomStream Stores(1987);
	for (int32 s = 0; s < 4; ++s)
	{
		const float Z = Shelves[s] + 1.2f;
		const int32 Count = 5 - (s % 2);
		for (int32 i = 0; i < Count; ++i)
		{
			if (Stores.FRand() < 0.2f)
			{
				continue;
			}
			const float U = Y0 + 14.f + i * (W - 28.f) / FMath::Max(1, Count - 1);
			const float Back = Depth * 0.5f + Stores.FRandRange(-10.f, 8.f);
			if (s == 3 || Stores.FRand() < 0.3f)
			{
				// A tin, its label long gone to rust.
				const float D = Stores.FRandRange(8.f, 11.f);
				const float Hgt = Stores.FRandRange(10.f, 14.f);
				Build.Cyl(WallPoint(EWall::East, U, Z + Hgt * 0.5f, Back), FRotator::ZeroRotator, FVector(D, D, Hgt), MatIron, false);
			}
			else
			{
				Jar(Build, WallPoint(EWall::East, U, Z, Back), Stores.FRandRange(9.f, 12.f), Stores.FRandRange(14.f, 22.f), Stores.FRandRange(0.f, 0.8f),
					Stores.FRand() < 0.5f ? MatPreserve.Get() : MatRot.Get());
			}
		}
	}
	// On the floor of it, a sack of potatoes that rotted where it stood, slumped against the side.
	Build.Add(FRoomShapes::Sphere(), WallPoint(EWall::East, Y0 + 40.f, F + 22.f, Depth * 0.5f), FRotator(0.f, 20.f, 8.f), FVector(40.f, 34.f, 36.f), MatCloth, false);
	Build.Add(FRoomShapes::Sphere(), WallPoint(EWall::East, Y0 + 40.f, F + 42.f, Depth * 0.5f - 2.f), FRotator(0.f, 0.f, 20.f), FVector(22.f, 20.f, 14.f), MatCloth, false);
	for (int32 i = 0; i < 5; ++i)
	{
		Build.Add(FRoomShapes::Sphere(), WallPoint(EWall::East, Y0 + 62.f + i * 6.f, F + 12.f, Depth * 0.5f + Stores.FRandRange(-12.f, 14.f)), FRotator(0.f, Stores.FRandRange(0.f, 180.f), 0.f),
			FVector(5.f, 4.f, 2.5f), MatRotDark, false);
	}
	Build.Add(FRoomShapes::Plane(), WallPoint(EWall::East, Y, F + H - 14.f, Depth * 0.6f), FRotator(0.f, 90.f, 90.f), FVector(60.f, 20.f, 1.f), MatWeb, false);
	Build.Add(FRoomShapes::Plane(), WallPoint(EWall::East, Y + 20.f, Shelves[1] + 16.f, Depth * 0.7f), FRotator(0.f, 90.f, 70.f), FVector(40.f, 18.f, 1.f), MatWeb, false);

	Footprints.Add(FBox2D(FVector2D(EastX() - Depth - 100.f, Y0 - 10.f), FVector2D(EastX(), Y0 + W + 10.f)));
}

void AKitchenActor::BuildTable(FRoomBuilder& Build)
{
	// The long prep table in the middle of the floor: thick scrubbed boards on turned legs, a drawer
	// at the end, a pot-board underneath. Supper was being got ready on it.
	const FVector C = TableCentre();
	const float F = FloorZ();
	const float TopZ = F + TableHeight;
	const float HalfL = TableLength * 0.5f;
	const float HalfW = TableWidth * 0.5f;

	Build.Box(FVector(C.X, C.Y, TopZ - 3.5f), FRotator(0.f, 0.f, 0.f), FVector(TableLength, TableWidth, 7.f), MatScrubbed);
	// The apron under the top, and the legs.
	for (const float S : { -1.f, 1.f })
	{
		Build.Box(FVector(C.X, C.Y + S * (HalfW - 9.f), TopZ - 13.f), FRotator::ZeroRotator, FVector(TableLength - 24.f, 2.5f, 12.f), MatOakDark, false);
	}
	Build.Box(FVector(C.X - HalfL + 12.f, C.Y, TopZ - 13.f), FRotator::ZeroRotator, FVector(2.5f, TableWidth - 18.f, 12.f), MatOakDark, false);
	for (const float SX : { -1.f, 1.f })
	{
		for (const float SY : { -1.f, 1.f })
		{
			const FVector Leg(C.X + SX * (HalfL - 12.f), C.Y + SY * (HalfW - 9.f), F);
			Build.Box(Leg + FVector(0.f, 0.f, TableHeight - 13.f), FRotator::ZeroRotator, FVector(9.f, 9.f, 12.f), MatOakDark, false);
			Build.Cyl(Leg + FVector(0.f, 0.f, (TableHeight - 19.f) * 0.5f), FRotator::ZeroRotator, FVector(7.f, 7.f, TableHeight - 19.f), MatOakDark, false);
			Build.Add(FRoomShapes::Sphere(), Leg + FVector(0.f, 0.f, 48.f), FRotator::ZeroRotator, FVector(10.f, 10.f, 14.f), MatOakDark, false);
			Build.Add(FRoomShapes::Sphere(), Leg + FVector(0.f, 0.f, 22.f), FRotator::ZeroRotator, FVector(9.f, 9.f, 8.f), MatOakDark, false);
			Build.Cyl(Leg + FVector(0.f, 0.f, 2.5f), FRotator::ZeroRotator, FVector(8.f, 8.f, 5.f), MatOakDark, false);
		}
	}
	// The pot-board, and on it an enamel bowl and a basket of onions gone to papery husks.
	Build.Box(FVector(C.X, C.Y, F + 16.f), FRotator::ZeroRotator, FVector(TableLength - 30.f, TableWidth - 24.f, 2.5f), MatOakDark, false);
	Build.Cyl(FVector(C.X - 60.f, C.Y + 10.f, F + 23.f), FRotator::ZeroRotator, FVector(34.f, 34.f, 11.f), MatChinaDusty, false);
	Build.Cyl(FVector(C.X - 60.f, C.Y + 10.f, F + 28.55f), FRotator::ZeroRotator, FVector(30.f, 30.f, 0.1f), MatShadow, false);
	Build.Cyl(FVector(C.X + 40.f, C.Y - 6.f, F + 26.f), FRotator::ZeroRotator, FVector(38.f, 30.f, 17.f), MatOakDark, false);
	for (int32 i = 0; i < 6; ++i)
	{
		Build.Add(FRoomShapes::Sphere(), FVector(C.X + 32.f + (i % 3) * 8.f, C.Y - 12.f + (i / 3) * 10.f, F + 35.f + (i % 2) * 2.f), FRotator::ZeroRotator, FVector(7.f, 7.f, 5.f), MatRot, false);
	}

	// The drawer in the east end, pulled half out, knives and spoons in it.
	{
		const FVector D(C.X + HalfL - 4.f, C.Y, TopZ - 14.f);
		Build.Box(D + FVector(22.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(1.6f, 46.f, 11.f), MatOakDark, false);
		Build.Sph(D + FVector(24.f, 0.f, 0.f), 3.f, MatBrass);
		for (const float S : { -1.f, 1.f })
		{
			Build.Box(D + FVector(4.f, S * 22.f, -1.f), FRotator::ZeroRotator, FVector(36.f, 1.2f, 9.f), MatOakDark, false);
		}
		Build.Box(D + FVector(4.f, 0.f, -5.f), FRotator::ZeroRotator, FVector(36.f, 44.f, 1.f), MatOakDark, false);
		for (int32 i = 0; i < 5; ++i)
		{
			Build.Box(D + FVector(8.f + (i % 2) * 4.f, -16.f + i * 8.f, -4.f), FRotator(0.f, (i % 2 ? 6.f : -4.f), 0.f), FVector(20.f, 1.4f, 0.5f), MatIron, false);
		}
	}

	// On the top. The cutting board, the rusted knife across it, and an onion half-cut, dried hard.
	const FVector Board(C.X - 50.f, C.Y + 14.f, TopZ);
	Build.Box(Board + FVector(0.f, 0.f, 1.2f), FRotator(0.f, 8.f, 0.f), FVector(48.f, 32.f, 2.4f), MatOakDark, false);
	Build.Crack(Board + FVector(0.f, 0.f, 6.f), FRotator(-90.f, 0.f, 8.f), FVector2D(28.f, 42.f), 1.f, 40.f);
	Build.Box(Board + FVector(4.f, -2.f, 2.6f), FRotator(0.f, 32.f, 0.f), FVector(20.f, 2.8f, 0.3f), MatIron, false);
	Build.Box(Board + FVector(-10.f, -10.f, 3.2f), FRotator(0.f, 32.f, 0.f), FVector(11.f, 2.2f, 1.6f), MatOakDark, false);
	for (int32 i = 0; i < 3; ++i)
	{
		Build.Add(FRoomShapes::Sphere(), Board + FVector(-8.f + i * 5.f, 8.f, 3.2f), FRotator(0.f, i * 50.f, 0.f), FVector(5.f, 4.f, 1.6f), MatRot, false);
	}
	// Carrots gone to black twigs, and a cabbage fallen in on itself under a fur of grey.
	for (int32 i = 0; i < 3; ++i)
	{
		Build.Add(FRoomShapes::Cone(), Board + FVector(30.f + i * 3.f, -18.f + i * 4.f, 1.2f), FRotator(90.f, 170.f + i * 12.f, 0.f), FVector(2.6f, 2.6f, 16.f), MatRotDark, false);
	}
	Build.Add(FRoomShapes::Sphere(), FVector(C.X - 96.f, C.Y - 22.f, TopZ + 4.f), FRotator(0.f, 30.f, 0.f), FVector(20.f, 18.f, 9.f), MatRotDark, false);
	Build.Add(FRoomShapes::Sphere(), FVector(C.X - 97.f, C.Y - 21.f, TopZ + 7.f), FRotator(0.f, 30.f, 0.f), FVector(15.f, 13.f, 5.f), MatMould, false);

	// A bowl of fruit, the apples and pears shrunk to brown stones, two furred over.
	const FVector Bowl(C.X + 10.f, C.Y - 26.f, TopZ);
	Build.Cyl(Bowl + FVector(0.f, 0.f, 4.5f), FRotator::ZeroRotator, FVector(30.f, 30.f, 9.f), MatChinaDusty, false);
	Build.Cyl(Bowl + FVector(0.f, 0.f, 9.05f), FRotator::ZeroRotator, FVector(26.f, 26.f, 0.1f), MatShadow, false);
	FRandomStream Fruit(1106);
	for (int32 i = 0; i < 7; ++i)
	{
		const float A = Fruit.FRandRange(0.f, 2.f * PI);
		const float R = i < 4 ? Fruit.FRandRange(2.f, 8.f) : Fruit.FRandRange(0.f, 4.f);
		const float S = Fruit.FRandRange(5.5f, 7.f);
		Build.Add(FRoomShapes::Sphere(), Bowl + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 9.f + (i < 4 ? 2.f : 6.f)), FRotator(Fruit.FRandRange(-20.f, 20.f), Fruit.FRandRange(0.f, 360.f), 0.f),
			FVector(S, S * 0.9f, S * 0.75f), i == 2 || i == 5 ? MatMould.Get() : MatRot.Get(), false);
	}
	// One that rolled out, and the dark ring it left where it lay.
	Build.Add(FRoomShapes::Sphere(), FVector(C.X + 34.f, C.Y - 40.f, TopZ + 2.4f), FRotator::ZeroRotator, FVector(6.f, 5.5f, 4.8f), MatRot, false);
	Build.Stain(RoomSurfaces::Damp, FVector(C.X + 34.f, C.Y - 40.f, TopZ + 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(10.f, 10.f), FLinearColor(0.06f, 0.04f, 0.025f), 0.7f, 1.f);

	// The mixing bowl, with the batter dried to a cracked crust in it, and the spoon still in that.
	// Turned rather than assembled (FRoomBuilder::Lathe): as a cylinder with a disc of batter
	// flush with its top it was a solid drum of stone with a stick in it, and nobody could say
	// what it was. What makes it a bowl is a foot, a belly flaring out to a rolled lip, and an
	// inside — and the batter sunk down in that inside, shrunk off the glaze as it dried.
	const FVector Mix(C.X + 62.f, C.Y + 18.f, TopZ);
	{
		const TArray<FVector2D> Turned = {
			// Under the foot, and the foot ring's hard edge.
			{ 0.f, 0.f }, { 8.2f, 0.f }, { 8.2f, 0.f }, { 8.8f, 1.1f },
			// The belly, out and up to the lip.
			{ 11.8f, 3.4f }, { 14.6f, 6.8f }, { 16.3f, 10.6f }, { 17.0f, 13.2f },
			// The rolled lip.
			{ 17.4f, 14.0f }, { 17.0f, 14.6f }, { 16.2f, 14.1f },
			// Down the inside to the bottom of the bowl.
			{ 15.6f, 11.4f }, { 14.0f, 7.8f }, { 11.2f, 4.6f }, { 7.0f, 2.4f }, { 0.f, 1.8f },
		};
		Build.Lathe(Mix, FRotator(0.f, 0.f, 0.f), Turned, 40, MatBowl, RoomSurfaces::Plaster.TexelSizeCm * 0.25f);
		// The crust: a low dome that has come away from the glaze all round, so a dark ring of the
		// bowl's inside shows between the two, and cracked across as batter cracks.
		UMaterialInterface* Batter = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.20f, 0.155f, 0.10f), 1.3f);
		// Walked inwards, so it faces up (the profile runs out along an underside and in along a top).
		const TArray<FVector2D> Crust = { { 11.4f, 5.0f }, { 11.0f, 5.8f }, { 9.2f, 6.6f }, { 5.f, 7.2f }, { 0.f, 7.4f } };
		Build.Lathe(Mix, FRotator(0.f, 0.f, 0.f), Crust, 32, Batter, 30.f);
		// Kept inside the crust's own radius, so the fissures do not run up the glaze and over the lip.
		Build.Crack(Mix + FVector(0.f, 0.f, 10.f), FRotator(-90.f, 0.f, 0.f), FVector2D(18.f, 18.f), 1.f, 22.f);
		// The wooden spoon, stuck in the crust where it was left and leaning on the lip.
		const FVector Stuck = Mix + FVector(-2.f, -2.f, 7.f);
		const FVector OnLip = Mix + FVector(12.3f, 12.3f, 15.3f);
		const FVector Handle = (OnLip - Stuck).GetSafeNormal();
		KitchenRod(Build, Stuck, OnLip + Handle * 14.f, 1.4f, MatOakDark);
		Build.Add(FRoomShapes::Sphere(), Stuck + Handle * 0.5f, FRotationMatrix::MakeFromX(Handle).Rotator(), FVector(5.f, 4.f, 1.6f), MatOakDark, false);
	}
	// Flour spilled round it, grey now.
	Build.Stain(RoomSurfaces::Damp, Mix + FVector(-8.f, -4.f, 6.f), FRotator(-90.f, 0.f, 30.f), FVector2D(46.f, 60.f), FLinearColor(0.52f, 0.50f, 0.46f), 0.45f, 1.3f);
	// The rolling pin, and two empty jars with their lids off.
	KitchenRod(Build, FVector(C.X + 96.f, C.Y - 20.f, TopZ + 3.f), FVector(C.X + 98.f, C.Y + 22.f, TopZ + 3.f), 6.f, MatScrubbed);
	Jar(Build, FVector(C.X - 16.f, C.Y + 34.f, TopZ), 10.f, 15.f, 0.f, nullptr);
	Jar(Build, FVector(C.X - 4.f, C.Y + 38.f, TopZ), 8.f, 12.f, 0.1f, MatPreserve);

	// The tea towel, thrown down over the corner nearest the sink.
	Build.Cloth(FVector(C.X - HalfL + 16.f, C.Y - HalfW + 14.f, TopZ + 0.4f), FRotator(0.f, 20.f, 0.f), FVector2D(44.f, 40.f), 1.4f, 3.f, 7121, MatCloth, RoomSurfaces::Drapery.TexelSizeCm);

	// Knife marks and old stains in the boards, and the dust over all of it.
	for (int32 i = 0; i < 7; ++i)
	{
		Build.Crack(FVector(C.X + Random.FRandRange(-HalfL + 20.f, HalfL - 20.f), C.Y + Random.FRandRange(-HalfW + 14.f, HalfW - 14.f), TopZ + 6.f), FRotator(-90.f, 0.f, Random.FRandRange(0.f, 180.f)),
			FVector2D(Random.FRandRange(6.f, 12.f), Random.FRandRange(18.f, 40.f)), 0.9f, Random.FRandRange(40.f, 56.f));
	}
	for (int32 i = 0; i < 4; ++i)
	{
		Build.Stain(RoomSurfaces::Damp, FVector(C.X + Random.FRandRange(-HalfL + 30.f, HalfL - 30.f), C.Y + Random.FRandRange(-HalfW + 20.f, HalfW - 20.f), TopZ + 6.f),
			FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)), FVector2D(Random.FRandRange(16.f, 36.f), Random.FRandRange(16.f, 36.f)), FLinearColor(0.07f, 0.045f, 0.03f), 0.55f, 1.1f);
	}
	Build.Stain(RoomSurfaces::Damp, FVector(C.X, C.Y, TopZ + 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(TableWidth, TableLength), FLinearColor(0.40f, 0.38f, 0.35f), 0.32f, 1.35f);

	// The chairs: two pushed in along the north side, one along the south, and the one at the east
	// end pulled well back and turned out, as a chair is left by somebody who got up in a hurry.
	// Painted ladder-backs, at their own life size (RoomProps::KitchenChair): WoodenChair_01 is a
	// Gothic hall chair 2.3 metres tall, and brought down to a kitchen chair's height it had a seat
	// the size of a stool's and sat at the table like doll's furniture. Its footprint is 64 x 66
	// about the middle, so a chair pushed in has the front of its seat just under the table's edge.
	struct FSeat { FVector At; float Yaw; };
	const float PushedIn = HalfW + 22.f;
	const FSeat Seats[] = {
		{ FVector(C.X - 60.f, C.Y - PushedIn, F), 0.f + 3.f },
		{ FVector(C.X + 50.f, C.Y - PushedIn - 3.f, F), 0.f - 4.f },
		{ FVector(C.X - 10.f, C.Y + PushedIn + 2.f, F), 180.f + 5.f },
		{ FVector(C.X + HalfL + 74.f, C.Y + 22.f, F), 90.f + 28.f },
	};
	for (const FSeat& Seat : Seats)
	{
		if (UStaticMeshComponent* Chair = Build.PropSeated(RoomProps::KitchenChair, Seat.At, FRotator(0.f, Seat.Yaw, 0.f)))
		{
			// The photograph is a cool blue-grey (linear 0.149, 0.174, 0.177), and untinted the chairs
			// were the palest things on the floor. Warmed and held to about 0.06, under the table's
			// scrubbed top, so they are the same kitchen and not a white set.
			FRoomShapes::TintSlots(Chair, FLinearColor(0.42f, 0.355f, 0.32f));
		}
		Footprints.Add(FBox2D(FVector2D(Seat.At.X - 36.f, Seat.At.Y - 36.f), FVector2D(Seat.At.X + 36.f, Seat.At.Y + 36.f)));
	}
	// The scrape its legs left in the dust on the tiles.
	for (int32 i = 0; i < 2; ++i)
	{
		Build.Crack(FVector(C.X + HalfL + 44.f, C.Y + 8.f + i * 30.f, F + 6.f), FRotator(-90.f, 0.f, 90.f + 12.f), FVector2D(6.f, 54.f), 0.8f, 50.f);
	}

	KitchenPawnOnly(Build.Box(FVector(C.X, C.Y, F + TableHeight * 0.5f), FRotator::ZeroRotator, FVector(TableLength, TableWidth, TableHeight), MatVoid));
	Footprints.Add(FBox2D(FVector2D(C.X - HalfL - 20.f, C.Y - HalfW - 20.f), FVector2D(C.X + HalfL + 40.f, C.Y + HalfW + 20.f)));
}

void AKitchenActor::BuildPanRack(FRoomBuilder& Build)
{
	// An iron rack on chains from the beams over the table, hung with what would not fit on the
	// range's rail: the big pans, a colander, a fish kettle. The draught through the broken glass
	// sets it swinging, a little, and the pans knock together without a sound.
	const FVector C = TableCentre();
	RackPivot = KitchenPivot(this, RoomRoot, FVector(C.X, C.Y, CeilingZ() - 22.f), FRotator::ZeroRotator, TEXT("PanRack"));
	FRoomBuilder R(this, RackPivot);
	const float Drop = 120.f;
	const float HalfL = 95.f;
	const float HalfW = 24.f;

	for (const float SX : { -1.f, 1.f })
	{
		for (const float SY : { -1.f, 1.f })
		{
			const FVector Top(SX * (HalfL - 10.f), SY * 4.f, 0.f);
			const FVector Bottom(SX * (HalfL - 10.f), SY * HalfW, -Drop);
			const int32 Links = FMath::FloorToInt(Drop / 5.f);
			for (int32 l = 0; l < Links; ++l)
			{
				const FVector At = FMath::Lerp(Top, Bottom, (l + 0.5f) / Links);
				R.Box(At, FRotator(0.f, (l % 2) * 90.f, 0.f), FVector(0.8f, 2.6f, 5.6f), MatIron, false);
			}
		}
		R.Box(FVector(SX * (HalfL - 10.f), 0.f, 1.5f), FRotator::ZeroRotator, FVector(4.f, 12.f, 3.f), MatIron, false);
	}
	for (const float SY : { -1.f, 1.f })
	{
		KitchenRod(R, FVector(-HalfL, SY * HalfW, -Drop), FVector(HalfL, SY * HalfW, -Drop), 2.f, MatIron);
	}
	for (const float SX : { -1.f, 1.f })
	{
		KitchenRod(R, FVector(SX * HalfL, -HalfW, -Drop), FVector(SX * HalfL, HalfW, -Drop), 2.f, MatIron);
	}

	// What hangs from it: pans on S-hooks, their handles straight up, bodies flat along the bars.
	struct FHung { float X; float Side; float Diameter; float Depth; float Handle; int32 Kind; };
	const FHung Hung[] = {
		{ -64.f, -1.f, 32.f, 5.f, 22.f, 0 },   // frying pan
		{ -34.f, -1.f, 24.f, 12.f, 14.f, 1 },  // saucepan
		{ -6.f, -1.f, 28.f, 12.f, 16.f, 2 },   // colander
		{ 30.f, -1.f, 20.f, 10.f, 12.f, 1 },
		{ 60.f, -1.f, 26.f, 4.f, 20.f, 0 },
		{ -50.f, 1.f, 22.f, 11.f, 13.f, 1 },
		{ -12.f, 1.f, 30.f, 13.f, 15.f, 1 },
		{ 44.f, 1.f, 18.f, 9.f, 11.f, 1 },
	};
	for (const FHung& Pan : Hung)
	{
		const float Y = Pan.Side * HalfW;
		R.Box(FVector(Pan.X, Y, -Drop - 3.5f), FRotator::ZeroRotator, FVector(0.8f, 0.8f, 7.f), MatIron, false);
		UMaterialInterface* Metal = Pan.Kind == 0 ? MatCastIron.Get() : MatCopper.Get();
		const float HandleTop = -Drop - 7.f;
		const float HandleBottom = HandleTop - Pan.Handle;
		R.Box(FVector(Pan.X, Y, (HandleTop + HandleBottom) * 0.5f), FRotator::ZeroRotator, FVector(2.6f, 1.2f, Pan.Handle), Metal, false);
		const FVector Body(Pan.X, Y + Pan.Side * (Pan.Depth * 0.5f - 0.6f), HandleBottom - Pan.Diameter * 0.5f);
		R.Cyl(Body, FRotator(0.f, 0.f, 90.f), FVector(Pan.Diameter, Pan.Diameter, Pan.Depth), Metal, false);
		if (Pan.Kind == 2)
		{
			// The colander's holes are a darker disc on its face.
			R.Cyl(Body + FVector(0.f, Pan.Side * (Pan.Depth * 0.5f + 0.1f), 0.f), FRotator(0.f, 0.f, 90.f), FVector(Pan.Diameter * 0.7f, Pan.Diameter * 0.7f, 0.2f), MatShadow, false);
		}
	}
	R.Add(FRoomShapes::Plane(), FVector(20.f, 0.f, -Drop * 0.5f), FRotator(0.f, 0.f, 90.f), FVector(40.f, 50.f, 1.f), MatWeb, false);
}

void AKitchenActor::BuildDamage(FRoomBuilder& Build)
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

		for (int32 i = 0; i < 8; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float Z = Random.FRandRange(F + 40.f, C - 30.f);
			if (!IsOnOpening(Wall, U, Z, 40.f, 40.f))
			{
				Stain(Wall, U, Z, Random.FRandRange(100.f, 220.f), Random.FRandRange(90.f, 200.f), RoomSurfaces::Damp, DampTint * 0.85f, Random.FRandRange(0.14f, 0.28f), Random.FRandRange(0.f, 360.f), 1.15f);
			}
		}
		for (int32 i = 0; i < 3; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float Drop = Random.FRandRange(100.f, 240.f);
			if (!IsOnOpening(Wall, U, C - Drop * 0.4f, 40.f, Drop * 0.4f))
			{
				Stain(Wall, U, C - Drop * 0.4f, Random.FRandRange(70.f, 140.f), Drop, RoomSurfaces::Damp, DampTint * 0.72f, Random.FRandRange(0.4f, 0.6f), 0.f, 1.25f);
			}
		}
		for (int32 i = 0; i < 5; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float Z = Random.FRandRange(F + 130.f, C - 50.f);
			if (!IsOnOpening(Wall, U, Z, 55.f, 55.f))
			{
				FVector Location;
				FRotator Rotation;
				AimAt(Wall, U, Z, 0.f, Location, Rotation);
				Build.Crack(Location, Rotation, FVector2D(Random.FRandRange(90.f, 190.f), Random.FRandRange(110.f, 220.f)), Random.FRandRange(0.65f, 1.f), Random.FRandRange(14.f, 26.f));
			}
		}
		// Plaster off to the brick, high up where nothing stands against the wall.
		for (int32 i = 0; i < 2; ++i)
		{
			const float U = Random.FRandRange(U0 + 60.f, U1 - 60.f);
			const float Z = Random.FRandRange(F + 290.f, C - 50.f);
			if (!IsOnOpening(Wall, U, Z, 60.f, 60.f))
			{
				Stain(Wall, U, Z, Random.FRandRange(60.f, 120.f), Random.FRandRange(50.f, 110.f), RoomSurfaces::Substrate, SubstrateTint, Random.FRandRange(0.85f, 1.f), Random.FRandRange(0.f, 360.f), 0.9f);
			}
		}
		// Mould in the corners, low and high.
		for (const float Corner : { U0, U1 })
		{
			const float U = Corner + (Corner == U0 ? 1.f : -1.f) * Random.FRandRange(10.f, 50.f);
			Stain(Wall, U, F + 40.f, Random.FRandRange(60.f, 120.f), Random.FRandRange(70.f, 140.f), RoomSurfaces::Damp, MouldTint, 0.7f, 0.f, 1.1f);
			Stain(Wall, U, C - 40.f, Random.FRandRange(70.f, 140.f), Random.FRandRange(60.f, 120.f), RoomSurfaces::Damp, MouldTint, 0.6f, 0.f, 1.1f);
		}
	}

	// Mould round both windows, blooming up the reveals and spreading behind the sink, where the
	// wall has been wet longest.
	for (int32 i = 0; i < 2; ++i)
	{
		const float U = WindowX(i);
		for (const float S : { -1.f, 1.f })
		{
			Stain(EWall::North, U + S * (WindowWidth * 0.5f + 22.f), F + Random.FRandRange(150.f, 280.f), Random.FRandRange(34.f, 56.f), Random.FRandRange(110.f, 200.f),
				RoomSurfaces::Damp, MouldTint, Random.FRandRange(0.6f, 0.8f), 0.f, 1.15f);
		}
	}
	Stain(EWall::North, WindowX(0), F + 60.f, 150.f, 110.f, RoomSurfaces::Damp, MouldTint, 0.85f, 0.f, 1.2f);

	// The painted cupboard fronts worn back to the pine: patches of the planks photograph laid on
	// the paint, torn at the edge as flaking paint is, along the north run and on the dresser. Never
	// flat rectangles (the door's 09-20 note).
	const FLinearColor PineTint(0.22f, 0.19f, 0.16f);
	for (int32 i = 0; i < 12; ++i)
	{
		const bool bNorth = i < 8;
		const float U = bNorth ? Random.FRandRange(WestX() + 30.f, EastX() - 30.f) : Random.FRandRange(DresserY() - DresserWidth * 0.5f + 20.f, DresserY() + DresserWidth * 0.5f - 20.f);
		const float Z = F + Random.FRandRange(20.f, 80.f);
		const EWall Wall = bNorth ? EWall::North : EWall::East;
		const float Proud = bNorth ? CounterDepth - 4.f : DresserDepth;
		const FVector At = WallPoint(Wall, U, Z, Proud + 5.f);
		Build.Stain(RoomSurfaces::RoughWood, At, FRotationMatrix::MakeFromX(-WallNormal(Wall)).Rotator() + FRotator(0.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(8.f, 22.f), Random.FRandRange(6.f, 18.f)), PineTint, 0.95f, 0.8f);
	}

	// Dust along the foot of every wall. Aimed down, a decal's first size is along world Y: roll 90
	// along the north and south walls.
	const FLinearColor DustTint(0.42f, 0.40f, 0.36f);
	for (int32 i = 0; i < 18; ++i)
	{
		FVector At;
		FRotator Rot(-90.f, 0.f, 0.f);
		switch (i % 4)
		{
		case 0: At = FVector(Random.FRandRange(WestX() + 30.f, EastX() - 30.f), NorthY() + CounterDepth + Random.FRandRange(4.f, 20.f), F + 4.f); Rot.Roll = 90.f; break;
		case 1: At = FVector(Random.FRandRange(WestX() + 30.f, EastX() - 30.f), SouthY() - Random.FRandRange(12.f, 34.f), F + 4.f); Rot.Roll = 90.f; break;
		case 2: At = FVector(EastX() - DresserDepth - Random.FRandRange(4.f, 24.f), Random.FRandRange(NorthY() + 30.f, SouthY() - 30.f), F + 4.f); break;
		default: At = FVector(WestX() + Random.FRandRange(12.f, 34.f), Random.FRandRange(NorthY() + 30.f, SouthY() - 30.f), F + 4.f); break;
		}
		Build.Stain(RoomSurfaces::Damp, At, Rot, FVector2D(Random.FRandRange(60.f, 150.f), Random.FRandRange(24.f, 44.f)), DustTint, Random.FRandRange(0.22f, 0.36f), 1.35f);
	}
	// Water stains on the tiles along the window wall, where the rain comes in.
	for (int32 i = 0; i < 2; ++i)
	{
		Build.Stain(RoomSurfaces::Damp, FVector(WindowX(i) + 30.f, NorthY() + CounterDepth + 50.f, F + 4.f), FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(120.f, 180.f), Random.FRandRange(80.f, 120.f)), FLinearColor(0.12f, 0.10f, 0.08f), 0.6f, 1.2f, 0.3f);
	}
}

void AKitchenActor::BuildDebris(FRoomBuilder& Build)
{
	const float F = FloorZ();

	// Rubble along the foot of the walls, kept out of the furniture.
	for (int32 i = 0; i < 70; ++i)
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
		const float Size = Random.FRandRange(1.5f, 6.f);
		// Draw the rotation's numbers whether or not the spot is used, so the stream after it holds.
		const FRotator Rot(Random.FRandRange(-30.f, 30.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-30.f, 30.f));
		const FVector Scale(Size * Random.FRandRange(0.8f, 1.8f), Size * Random.FRandRange(0.8f, 1.5f), Size * 0.6f);
		if (!IsFloorSpotClear(Spot.X, Spot.Y, Size))
		{
			continue;
		}
		Build.Box(FVector(Spot.X, Spot.Y, F + Size * 0.35f), Rot, Scale, MatRubble, false);
	}

	// A plate that went off the dresser's rack — the gap it left is on the middle shelf — in pieces
	// on the tiles in front of it.
	const FVector Smash(EastX() - DresserDepth - 44.f, DresserY() - 24.f, F);
	FRandomStream Pieces(4411);
	for (int32 i = 0; i < 9; ++i)
	{
		const float A = Pieces.FRandRange(0.f, 2.f * PI);
		const float R = Pieces.FRandRange(0.f, 36.f);
		Build.Box(Smash + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.6f), FRotator(Pieces.FRandRange(-6.f, 6.f), Pieces.FRandRange(0.f, 360.f), Pieces.FRandRange(-6.f, 6.f)),
			FVector(Pieces.FRandRange(3.f, 9.f), Pieces.FRandRange(2.f, 6.f), 1.1f), MatChina, false);
	}

	// A tin that rolled under the table, and loose pages from the recipe book scattered off the
	// south counter.
	const FVector C = TableCentre();
	Build.Cyl(FVector(C.X + 30.f, C.Y + 30.f, F + 5.f), FRotator(0.f, 30.f, 90.f), FVector(10.f, 10.f, 13.f), MatIron, false);
	for (int32 i = 0; i < 6; ++i)
	{
		const FVector2D Spot(Random.FRandRange(WestX() + 80.f, SouthCounterEndX()), Random.FRandRange(SouthY() - 180.f, SouthY() - CounterDepth - 20.f));
		const FRotator Rot(Random.FRandRange(-2.f, 2.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-2.f, 2.f));
		if (!IsFloorSpotClear(Spot.X, Spot.Y, 16.f))
		{
			continue;
		}
		Build.Mark(FVector(Spot.X, Spot.Y, F + 0.2f), Rot, FVector2D(14.f, 21.f), i % 2 ? MatPaper.Get() : MatPaperDamp.Get());
	}

	// Dead leaves blown in under the windows, through the broken panes.
	UMaterialInterface* Leaf = Build.Flat(FLinearColor(0.035f, 0.020f, 0.009f), 0.9f);
	for (int32 i = 0; i < 22; ++i)
	{
		const float X = WindowX(i % 2) + Random.FRandRange(-100.f, 100.f);
		const float Y = NorthY() + CounterDepth + Random.FRandRange(10.f, 140.f);
		const FRotator Rot(Random.FRandRange(-8.f, 8.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-8.f, 8.f));
		const FVector Scale(Random.FRandRange(4.f, 7.f), Random.FRandRange(2.5f, 4.f), 0.3f);
		if (!IsFloorSpotClear(X, Y, 4.f))
		{
			continue;
		}
		Build.Box(FVector(X, Y, F + 0.5f), Rot, Scale, Leaf, false);
	}
	// And on the worktop under the east window, where the glass came in.
	for (int32 i = 0; i < 8; ++i)
	{
		Build.Box(FVector(WindowX(1) + Random.FRandRange(-60.f, 60.f), NorthY() + Random.FRandRange(10.f, CounterDepth - 6.f), F + CounterHeight + 0.4f),
			FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f), FVector(Random.FRandRange(2.f, 6.f), Random.FRandRange(1.f, 4.f), 0.4f), MatGlass, false);
	}
}

void AKitchenActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ElapsedTime += DeltaTime;

	const float Gust = LeadStorm ? LeadStorm->GetWindGust() : 0.f;

	// The rack over the table: long chains, a slow swing, a fraction of a degree.
	if (RackPivot)
	{
		const float Amp = FMath::Lerp(0.1f, 0.6f, Gust);
		const float Pitch = Amp * FMath::Sin(ElapsedTime * 2.f * PI / 3.4f);
		const float Roll = Amp * 0.7f * FMath::Sin(ElapsedTime * 2.f * PI / 4.3f + 1.1f);
		RackPivot->SetRelativeRotation(FRotator(Pitch, 0.f, Roll));
	}

	if (DustMotes)
	{
		DustMotes->SetWindStrength(Gust);
	}
}

// ---------------------------------------------------------------------------------------------

void AKitchenActor::SpawnWindows()
{
	// Two followers of the bedroom's storm in the north wall. Only the west one — over the sink —
	// brings the world outside and the strike's shadows with it (see FStormWindowSetup::bOwnView);
	// its sky, trees and rain are wide enough to fill the other.
	for (int32 i = 0; i < 2; ++i)
	{
		const FTransform Transform(FRotator(0.f, -90.f, 0.f),
			GetActorTransform().TransformPosition(FVector(WindowX(i), NorthY() - Setup.WallThickness * 0.5f, FloorZ())));
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
		WindowSetup.bOwnView = (i == 0);
		WindowSetup.PortalScale = 0.45f;
		// Every window its own seed, and none shared with another room's.
		WindowSetup.Seed = 19861104 + i * 101;
		Window->Configure(WindowSetup);
		Window->SetLead(LeadStorm);
		Window->FinishSpawning(Transform);
		Windows.Add(Window);
	}
}

AClueActor* AKitchenActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description)
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

void AKitchenActor::BuildClues()
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
	// One word of handwriting: a flat stroke in the plane of the sheet. A plane rather than a Mark,
	// and casting no shadow: a Mark is a box four millimetres thick, and on a page under a lantern a
	// row of them stood up off the paper, each throwing its own shadow, and read as the slats of a
	// ventilation grille.
	auto Word = [&](FRoomBuilder& B, const FTransform& Sheet, float X, float Y, float Length, float Weight, UMaterialInterface* Ink)
	{
		if (UStaticMeshComponent* Stroke = B.Add(FRoomShapes::Plane(), Sheet.TransformPosition(FVector(X, Y, 0.f)), Sheet.Rotator(),
			FVector(Length, Weight, 1.f), Ink, false))
		{
			Stroke->SetCastShadow(false);
		}
	};
	// Lines of handwriting in the XY plane of Sheet, whose origin is the middle of the written area
	// on the paper's surface. Read along local +X, the lines running down the sheet towards +Y,
	// which is how a page reads from above in this frame. Each line is broken into words of
	// uneven length, and the last line of each paragraph stops short: evenly ruled strokes of one
	// length were a grille, not writing.
	auto Writing = [&](FRoomBuilder& B, const FTransform& Sheet, float Width, float Height, int32 Lines, int32 Seed)
	{
		FRandomStream Hand(Seed);
		const float Pitch = Height / FMath::Max(1, Lines);
		for (int32 l = 0; l < Lines; ++l)
		{
			// A paragraph every four or so lines: its last line is short and the next is indented.
			const bool bLast = Hand.FRand() < 0.24f || l == Lines - 1;
			const float Y = -Height * 0.5f + Pitch * (l + 0.5f);
			float Cursor = -Width * 0.5f + (l > 0 && Hand.FRand() < 0.2f ? Width * 0.08f : 0.f);
			const float End = -Width * 0.5f + Width * (bLast ? Hand.FRandRange(0.3f, 0.65f) : Hand.FRandRange(0.9f, 1.f));
			while (Cursor < End - 0.6f)
			{
				const float Length = FMath::Min(Hand.FRandRange(0.7f, 2.6f), End - Cursor);
				Word(B, Sheet, Cursor + Length * 0.5f, Y + Hand.FRandRange(-0.06f, 0.06f), Length, Hand.FRandRange(0.26f, 0.34f), MatInk);
				Cursor += Length + Hand.FRandRange(0.4f, 0.6f);
			}
		}
	};
	const float F = FloorZ();
	const FVector T = TableCentre();
	const float TopZ = F + TableHeight;

	// The recipe book, open on the south worktop.
	{
		const FVector At(WestX() + 250.f, SouthY() - CounterDepth * 0.5f + 4.f, F + CounterHeight);
		if (AClueActor* Book = SpawnClue(At, FRotator::ZeroRotator,
			TEXT("Examine the recipe book"),
			TEXT("The family's recipe book, split at the spine from use, open at a birthday cake. In the margin, in a child's big letters: MORE SPRINKLES. And under it, in her mother's hand: \"We'll see.\"")))
		{
			FRoomBuilder B(Book, Book->GetRootScene());
			UMaterialInterface* Boards = B.Flat(FLinearColor(0.035f, 0.018f, 0.012f), 0.85f);
			const float Yaw = 6.f;
			B.Box(FVector(0.f, 0.f, 0.4f), FRotator(0.f, Yaw, 0.f), FVector(44.f, 30.f, 0.8f), Boards, false);
			for (const float S : { -1.f, 1.f })
			{
				// Each half of the block of pages, falling away into the gutter: an open book lies in
				// a shallow V, pinched down at the spine. Pitch is the tilt about the spine. The first
				// version rolled the pages about the other axis, which tipped each one along its
				// lines, so the writing laid flat over it stood clear of the paper at one end.
				const FTransform Page(FRotator(S * 3.f, Yaw, 0.f), FRotator(0.f, Yaw, 0.f).RotateVector(FVector(S * 10.5f, 0.f, 1.5f)));
				B.Box(Page.GetLocation(), Page.Rotator(), FVector(20.f, 27.f, 2.4f), S < 0.f ? MatPaperDamp.Get() : MatPaper.Get(), false);
				// Written in the page's own frame, turned half round so that it reads from the side
				// of the worktop the detective stands on: along world -X, the lines coming towards him.
				const FTransform Sheet = FTransform(FRotator(0.f, 180.f, 0.f), FVector(0.f, 0.f, 1.25f)) * Page;
				if (S > 0.f)
				{
					// The left-hand page as he reads it: the recipe's name over the method.
					Word(B, Sheet, -1.f, -10.2f, 9.f, 0.45f, MatInk);
					Writing(B, FTransform(FVector(0.f, 1.2f, 0.f)) * Sheet, 15.f, 18.f, 11, 31);
				}
				else
				{
					Writing(B, FTransform(FVector(0.f, -2.f, 0.f)) * Sheet, 15.f, 16.f, 10, 32);
					// And in the margin under it, in big crooked capitals: MORE SPRINKLES.
					const FTransform Margin = FTransform(FRotator(0.f, -7.f, 0.f), FVector(-1.f, 9.6f, 0.01f)) * Sheet;
					Word(B, Margin, -4.2f, 0.f, 4.6f, 0.9f, MatInk);
					Word(B, Margin, 2.4f, 0.3f, 7.2f, 0.9f, MatInk);
				}
			}
			// The crease of the spine, in the bottom of the V, and the ribbon lying in it.
			B.Box(FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.f, 0.f, 1.5f)), FRotator(0.f, Yaw, 0.f), FVector(1.2f, 27.f, 1.3f), MatShadow, false);
			B.Box(FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.2f, -4.f, 2.2f)), FRotator(0.f, Yaw + 1.5f, 0.f), FVector(0.8f, 19.f, 0.1f), MatRedInk, false);
			// A fat thumbprint of butter gone brown on the left-hand page.
			B.Stain(RoomSurfaces::Damp, FVector(13.f, -6.f, 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(6.f, 5.f), FLinearColor(0.08f, 0.05f, 0.02f), 0.7f, 1.f);
			HitVolume(B, FVector(0.f, 0.f, 3.f), FVector(48.f, 34.f, 6.f));
		}
	}

	// The calendar and the grocery list, on the wall over the south worktop.
	const float CalU = WestX() + 150.f;
	const float CalZ = F + 170.f;
	if (AClueActor* Calendar = SpawnClue(WallPoint(EWall::South, CalU, CalZ, 0.4f), FRotator::ZeroRotator,
		TEXT("Examine the calendar"),
		TEXT("A calendar for a year a long way gone, still on the same month. One square is ringed in red, with a cake drawn in it and a 7. Nothing is written on any day after the one before it.")))
	{
		FRoomBuilder B(Calendar, Calendar->GetRootScene());
		// On the south wall a Mark rolled -90 lies flat on it, its first size along world X.
		const FRotator Flat(0.f, 0.f, -90.f);
		B.Mark(FVector(0.f, 0.f, 0.f), Flat, FVector2D(32.f, 46.f), MatPaper);
		// The picture at the top, a faded landscape gone to one brown.
		B.Mark(FVector(0.f, -0.1f, 12.f), Flat, FVector2D(28.f, 18.f), MatPaperDamp);
		// The grid of days: six rules across, six down.
		for (int32 i = 0; i < 6; ++i)
		{
			B.Mark(FVector(0.f, -0.12f, 1.5f - i * 4.f), Flat, FVector2D(28.f, 0.25f), MatInk);
		}
		for (int32 i = 0; i < 8; ++i)
		{
			B.Mark(FVector(-14.f + i * 4.f, -0.12f, -8.5f), Flat, FVector2D(0.25f, 20.f), MatInk);
		}
		// The ringed day: a loop of red around one square, drawn in short strokes.
		const FVector Day(4.f, -0.14f, -4.5f);
		for (int32 i = 0; i < 12; ++i)
		{
			const float A = 2.f * PI * i / 12.f;
			B.Mark(Day + FVector(FMath::Cos(A) * 2.6f, 0.f, FMath::Sin(A) * 2.2f), Flat, FVector2D(1.4f, 0.4f), MatRedInk);
		}
		B.Sph(FVector(0.f, -0.8f, 23.5f), 1.4f, MatIron);
		HitVolume(B, FVector(0.f, -1.f, 0.f), FVector(34.f, 2.f, 48.f));
	}
	if (AClueActor* List = SpawnClue(WallPoint(EWall::South, CalU + 36.f, CalZ + 6.f, 0.4f), FRotator::ZeroRotator,
		TEXT("Examine the list"),
		TEXT("A shopping list on the back of an envelope, pinned under the calendar's corner. Icing sugar. Candles (7). Sprinkles — the coloured ones. And last, underlined twice: coffee for him — the strong one.")))
	{
		FRoomBuilder B(List, List->GetRootScene());
		B.Mark(FVector(0.f, 0.f, 0.f), FRotator(0.f, 4.f, -90.f), FVector2D(11.f, 17.f), MatPaperDamp);
		for (int32 l = 0; l < 6; ++l)
		{
			B.Mark(FVector(-1.f + (l % 2) * 0.6f, -0.12f, 6.f - l * 2.3f), FRotator(0.f, 0.f, -90.f), FVector2D(5.f + (l % 3) * 1.4f, 0.3f), MatInk);
		}
		B.Mark(FVector(0.f, -0.14f, -7.4f), FRotator(0.f, 0.f, -90.f), FVector2D(6.f, 0.25f), MatInk);
		B.Sph(FVector(0.f, -0.8f, 7.5f), 1.2f, MatBrass);
		HitVolume(B, FVector(0.f, -1.f, 0.f), FVector(14.f, 2.f, 20.f));
	}

	// The clock over the worktop, high on the south wall: a plain round kitchen clock, stopped, its
	// hands fallen off inside the glass.
	if (AClueActor* Clock = SpawnClue(WallPoint(EWall::South, WestX() + 240.f, F + 238.f, 0.f), FRotator::ZeroRotator,
		TEXT("Examine the clock"),
		TEXT("The kitchen clock, stopped. Both its hands have come off the spindle and lie in the bottom of the case, behind the glass — so whatever time it stopped at is anybody's guess.")))
	{
		FRoomBuilder B(Clock, Clock->GetRootScene());
		// A disc on the south wall faces -Y: its axis is world Y.
		const FRotator OnWall(0.f, 0.f, 90.f);
		UMaterialInterface* Dial = B.Flat(FLinearColor(0.22f, 0.20f, 0.16f), 0.7f);
		B.Cyl(FVector(0.f, -3.f, 0.f), OnWall, FVector(34.f, 34.f, 6.f), MatOakDark, false);
		B.Cyl(FVector(0.f, -6.1f, 0.f), OnWall, FVector(29.f, 29.f, 0.2f), Dial, false);
		// Twelve hour marks, the three and nine and twelve and six heavier.
		for (int32 h = 0; h < 12; ++h)
		{
			const float A = 2.f * PI * h / 12.f;
			const float Heavy = h % 3 == 0 ? 1.f : 0.f;
			B.Box(FVector(FMath::Sin(A) * 12.f, -6.3f, FMath::Cos(A) * 12.f), FRotator(-FMath::RadiansToDegrees(A), 0.f, 0.f),
				FVector(0.6f + Heavy * 0.5f, 0.2f, 2.4f + Heavy * 1.2f), MatInk, false);
		}
		B.Cyl(FVector(0.f, -6.4f, 0.f), OnWall, FVector(1.4f, 1.4f, 0.6f), MatBrass, false);
		// The hands, lying at the bottom of the dial against the bezel.
		B.Box(FVector(-3.f, -6.5f, -12.2f), FRotator(8.f, 0.f, 0.f), FVector(10.f, 0.3f, 0.8f), MatInk, false);
		B.Box(FVector(2.f, -6.7f, -11.6f), FRotator(0.f, 0.f, 0.f), FVector(7.f, 0.3f, 1.1f), MatInk, false);
		B.Cyl(FVector(0.f, -6.9f, 0.f), OnWall, FVector(29.f, 29.f, 0.4f), B.Glass(RoomPalette::GlassShard, 0.035f, 0.06f), false);
		B.Crack(FVector(4.f, -12.f, 5.f), FRotator(0.f, 90.f, 30.f), FVector2D(18.f, 14.f), 0.9f, 36.f);
		HitVolume(B, FVector(0.f, -4.f, 0.f), FVector(36.f, 8.f, 36.f));
	}

	// The photograph, tucked in a frame on the range's mantel shelf.
	{
		const FVector At(BreastX() + 12.f, HearthY() - 40.f, F + ShelfHeight);
		if (AClueActor* Photo = SpawnClue(At, FRotator::ZeroRotator,
			TEXT("Examine the photograph"),
			TEXT("A snapshot, gone brown: a woman and a little girl at this table, flour to the elbows, both laughing at whoever held the camera. On the back, in pencil: \"Our helper, aged 6.\"")))
		{
			FRoomBuilder B(Photo, Photo->GetRootScene());
			UMaterialInterface* PhotoGlass = B.Glass(RoomPalette::GlassShard, 0.035f, 0.06f);
			// standing_picture_frame_01 is glass, artwork, frame, and faces its local +Y: yaw -90
			// turns that out of the west wall to the room.
			if (UStaticMeshComponent* Frame = B.PropSeated(RoomProps::PhotoFrame, FVector::ZeroVector, FRotator(0.f, -90.f + 12.f, 0.f), 22.f, false))
			{
				Frame->SetMaterial(0, PhotoGlass);
				FRoomShapes::TintSlots(Frame, FLinearColor(0.15f, 0.12f, 0.085f), 1);
				FRoomShapes::TintSlots(Frame, FLinearColor(0.35f, 0.32f, 0.30f), 2);
			}
			HitVolume(B, FVector(0.f, 0.f, 12.f), FVector(20.f, 26.f, 24.f));
		}
	}

	// The coffee cup, at the east end of the table, in front of the chair that was pushed back.
	{
		const FVector At(T.X + TableLength * 0.5f - 22.f, T.Y + 12.f, TopZ);
		if (AClueActor* Cup = SpawnClue(At, FRotator::ZeroRotator,
			TEXT("Examine the cup"),
			TEXT("A cup of coffee, poured and not drunk. What was left in it dried to a black crust years ago, and the dust has laid a grey skin over that. It is a man's cup, a big one, chipped where he always held it.")))
		{
			FRoomBuilder B(Cup, Cup->GetRootScene());
			B.Cyl(FVector(0.f, 0.f, 0.45f), FRotator::ZeroRotator, FVector(15.f, 15.f, 0.9f), MatChinaDusty, false);
			B.Cyl(FVector(1.f, -0.6f, 5.4f), FRotator::ZeroRotator, FVector(9.5f, 9.5f, 9.f), MatChina, false);
			B.Cyl(FVector(1.f, -0.6f, 9.95f), FRotator::ZeroRotator, FVector(8.4f, 8.4f, 0.1f), MatRotDark, false);
			B.Box(FVector(1.f - 5.4f, -0.6f + 1.6f, 5.6f), FRotator(0.f, 160.f, 0.f), FVector(2.2f, 0.9f, 5.f), MatChina, false);
			B.Stain(RoomSurfaces::Damp, FVector(0.f, 0.f, 16.f), FRotator(-90.f, 0.f, 0.f), FVector2D(18.f, 18.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.55f, 1.3f);
			HitVolume(B, FVector(0.f, 0.f, 5.f), FVector(18.f, 18.f, 11.f));
		}
	}

	// The note under the salt cellar, at the middle of the table.
	{
		const FVector At(T.X + 30.f, T.Y + 30.f, TopZ);
		if (AClueActor* Note = SpawnClue(At, FRotator::ZeroRotator,
			TEXT("Read the note"),
			TEXT("A note in a woman's hand, weighted under the salt: \"Gone to fetch her from dance — the rain's too heavy for her to walk. Back by eight. Keep an eye on the oven. x\"")))
		{
			FRoomBuilder B(Note, Note->GetRootScene());
			B.Mark(FVector(0.f, 0.f, 0.1f), FRotator(0.f, 14.f, 0.f), FVector2D(15.f, 20.f), MatPaper);
			// A Mark's face is 0.8 above where it is put (FRoomBuilder::Mark). Written across the
			// short side of the sheet, which is how a note is written.
			Writing(B, FTransform(FRotator(0.f, 14.f, 0.f), FVector(0.f, 0.f, 0.94f)), 11.f, 14.f, 6, 71);
			// The salt cellar holding it down: a glass pot with a pewter top, the salt gone to a lump.
			B.Cyl(FVector(4.f, -4.f, 3.6f), FRotator::ZeroRotator, FVector(5.f, 5.f, 3.2f), MatChina, false);
			B.Cyl(FVector(4.f, -4.f, 3.5f), FRotator::ZeroRotator, FVector(6.2f, 6.2f, 7.f), MatGlass, false);
			B.Cyl(FVector(4.f, -4.f, 7.6f), FRotator::ZeroRotator, FVector(5.6f, 5.6f, 1.6f), MatIron, false);
			HitVolume(B, FVector(0.f, 0.f, 3.f), FVector(22.f, 24.f, 7.f));
		}
	}
}
