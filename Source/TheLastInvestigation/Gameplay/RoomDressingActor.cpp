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
	// The plaster set is three times brighter than the one it replaced — it is a photograph of a
	// pale flaked wall rather than of brown clay — so the tints that hold it at the room's albedo
	// are correspondingly lower. The wall ends up at roughly a tenth reflectance, which is what
	// lets a lantern flame be the brightest thing in the frame.
	MatPlaster = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.158f, 0.150f, 0.138f));
	// Every tint in this room runs warm — red above green above blue — and that is deliberate
	// rather than decorative. The room's fill light is a cold rectangle of storm sky, so anything
	// neutral in the texture comes back off the wall blue-green, and next to the lantern's flame
	// it reads as a green stain rather than as dirt. Warm tints cancel it.
	// Brick and coarse render: what the wall is actually made of, wherever the plaster has gone.
	MatSubstrate = Build.Surface(RoomSurfaces::Substrate, FLinearColor(0.115f, 0.098f, 0.084f));
	// The same plaster once it is on the floor. It needs its own tint rather than the wall's: the
	// wall tint is set to sit the wall at the room's reflectance under a cold rectangle of sky,
	// and a chunk of it lying on brown boards under a lantern is being asked a different question.
	// Beside the floorboards the wall value read as a grey lump, because concrete photographed
	// clean *is* grey and the wall only escapes it by having decades of damp projected over it.
	// Rubble that has been down there as long has the same dirt in it, so it goes warmer and
	// darker — nothing on this floor is newer than the floor.
	MatRubble = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.145f, 0.127f, 0.110f));
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.38f, 0.37f, 0.34f));
	MatFloorboards = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.70f, 0.67f, 0.62f));
	MatFloorboardsWorn = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.44f, 0.40f, 0.36f));
	// The ceiling beams, the broken joists under the collapse and the splintered wood on the floor.
	// At the old tint these came out brighter than the plaster — a run of fresh blonde pine across
	// the ceiling of a room whose whole point is that nothing in it is newer than the house. Half
	// that puts them just under the wall, which is where old dry timber belongs.
	MatRoughWood = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.402f, 0.409f, 0.391f));
	// The skirting, and it was the brightest thing in the room by a distance.
	//
	// raw_plank_wall is much the palest photograph in the set — sRGB 165/129/86 flat, which is
	// nearly four times the plaster in linear red — and it was being tinted as though it were a
	// dark one. What that produced is a continuous band of glowing pale timber running round the
	// bottom of every wall at 0.27 albedo, against a wall at 0.08 and a floor at 0.06: a bright
	// line where the room should be darkest, which is exactly where the eye reads a seam.
	MatPlankWood = Build.Surface(RoomSurfaces::PlankWall, FLinearColor(0.150f, 0.175f, 0.225f));
	// Linen and iron are the two photographs in the set that are not the colour they look like on
	// a library thumbnail, and both had been tinted as though they were. A near-neutral tint does
	// not neutralise anything — it multiplies — so whatever the photograph already leans towards,
	// it keeps leaning towards, only darker. Both tints below are the correction, and both were
	// worked out by measuring the photograph rather than by eye.
	//
	// rough_linen is a *blue* linen: sRGB 145/171/205 flat, which is to say its blue channel is
	// more than twice its red in linear. Everything cut from it came out cold.
	MatCloth = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.183f, 0.101f, 0.047f));
	// green_metal_rust is, flat, a sheet of green paint with four rust pinholes in it — the rust
	// the name promises is a handful of pixels. Tinted neutral it stayed green, which is how the
	// lamp cord and the hook came to be green things hanging from the ceiling.
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatRust = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.988f, 0.407f, 0.499f));

	// Paper is the one flat tint that never held up. A sheet of it is a hand's width across, which
	// is close enough for the lantern to show the fibre — and a flat tint has no fibre to show, so
	// every sheet in the room came out as a rectangle of one colour lying on the boards. Linen
	// photographs at about the right coarseness for paper that has been damp for fifty years; the
	// tint doubles because the tint multiplies the photograph rather than replacing it, and lands
	// the sheets back at the reflectance the flat colour had.
	// Paper, and the reason the floor was covered in grey cards: the sheets are cut from the linen
	// photograph, and the tint they were given was a darkening rather than a correction, so every
	// sheet in the room came out at roughly (0.10, 0.14, 0.17) — blue, and brighter than the
	// plaster wall behind it. Paper that has been damp for fifty years is the opposite of that in
	// both respects. Two variants, because eleven identical sheets read as eleven of one prop:
	// the drier one still has its colour, and the other has been bleached and gone dark.
	MatPaper = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.464f, 0.245f, 0.108f));
	MatPaperDamp = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.260f, 0.142f, 0.065f));
	MatPhoto = Build.Flat(RoomPalette::Photo, 0.6f);
	// Glass, translucent — the same material the window panes are made of. As a flat tint every
	// shard on the floor and every bottle under the shelf was an opaque pale chip, which is the
	// one thing glass never is: what makes it read as glass is that the boards show through it.
	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.28f, 0.06f);
	MatBlood = Build.Flat(RoomPalette::DriedBlood, 0.95f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
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

	// Damp and exposed brick, held to one tint each so every patch in the room is the same dirt.
	//
	// Both run warm and both are dark. Damp photographed in daylight is a neutral grey-green, and
	// neutral is the one thing this room cannot afford: the only fill light in it is a cold
	// rectangle of storm sky, so a neutral stain comes back off the wall as a green one.
	const FLinearColor DampTint(0.46f, 0.38f, 0.30f);
	const FLinearColor SubstrateTint(0.30f, 0.26f, 0.22f);

	// Where a projected patch of damage goes, and which way it is thrown.
	//
	// A decal is aimed along its own +X, so the rotation here is the direction the damage is
	// projected *in* — into the wall — rather than the direction it faces. Its Y axis then lands
	// along the same U the panels above use, so a stain can be positioned in exactly the wall-local
	// coordinates everything else on the wall is positioned in.
	auto AimAt = [&](EWallSide Side, float U, float V, float Roll, FVector& OutLocation, FRotator& OutRotation)
	{
		switch (Side)
		{
		case EWallSide::North: OutLocation = FVector(U, -DepthHalf + FaceInset + 2.f, V); OutRotation = FRotator(0.f, -90.f, Roll); break;
		case EWallSide::South: OutLocation = FVector(U, DepthHalf - FaceInset - 2.f, V); OutRotation = FRotator(0.f, 90.f, Roll); break;
		case EWallSide::East:  OutLocation = FVector(WidthHalf - FaceInset - 2.f, U, V); OutRotation = FRotator(0.f, 0.f, Roll); break;
		default:               OutLocation = FVector(-WidthHalf + FaceInset + 2.f, U, V); OutRotation = FRotator(0.f, 180.f, Roll); break;
		}
	};

	auto WallStain = [&](EWallSide Side, float U, float V, float SizeU, float SizeV,
		const FRoomSurface& Set, const FLinearColor& Tint, float Opacity, float Roll = 0.f, float EdgeNoise = 0.9f)
	{
		FVector Location;
		FRotator Rotation;
		AimAt(Side, U, V, Roll, Location, Rotation);
		Build.Stain(Set, Location, Rotation, FVector2D(SizeU, SizeV), Tint, Opacity, EdgeNoise);
	};

	auto WallCrack = [&](EWallSide Side, float U, float V, float SizeU, float SizeV, float Opacity, float Sharpness)
	{
		FVector Location;
		FRotator Rotation;
		AimAt(Side, U, V, 0.f, Location, Rotation);
		Build.Crack(Location, Rotation, FVector2D(SizeU, SizeV), Opacity, Sharpness);
	};

	const EWallSide Sides[4] = { EWallSide::North, EWallSide::South, EWallSide::East, EWallSide::West };

	for (const EWallSide Side : Sides)
	{
		const float WallLength = (Side == EWallSide::North || Side == EWallSide::South) ? Setup.Width : Setup.Depth;
		const float HalfLength = WallLength * 0.5f - Setup.WallThickness;

		// The plaster, over the whole face of the wall — and the only thing built on this wall at
		// all. Everything that used to sit on top of it (paper strips, hanging peels, lifted
		// flakes) stood a few centimetres proud, and standing proud is what put it beyond the
		// reach of the damage below: a decal projects about nine units past the surface it is
		// aimed at, so the wall rotted and the pieces in front of it stayed clean. Pale, hard-
		// edged, undamaged panels on a wall that is coming apart everywhere behind them. All the
		// damage is projected now, so there is nothing left in here for it to miss.
		WallFill(Side, 0.f, Setup.Height * 0.5f, WallLength - Setup.WallThickness * 2.f, Setup.Height, MatPlaster);

		// Grime: broad, faint, overlapping washes of damp across the whole wall.
		//
		// Nothing here is meant to be looked at. Their job is to stop the wall being one even
		// tone, because an even tone is what makes a surface read as a flat panel however good
		// the texture on it is. Low opacity and large: dirt that accumulated, not dirt that was
		// applied.
		for (int32 Wash = 0; Wash < 12; ++Wash)
		{
			const float U = Random.FRandRange(-HalfLength, HalfLength);
			const float V = Random.FRandRange(30.f, Setup.Height - 20.f);
			WallStain(Side, U, V, Random.FRandRange(110.f, 230.f), Random.FRandRange(90.f, 200.f),
				RoomSurfaces::Damp, DampTint * 0.85f, Random.FRandRange(0.14f, 0.26f),
				Random.FRandRange(0.f, 360.f), 1.15f);
		}

		// Cracks that have opened. The plaster's own photograph carries the fine crazing; these
		// are the ones with a wall's worth of subsidence behind them, and they are drawn rather
		// than photographed — see M_RoomCrack.
		// Five, not ten. Ten of them at this size overlapped into an even mesh over the whole
		// wall, and a surface cracked evenly everywhere reads as crazed pottery — the eye takes it
		// for the material's own pattern rather than for damage. Cracks have to have somewhere
		// they are not, or there is nowhere they are.
		for (int32 i = 0; i < 5; ++i)
		{
			const float U = Random.FRandRange(-HalfLength, HalfLength);
			const float V = Random.FRandRange(55.f, Setup.Height - 45.f);
			if (SpotBlocked(Side, U, V, 55.f, 55.f))
			{
				continue;
			}
			WallCrack(Side, U, V, Random.FRandRange(95.f, 190.f), Random.FRandRange(110.f, 230.f),
				Random.FRandRange(0.65f, 1.f), Random.FRandRange(15.f, 28.f));
		}

		// What is behind the plaster, showing wherever a patch of it has come away: brick and
		// coarse render the damp has blackened. Without this the wall is one even colour and
		// nothing on it has anything to have exposed.
		for (int32 Patch = 0; Patch < 9; ++Patch)
		{
			const float PatchU = Random.FRandRange(-HalfLength, HalfLength);
			const float PatchV = Random.FRandRange(30.f, Setup.Height - 30.f);
			if (SpotBlocked(Side, PatchU, PatchV, 50.f, 50.f))
			{
				continue;
			}
			// Projected, not built: this is a hole in the plaster, and a hole cannot have four
			// square corners. The brick showing through is the same photographed brick the fallen
			// pieces on the floor are made of.
			//
			// Darker than the wall around it, and by a long way. The first version of this had the
			// brick brighter than the plaster and the wall came out looking flicked with paint —
			// which is what exposed brick would have to be to read that way. A place where the
			// surface is missing is a place light does not get out of again.
			WallStain(Side, PatchU, PatchV, Random.FRandRange(34.f, 96.f), Random.FRandRange(38.f, 115.f),
				RoomSurfaces::Substrate, SubstrateTint, Random.FRandRange(0.8f, 1.f),
				Random.FRandRange(0.f, 360.f));
		}

		// Blotches: the irregular patches where a whole sheet of the surface came away in one piece
		// and took the top of the plaster with it. The washes above are even and the cracks are
		// linear, so between the two there is a scale of damage the wall has none of without
		// these.
		for (int32 Blotch = 0; Blotch < 7; ++Blotch)
		{
			const float BlotchU = Random.FRandRange(-HalfLength, HalfLength);
			const float BlotchV = Random.FRandRange(50.f, Setup.Height - 30.f);
			if (SpotBlocked(Side, BlotchU, BlotchV, 70.f, 70.f))
			{
				continue;
			}

			// Two or three projections thrown across each other, so the outline of the whole is
			// something no single patch could be: an edge that wanders, doubles back and thins
			// out. Sheets of paper come off a wall in shapes like this.
			const int32 Pieces = Random.RandRange(2, 4);
			for (int32 Piece = 0; Piece < Pieces; ++Piece)
			{
				const bool bDamp = Random.FRand() < 0.5f;
				WallStain(Side,
					BlotchU + Random.FRandRange(-34.f, 34.f),
					BlotchV + Random.FRandRange(-30.f, 30.f),
					Random.FRandRange(50.f, 120.f),
					Random.FRandRange(46.f, 108.f),
					bDamp ? RoomSurfaces::Damp : RoomSurfaces::Substrate,
					bDamp ? DampTint : SubstrateTint * 1.25f,
					bDamp ? Random.FRandRange(0.35f, 0.6f) : Random.FRandRange(0.65f, 0.9f),
					Random.FRandRange(0.f, 360.f));
			}
		}

		// The ceiling line, where the damp has come down the wall from above and blackened it.
		// Wide and shallow, and heavily bitten into along its lower edge — water does not stop at
		// a level line, it runs down until it dries out.
		for (int32 i = 0; i < 4; ++i)
		{
			const float StainU = Random.FRandRange(-HalfLength, HalfLength);
			const float StainHeight = Random.FRandRange(45.f, 105.f);
			WallStain(Side, StainU, Setup.Height - StainHeight * 0.38f,
				Random.FRandRange(90.f, 200.f), StainHeight,
				RoomSurfaces::Damp, DampTint * 0.75f, Random.FRandRange(0.4f, 0.6f),
				0.f, 1.25f);
		}

		// Damp corners: the bloom that creeps up out of the skirting, darkest at the floor.
		for (int32 i = 0; i < 2; ++i)
		{
			const float U = (i == 0 ? -1.f : 1.f) * Random.FRandRange(HalfLength * 0.72f, HalfLength * 0.95f);
			const float V = Random.FRandRange(35.f, 85.f);
			if (SpotBlocked(Side, U, V, 45.f, 45.f))
			{
				continue;
			}
			WallStain(Side, U, V * 0.75f, Random.FRandRange(60.f, 130.f), V * 2.f,
				RoomSurfaces::Damp, DampTint * 0.7f, Random.FRandRange(0.45f, 0.7f),
				0.f, 1.1f);
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
			// A sheet with no thickness, not a slab with four edges.
			//
			// The web material cuts the *face* of whatever it is on into filaments, and a cube has
			// five other faces it does not cut: the four rims of a Mark are solid strips a couple
			// of millimetres wide and up to a metre long, and once the face went transparent those
			// rims were the only part left. What was hanging in the ceiling corners was a rectangle
			// drawn in pale sticks. A plane has no rims to leave behind.
			//
			// A web spans the corner diagonally, so it is turned 45 degrees in plan, and rolled to
			// face down into the room.
			Build.Add(FRoomShapes::Plane(), Location,
				FRotator(0.f, SignX * SignY > 0.f ? 45.f : -45.f, 180.f),
				FVector(Inset * 1.6f, Inset * 1.6f, 1.f), MatWeb, /*bBlockingCollision*/ false);
		}

		// One hanging strand per corner, long enough to catch the lantern and move in the draught.
		USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("WebPivot")));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(DressingRoot, FAttachmentTransformRules::KeepRelativeTransform);
		Pivot->SetRelativeLocation(FVector(CornerX[Corner] - SignX * 55.f, CornerY[Corner] - SignY * 55.f, Setup.Height - 12.f));
		Pivot->RegisterComponent();
		AddInstanceComponent(Pivot);

		FRoomBuilder WebBuild(this, Pivot);
		// A ribbon, not a rod. At one centimetre square this was a stick: a strand of web is
		// thinner than the noise that is supposed to be cutting it into strands, so nothing got
		// cut and what hung from the ceiling was a pale dowel a metre long. Flat and a few
		// centimetres across, the same noise breaks it into threads.
		const float StrandLength = Random.FRandRange(50.f, 130.f);
		WebBuild.Box(FVector(0.f, 0.f, -StrandLength * 0.5f), FRotator(0.f, Random.FRandRange(0.f, 180.f), 0.f),
			FVector(0.4f, 4.f, StrandLength), MatWeb, /*bBlockingCollision*/ false);

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

	// The subfloor: a second course of boards under the first.
	//
	// The boards above are laid with a centimetre and a half between each one and one in twenty
	// missing outright, so a good deal of what the player is looking at when they look at the
	// floor is not floor — it is whatever is underneath. That used to be the shell slab, a flat
	// pale untextured grey, and what it produced was the thing that reads as grey boards lying
	// among the brown ones.
	//
	// Laid the same way as the boards above, not across them. A real subfloor runs crosswise, and
	// that is exactly what was wrong with it: where a board was missing, the crosswise course
	// showed through as one long plank lying at right angles to the floor, which is a thing the
	// eye cannot read as anything but a plank somebody dropped. What a gap in a floor should show
	// is more of the same floor. Offset half a board so the joints never line up with the course
	// above, and in the worn variant, because nothing down there has seen light or a broom since
	// the house was shut.
	//
	// Cut around the collapse — that hole has to stay a hole.
	const float SubBoardWidth = 21.f;
	const int32 SubRowCount = FMath::CeilToInt((DepthHalf * 2.f) / SubBoardWidth) + 1;
	for (int32 Row = 0; Row < SubRowCount; ++Row)
	{
		const float Y = -DepthHalf + SubBoardWidth * Row; // half a board off the course above
		const float DY = FMath::Abs(Y - CollapseCenter.Y);

		float GapMin = 0.f;
		float GapMax = 0.f;
		if (DY < CollapseRadius + SubBoardWidth * 0.5f)
		{
			const float HalfChord = FMath::Sqrt(FMath::Max(FMath::Square(CollapseRadius + SubBoardWidth * 0.5f) - FMath::Square(DY), 0.f));
			GapMin = CollapseCenter.X - HalfChord;
			GapMax = CollapseCenter.X + HalfChord;
		}

		const float Segments[2][2] = {
			{ -WidthHalf, GapMax > GapMin ? GapMin : WidthHalf },
			{ GapMax > GapMin ? GapMax : WidthHalf, WidthHalf }
		};

		for (int32 Segment = 0; Segment < 2; ++Segment)
		{
			const float Start = Segments[Segment][0];
			const float End = Segments[Segment][1];
			if (End - Start < 12.f)
			{
				continue;
			}
			Build.Box(
				FVector((Start + End) * 0.5f, Y, -4.f),
				FRotator::ZeroRotator,
				FVector(End - Start, SubBoardWidth - 1.f, 6.f),
				MatFloorboardsWorn,
				/*bBlockingCollision*/ false);
		}
	}

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
	//
	// Projected rather than laid down. Even at this size a slab is a rectangle with four straight
	// edges standing half a centimetre off the floor, and no tint makes a rectangle read as
	// something that blew into a corner; a stain takes the boards' grain through it and tears its
	// own outline. Aimed at the floor: a decal projects along its own +X, so a pitch of minus
	// ninety points it down at the boards it is lying on.
	const FLinearColor DustTint(0.42f, 0.40f, 0.36f);
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

		Build.Stain(
			RoomSurfaces::Damp,
			FVector(Spot.X, Spot.Y, 10.f),
			FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(40.f, 90.f), Random.FRandRange(26.f, 54.f)),
			DustTint,
			Random.FRandRange(0.22f, 0.38f),
			1.35f);
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

	// Projected onto the boards rather than laid on top of them. As slabs these were four or five
	// overlapping rectangles per pool, and overlapping rectangles do not make a puddle — they make
	// a pale angular shape with corners in the middle of it, which is what the floor was covered
	// in. The water takes the boards' own photograph, darkened, and the roughness is where the
	// reflection comes from: the window, the lightning and the lantern all still come back off it.
	const FLinearColor WaterTint(0.20f, 0.23f, 0.25f);
	for (const FVector2D& Pool : Pools)
	{
		const int32 Lobes = Random.RandRange(3, 6);
		for (int32 i = 0; i < Lobes; ++i)
		{
			// A pool is not an ellipse; it is a handful of overlapping lobes that found the low
			// spots between warped boards.
			Build.Stain(
				RoomSurfaces::Floorboards,
				FVector(Pool.X + Random.FRandRange(-34.f, 34.f), Pool.Y + Random.FRandRange(-30.f, 30.f), 11.f),
				FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)),
				FVector2D(Random.FRandRange(26.f, 62.f), Random.FRandRange(20.f, 48.f)),
				WaterTint,
				Random.FRandRange(0.75f, 0.95f),
				1.2f,
				/*RoughnessScale*/ 0.05f);
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

	// Water damage: rings of stain thrown up at the ceiling, darkest and smallest at the centre
	// where it is still getting wet. Decals, like the damage on the walls — a rectangle of flat
	// colour overhead reads as a hatch, not as a leak. The rotation aims each one straight up:
	// a decal projects along its own +X, and a pitch of ninety degrees is what points that at the
	// ceiling it is lying against.
	const FVector2D StainCenters[3] = { FVector2D(120.f, -80.f), FVector2D(-180.f, 130.f), FVector2D(250.f, 170.f) };
	for (const FVector2D& StainCenter : StainCenters)
	{
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const float Size = Random.FRandRange(70.f, 150.f) * (1.f + Ring * 0.55f);
			Build.Stain(
				RoomSurfaces::Damp,
				FVector(StainCenter.X + Random.FRandRange(-20.f, 20.f), StainCenter.Y + Random.FRandRange(-20.f, 20.f), CeilingZ - 3.f),
				FRotator(90.f, 0.f, Random.FRandRange(0.f, 360.f)),
				FVector2D(Size, Size * Random.FRandRange(0.7f, 1.3f)),
				// Each ring out is fainter and browner: the centre is wet, the edge is where the
				// water stopped and dried years ago.
				Ring == 0 ? FLinearColor(0.26f, 0.26f, 0.23f) : FLinearColor(0.44f, 0.40f, 0.34f),
				Ring == 0 ? 0.9f : 0.34f - Ring * 0.06f,
				1.0f + Ring * 0.15f);
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
	Build.Stain(
		RoomSurfaces::Floorboards,
		FVector(DropOrigin.X, DropOrigin.Y, 11.f),
		FRotator(-90.f, 0.f, 0.f),
		FVector2D(46.f, 38.f),
		FLinearColor(0.20f, 0.23f, 0.25f),
		0.95f,
		1.2f,
		/*RoughnessScale*/ 0.05f);
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
	const FVector BookcaseSpot(WidthHalf - 24.f, -232.f, 0.f);
	const FRotator BookcaseFacing(0.f, 92.f, -2.f);
	const float BookcaseHeight = 192.f;
	Build.Prop(RoomProps::Bookshelf, BookcaseSpot, BookcaseFacing, BookcaseHeight);
	BuildBookcaseContents(BookcaseSpot, BookcaseFacing, BookcaseHeight);

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
			// Under the broken panes, so the rain has actually reached these.
			Random.FRand() < 0.7f ? Cast<UMaterialInterface>(MatPaperDamp) : Cast<UMaterialInterface>(MatPaper));
	}

	// A crate shoved into the far corner behind him, out of the opening shot: it is there for the
	// walk back, not for the wake-up.
	Build.Prop(RoomProps::Crate, FVector(-WidthHalf + 62.f, DepthHalf - 74.f, 0.f), FRotator(0.f, 34.f, 0.f), 46.f);
}

void ARoomDressingActor::BuildBookcaseContents(const FVector& Spot, const FRotator& Facing, float HeightCm)
{
	// Everything in the bookcase is placed in the bookcase's own space rather than in the room's.
	//
	// It stands at ninety-two degrees and leans two more, and while it did, every book in it was
	// being positioned with world-space numbers guessed against that — which is why one row hung
	// out through the side of the carcass, one floated above its shelf and one went through the
	// floor. In the carcass's own frame a shelf is a height and a book is a place along it, and
	// the lean comes for free.
	//
	// The shelf heights below are measured off wooden_bookshelf_worn itself — the upward-facing
	// surfaces in the mesh, reported by Tools/inspect_props.py — and scaled by whatever the
	// carcass was scaled to. The asset is 206.34 tall as authored; the boards are at 11, 38, 65,
	// 95, 125.5 and 165, and the inside of it is 121 wide.
	USceneComponent* Carcass = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("Bookcase")));
	Carcass->SetMobility(EComponentMobility::Movable);
	Carcass->AttachToComponent(DressingRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Carcass->SetRelativeLocationAndRotation(Spot, Facing);
	Carcass->RegisterComponent();
	AddInstanceComponent(Carcass);

	FRoomBuilder Shelf(this, Carcass);

	const float S = HeightCm / 206.34f;
	// Local +X runs along the shelf, local +Y is the way it faces — the open side.
	const float ShelfZ[6] = { 11.f * S, 38.f * S, 65.f * S, 95.f * S, 125.5f * S, 165.f * S };
	// Books stand near the front edge, not pushed back against the panel, the way books do.
	const float Front = 9.f * S;

	// Nothing on a shelf is square to it.
	//
	// book_encyclopedia_set_01 is one rigid row of twenty matched volumes, so anything built out
	// of it starts out looking machine-set — and four of them placed at the same angle at the same
	// depth on four shelves looks like a shop display, which is the one thing a room nobody has
	// been into for fifty years is not. The row cannot be bent, but every other freedom it has is
	// worth spending: each one gets its own height, its own angle across the shelf, its own lean,
	// and its own distance back from the edge. The lean is what does most of the work — a row with
	// nothing holding up one end goes over, and then stays gone over.
	// Top shelf, shoved to one end and leaning hard into the space the missing volumes left.
	Shelf.PropSeated(RoomProps::ShelfBooks, FVector(-24.f * S, Front + 3.f * S, ShelfZ[5]),
		FRotator(0.f, 5.f, 13.f), 26.f, /*bBlockingCollision*/ false);

	// The shelf below it: two short stands with a gap between them, at different angles, because
	// what is left of a row that has been raided is groups, not a row.
	Shelf.PropSeated(RoomProps::ShelfBooks, FVector(14.f * S, Front - 4.f * S, ShelfZ[4]),
		FRotator(0.f, -6.f, -8.f), 24.f, /*bBlockingCollision*/ false);

	// The one thing in this room somebody chose to keep where they would see it: a small frame,
	// stood on the shelf rather than hung, and knocked off square.
	if (UStaticMeshComponent* Frame = Shelf.PropSeated(RoomProps::PictureFrame, FVector(-14.f * S, Front, ShelfZ[3]), FRotator(0.f, -168.f, 5.f), 26.f, /*bBlockingCollision*/ false))
	{
		// hanging_picture_frame_01 ships with its frame and its glass still on WorldGridMaterial —
		// the engine checkerboard — and only its artwork slot textured. On a shelf at eye height
		// that is a bright grey grid panel sitting where the photograph is supposed to be, which
		// is the most conspicuous thing in the bookcase. Slots 0 and 2 are the glass and the frame.
		Frame->SetMaterial(0, MatGlass);
		Frame->SetMaterial(2, MatRoughWood);
	}

	// Two shelves down, pushed well back and almost straight — this is the one nobody touched.
	Shelf.PropSeated(RoomProps::ShelfBooks, FVector(22.f * S, Front - 9.f * S, ShelfZ[2]),
		FRotator(0.f, 3.f, -2.f), 23.f, /*bBlockingCollision*/ false);

	// The shelf under that is empty. It is the one the carcass lost, and an empty shelf is what
	// makes the full ones read as having been emptied rather than as decoration.

	// The bottom shelf: a row that has come off its feet and is lying on its side, pushed back
	// far enough that it does not hang out over the front edge.
	Shelf.PropSeated(RoomProps::ShelfBooks, FVector(-16.f * S, -4.f * S, ShelfZ[0]),
		FRotator(0.f, -9.f, 90.f), 21.f, /*bBlockingCollision*/ false);

	// And one row on the floor, out in front of the carcass and clear of it.
	//
	// Clear of it is the point: at forty-six centimetres this was half inside the side panel, and
	// a row of books growing out of the side of a bookcase is worse than no books at all. The
	// carcass is fifty-eight deep, so its front face is twenty-nine out, and this sits beyond that
	// with its own length turned across the room rather than driven back into the shelf.
	//
	// Not the decorative set: that asset is a *catalogue* — ninety different books laid out side by
	// side in a single mesh, the way a library ships them for picking — so placing it as one prop
	// emptied a bookshop onto the floor beside the shelf. The encyclopedia set is one row of books,
	// which is the thing that actually falls off a shelf.
	Shelf.PropSeated(RoomProps::ShelfBooks, FVector(-40.f * S, 62.f * S, 6.f),
		FRotator(0.f, 62.f, 90.f), 20.f, /*bBlockingCollision*/ false);
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
			bPlaster ? Cast<UMaterialInterface>(MatRubble) : Cast<UMaterialInterface>(MatRoughWood),
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
		// The ones that came down where the roof lets water in have gone dark; the rest have not.
		Build.Mark(FVector(Spot.X, Spot.Y, 5.f), FRotator(Random.FRandRange(-6.f, 6.f), Random.FRandRange(0.f, 360.f), 0.f), FVector2D(Random.FRandRange(16.f, 26.f), Random.FRandRange(20.f, 32.f)),
			Random.FRand() < 0.45f ? Cast<UMaterialInterface>(MatPaperDamp) : Cast<UMaterialInterface>(MatPaper));
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
	//
	// Gouges, not stripes. These were slabs of flat near-black laid on the wall — a centimetre and
	// a half wide, up to forty long, standing proud of the plaster — and at that size a slab is a
	// painted line: they read as somebody having drawn on the wall with a ruler, and because flat
	// black returns whatever is bouncing around the room, they read as a blue-grey painted line at
	// that. A scratch is a place where the surface has been taken off, which is the same thing a
	// crack is, so it is drawn the same way: a noise contour, high sharpness for a fine split,
	// projected onto the wall so it takes the plaster's own grain and has no straight edge.
	for (int32 i = 0; i < 9; ++i)
	{
		const float X = Setup.DoorOpeningCenterX - Setup.DoorOpeningWidth * 0.5f - 25.f - Random.FRandRange(0.f, 40.f);
		const float Z = 108.f + Random.FRandRange(-16.f, 16.f);
		const float Length = Random.FRandRange(18.f, 44.f);
		Build.Crack(
			// On the wall's inner face. The slabs these replace were placed ten centimetres short
			// of it, floating in the room — part of why they read as drawn rather than cut.
			FVector(X, DepthHalf - 2.f, Z),
			// Aimed at the wall (+Y face looks back down the room), rolled off horizontal so the
			// marks rake the way a hand dragging down a wall rakes.
			FRotator(0.f, 90.f, Random.FRandRange(-26.f, 26.f)),
			FVector2D(Length, Length * Random.FRandRange(0.3f, 0.5f)),
			Random.FRandRange(0.55f, 0.85f),
			Random.FRandRange(42.f, 64.f));
	}

	// What is left of a bloodstain, soaked into the boards near the west wall. Decades of dust
	// have taken almost all of it; only the deepest part of the soak is still readable.
	const FVector2D BloodCenter(-WidthHalf + 150.f, 55.f);
	for (int32 i = 0; i < 5; ++i)
	{
		// A soak has no edge — that is most of what makes it a soak. Projected onto the boards it
		// takes their grain and runs along it; laid on as a slab it was a dark rectangle, and a
		// dark rectangle on a floor reads as a missing board.
		Build.Stain(
			RoomSurfaces::Floorboards,
			FVector(BloodCenter.X + Random.FRandRange(-26.f, 26.f), BloodCenter.Y + Random.FRandRange(-26.f, 26.f), 11.f),
			FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(18.f, 54.f), Random.FRandRange(14.f, 44.f)),
			FLinearColor(0.22f, 0.10f, 0.08f),
			Random.FRandRange(0.5f, 0.75f),
			1.1f);
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

		// Dust displaced, not ink spilled: a print is the board showing through where a boot took
		// the dust off it, so it is the floor's own photograph darkened and torn to shape.
		Build.Stain(
			RoomSurfaces::Floorboards,
			FVector(Spot.X, Spot.Y, 11.f),
			FRotator(-90.f, 0.f, FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X))),
			FVector2D(27.f, 11.f),
			FLinearColor(0.30f, 0.28f, 0.25f),
			0.65f,
			0.8f);
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
		LetterBuild.Box(FVector(6.f, 8.f, 1.6f), FRotator(0.f, 24.f, 0.f), FVector(20.f, 26.f, 1.f), MatPaperDamp, /*bBlockingCollision*/ false);
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
	//
	// It was hung four centimetres *inside* the wall and turned to face the other way. The mesh is
	// a disc in its own XZ plane whose dial sits at local Y = 0 with the case behind it at +Y, so
	// the dial looks along -Y — and the clue was yawed 180, which pointed that at the brickwork.
	// What the room got was the back of a clock, half sunk into the plaster, with the rim of the
	// dial showing past the wall as a pale crescent.
	//
	// At DepthHalf - 5 the dial stands five centimetres proud of the inner face and the case sits
	// just clear of it, which is how a clock hangs.
	if (AClueActor* Clock = SpawnClue(FVector(-40.f, DepthHalf - 5.f, 215.f), FRotator::ZeroRotator, TEXT("Examine the clock"),
		TEXT("A wall clock, stopped. The glass is starred where something struck it. Not a power cut, then — somebody stopped it, and there is no knowing what hour that was.")))
	{
		FRoomBuilder ClockBuild(Clock, Clock->GetRootScene());

		// Hands and marks, drawn rather than taken from the asset.
		//
		// wall_clock's atlas has a proper dial on it — numerals, minute ticks, a maker's name — and
		// none of it survives the trip onto the mesh: what the face actually samples is the middle
		// of the dial, magnified, so the clock arrives as a blank white plate with the maker's logo
		// blown up across it. Chasing that through the model's UVs is not worth it for one prop,
		// and a clock that has stopped is a thing the room needs to be able to *say*: the hands are
		// the whole point of it, and the hands are two boxes.
		//
		// The face looks along -Y with the case behind it, and +X is the viewer's right, so an
		// angle measured clockwise from twelve points along (sin, 0, cos) and the bar that draws it
		// is pitched by the negative of that angle.
		UMaterialInstanceDynamic* HandMat = ClockBuild.Flat(FLinearColor(0.012f, 0.011f, 0.010f), 0.55f);
		auto Hand = [&](float Degrees, float Length, float Width, float Stand)
		{
			const float Radians = FMath::DegreesToRadians(Degrees);
			const FVector Direction(FMath::Sin(Radians), 0.f, FMath::Cos(Radians));
			ClockBuild.Box(Direction * (Length * 0.5f) - FVector(0.f, Stand, 0.f),
				FRotator(-Degrees, 0.f, 0.f), FVector(Width, 0.5f, Length), HandMat,
				/*bBlockingCollision*/ false);
		};

		// Stopped at four minutes past eleven. The hour is not a clue and is not meant to be
		// solved — it is the date the house keeps coming back to, and the detective has no way
		// yet of knowing why that should mean anything.
		Hand(332.f, 11.f, 1.6f, 0.7f);  // hour
		Hand(24.f, 16.f, 1.1f, 0.9f);   // minute
		ClockBuild.Box(FVector(0.f, -1.1f, 0.f), FRotator::ZeroRotator, FVector(2.4f, 0.8f, 2.4f), HandMat, /*bBlockingCollision*/ false);

		// The hour marks, so the hands have something to have stopped against.
		for (int32 Mark = 0; Mark < 12; ++Mark)
		{
			const float Radians = FMath::DegreesToRadians(Mark * 30.f);
			const FVector Direction(FMath::Sin(Radians), 0.f, FMath::Cos(Radians));
			const bool bQuarter = (Mark % 3) == 0;
			ClockBuild.Box(Direction * 16.f - FVector(0.f, 0.4f, 0.f),
				FRotator(-Mark * 30.f, 0.f, 0.f),
				FVector(bQuarter ? 1.4f : 0.7f, 0.4f, bQuarter ? 4.2f : 2.6f), HandMat,
				/*bBlockingCollision*/ false);
		}

		if (UStaticMeshComponent* Face = ClockBuild.Prop(RoomProps::WallClock, FVector::ZeroVector, FRotator::ZeroRotator, 44.f))
		{
			// Slot 1 is the crystal, and the imported instance is opaque — a solid white disc laid
			// over the dial, which is why the clock read as a blank plate with a wedge out of it.
			//
			// Clearer than the window glass, which is the room default: the window is meant to be
			// dirty and the crystal is meant to be looked through. At the window value the dial
			// went behind a grey veil and the numerals, which are hairlines, disappeared into it.
			Face->SetMaterial(1, ClockBuild.Glass(FLinearColor(0.16f, 0.17f, 0.17f), 0.08f, 0.03f));
		}
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
