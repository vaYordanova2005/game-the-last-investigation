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
	// Bedding: the same grey the curtains are, and on the same coarse-tiled linen, because a
	// sheet is a two-metre piece of cloth and the weave is the thing being looked at. Warm-leaning
	// rather than neutral for the same reason as the drapes — the only fill in that corner is the
	// cold rectangle of sky at the far end of the room.
	MatBedding = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.214f, 0.150f, 0.099f));
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

	// And nothing scattered through the bed. The debris, the papers and the dust are all thrown
	// at random floor positions, and until the corner had a bed in it that was harmless.
	{
		FVector2D BedCentre;
		FVector2D BedHalf;
		BedFootprint(BedCentre, BedHalf);
		if (FMath::Abs(Point.X - BedCentre.X) < BedHalf.X + Radius
			&& FMath::Abs(Point.Y - BedCentre.Y) < BedHalf.Y + Radius)
		{
			return false;
		}
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
	BuildBedroom(Build);
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

		// No skirting board, and that is the second time this one has been the brightest thing in
		// the room. Darkening it was not enough and could not have been.
		//
		// raw_plank_wall is much the palest photograph in the set — sRGB 165/129/86 flat, nearly
		// four times the plaster in linear red — and it is a photograph of *vertical planks*. A
		// board eighteen centimetres tall and six metres long asks the tiler for four repeats
		// along its length and a twentieth of one across its height, so the photograph arrives
		// stretched twenty times vertically: the seams between the planks become full-height black
		// lines and the faces between them become bright slats. What ran round the foot of every
		// wall was a white picket fence, and no tint fixes that, because the shape is coming out
		// of the normal map, not the colour.
		//
		// A house this far gone has no skirting anyway — it is the first timber to go, because it
		// is the piece that sits in the water. The plaster now runs down to the boards, which is
		// what is behind a skirting once the skirting has rotted off.

		// A mouse hole at the base, chewed through where the plaster has gone soft.
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
	//
	// Into a beam, which it was not. The beams run along Y at four fixed X positions and this sat
	// between two of them, so what was actually on the ceiling was a bent piece of iron stuck to
	// bare plaster with nothing holding it and nothing above it — and lit from below it threw a
	// bent shadow beside itself, so it read as two pieces of rubbish up there rather than one
	// fitting. On the underside of the timber it reads as a fitting.
	// And it was two straight cylinders — a stub down and a second stub leaning off it at
	// seventy-five degrees. That is not a hook, it is two sticks meeting at a corner. What makes a
	// hook a hook is the *curve*: one rod, bent through most of a circle and drawn to a point, and
	// a curve has to be built as a curve. Fifteen short segments round an arc with a ball at every
	// joint, so the rod reads as continuous rather than as a chain of pipes, tapering from the
	// shank to the tip because a smith draws the end out thin before bending it.
	//
	// The arc is swept in the room's YZ plane, which is the plane the detective is looking across
	// as he wakes: a hook whose opening faces the eye is a hook, and the same hook turned ninety
	// degrees is a vertical line.
	const float HookBeamX = -WidthHalf + (WidthHalf * 2.f) * 2.5f / 4.f;
	const FVector HookAnchor(HookBeamX, 60.f, CeilingZ - 18.f);

	// The screw end, up into the timber. Four thin collars are the thread: it is the detail that
	// says the hook was turned into the beam rather than glued onto it, and the only part of the
	// fitting that is lit from directly underneath.
	const float ShankLength = 8.5f;
	Build.Cyl(HookAnchor + FVector(0.f, 0.f, -ShankLength * 0.5f), FRotator::ZeroRotator, FVector(2.2f, 2.2f, ShankLength), MatIron, /*bBlockingCollision*/ false);
	for (int32 Thread = 0; Thread < 4; ++Thread)
	{
		Build.Cyl(HookAnchor + FVector(0.f, 0.f, -1.6f - Thread * 1.5f), FRotator(0.f, 0.f, 4.f),
			FVector(3.1f, 3.1f, 0.55f), MatIron, /*bBlockingCollision*/ false);
	}

	// The bend. The centre sits one radius to the side of the shank, so the rod leaves the shank
	// pointing straight down and turns from there — put the centre directly below it instead and
	// the hook starts horizontal, which is a corner, which is what was wrong with the old one.
	const float HookRadius = 5.4f;
	const FVector BendCentre = HookAnchor + FVector(0.f, HookRadius, -ShankLength);
	const int32 BendSegments = 15;
	const float BendSweep = 214.f;

	FVector Previous = HookAnchor + FVector(0.f, 0.f, -ShankLength);
	FVector Heading = FVector(0.f, 0.f, -1.f);
	float Thickness = 2.2f;
	for (int32 Step = 1; Step <= BendSegments; ++Step)
	{
		const float Along = Step / static_cast<float>(BendSegments);
		const float Sweep = FMath::DegreesToRadians(BendSweep * Along);
		const FVector Point = BendCentre + FVector(0.f, -FMath::Cos(Sweep), -FMath::Sin(Sweep)) * HookRadius;

		Heading = Point - Previous;
		Thickness = FMath::Lerp(2.2f, 1.f, Along);

		Build.Cyl(Previous + Heading * 0.5f, FRotationMatrix::MakeFromZ(Heading).Rotator(),
			FVector(Thickness, Thickness, Heading.Size() + 0.5f), MatIron, /*bBlockingCollision*/ false);
		Build.Sph(Point, Thickness, MatIron);

		Previous = Point;
	}

	// Drawn to a point, which is the last thing that separates a hook from a bent bar.
	Build.Add(FRoomShapes::Cone(), Previous + Heading.GetSafeNormal() * 1.6f,
		FRotationMatrix::MakeFromZ(Heading).Rotator(), FVector(Thickness, Thickness, 3.4f), MatIron, /*bBlockingCollision*/ false);

	// Fifty years of iron in wet timber leaves a mark on the timber. Aimed straight up at the
	// beam's underside, the same way the leak stains are aimed at the ceiling.
	Build.Stain(RoomSurfaces::Damp, HookAnchor + FVector(0.f, 0.f, -1.f), FRotator(90.f, 0.f, 24.f),
		FVector2D(15.f, 13.f), FLinearColor(0.150f, 0.064f, 0.030f), 0.7f, 1.f);

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

	// A crate in the corner past the table, where the window wall meets the door wall. It was in
	// the far corner on the other side of the room, which is where the armchair stands now — and
	// this corner is the one the player walks into on the way from the window to the door, so it
	// is a thing to step round rather than a thing seen across the floor.
	//
	// East of the hinge, which is the half of that corner the door leaf never sweeps through.
	//
	// Square to the wall and touching it, and the touching is measured rather than guessed:
	// PropSeated puts the middle of the footprint where it is asked for, so how far the crate then
	// has to move to reach the plaster is half of its own box *after* the turn — a different
	// number for every yaw, and the reason a hand-tuned offset stops being right the moment
	// anything is rotated.
	const FRotator CrateFacing(0.f, 112.f, 0.f);
	if (UStaticMeshComponent* Crate = Build.PropSeated(RoomProps::Crate, FVector(WidthHalf, DepthHalf - 52.f, 0.f), CrateFacing, 46.f))
	{
		if (UStaticMesh* Mesh = Crate->GetStaticMesh())
		{
			const FBox Placed = Mesh->GetBoundingBox().TransformBy(FTransform(CrateFacing, FVector::ZeroVector, Crate->GetRelativeScale3D()));
			const FVector Spot = Crate->GetRelativeLocation();
			const float EastFace = Setup.Width * 0.5f - Setup.WallThickness * 0.5f;
			Crate->SetRelativeLocation(FVector(EastFace - Placed.Max.X - 1.f, Spot.Y, Spot.Z));
		}
	}
}

void ARoomDressingActor::BedFootprint(FVector2D& OutCentre, FVector2D& OutHalfExtent) const
{
	// GothicBed_01 is authored at 149 by 204 by 153 and is placed at 186 tall, which scales its
	// footprint to 181 by 247: a grand four-poster rather than the half-tester it ships as. A bed
	// is one of the few things in a room whose real size the player already knows, so the way to
	// make a room read as somebody's bedroom is to let the bed be the largest object in it.
	//
	// The head goes against the west wall, so the bed faces the window straight down the length
	// of the room. That is the only wall in here it can face from: the window is the room's one
	// view and its second light source, and a bed is a thing you lie in looking at something.
	// Against the north wall it faces the wardrobe.
	//
	// Turned a quarter from the mesh's own axes, so the long side runs east-west.
	OutHalfExtent = FVector2D(123.7f, 90.6f);

	const float WestFace = -Setup.Width * 0.5f + Setup.WallThickness * 0.5f;

	// Centred on its wall, and standing clear on three sides rather than pushed up a corner. That
	// is how the reference is composed and how a four-poster is actually arranged: it is a piece
	// of furniture you walk round, with the wall only behind the head of it. Centred also means
	// the block of frames over the headboard is centred, and a wall of pictures hung off to one
	// side reads as a mistake rather than as an arrangement.
	OutCentre = FVector2D(WestFace + 11.f + OutHalfExtent.X, 0.f);
}

void ARoomDressingActor::BuildBedroom(FRoomBuilder& Build)
{
	// Until now there was no reason for the detective to have woken up in this room at all: bare
	// boards, a wardrobe against one wall, and nothing in it that says anybody ever lived here. A
	// bed is what makes a house a house. It also does something for the story the rest of the
	// dressing cannot — the room is where he opens his eyes at the start and where the whole thing
	// closes at the end, so what is in here has to be worth coming back to.
	//
	// Composed from a reference frame: the four-poster centred against the wall with a block of
	// hung frames over the headboard, an armchair to the left of it, a nightstand at the head, all
	// of it grey and half rotted. The three pieces are CC0 Poly Haven meshes; everything on and
	// around them is built, the same as the rest of the room.
	//
	// Its own stream: everything after this in the build order — the debris, the traces, every
	// clue — draws from the room's, and one more random number in here would relay all of it.
	FRandomStream BedRandom(19550416);

	FVector2D BedCentre;
	FVector2D BedHalf;
	BedFootprint(BedCentre, BedHalf);

	const float WestFace = -Setup.Width * 0.5f + Setup.WallThickness * 0.5f;
	const float BedHeight = 186.f;

	// A degree and a half off square. Nobody moved this bed in fifty years, but nobody lined it up
	// with the wall in the first place either.
	const FRotator BedFacing(0.f, -88.4f, 0.f);
	if (UStaticMeshComponent* Frame = Build.PropSeated(RoomProps::Bed, FVector(BedCentre.X, BedCentre.Y, 0.f), BedFacing, BedHeight))
	{
		// The headboard was going into the plaster behind it. Both were mid-brown at roughly the
		// same value, and the carving that is the whole reason for using this mesh — the crest,
		// the turned posts, the tracery — is a silhouette before it is anything else, so it needs
		// something behind it that it is not the same colour as.
		//
		// Darker, not lighter: the wall is the pale thing in that corner and the bed is furniture
		// in a house where nothing has been oiled since the war. Against pale plaster a darker bed
		// separates; a lighter one competes with it. It is also stood a little further off the
		// wall now, so there is a line of its own shadow behind the headboard.
		for (int32 Slot = 0; Slot < Frame->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInterface* Source = Frame->GetMaterial(Slot))
			{
				if (UMaterialInstanceDynamic* Aged = UMaterialInstanceDynamic::Create(Source, this))
				{
					Aged->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.52f, 0.39f, 0.30f));
					Frame->SetMaterial(Slot, Aged);
				}
			}
		}
	}

	// Everything that lies on the bed is placed in the bed's own frame: Across runs from one side
	// to the other and Along runs from the head to the foot. Numbers guessed in room coordinates
	// against a bed that is not square to the room is how the bookcase ended up with a row of
	// books growing out of the side of it.
	const FVector BedOrigin(BedCentre.X, BedCentre.Y, 0.f);
	auto OnBed = [&](float Across, float Along, float Up)
	{
		return BedOrigin + BedFacing.RotateVector(FVector(Across, Along, Up));
	};
	const float HalfLength = 123.7f;   // head to foot, after scaling
	const float MattressZ = 54.f;      // the mattress top at 44 authored units, scaled

	// The armchair, in the corner off the foot of the bed on its left, turned back across the
	// room. It stood out in the open floor beside the bed before, which is where a chair ends up
	// when somebody is moving it, not where one lives: a chair in a bedroom is in the corner, and
	// this one is angled so that whoever sat in it was looking down the room at the window.
	const float SouthFace = Setup.Depth * 0.5f - Setup.WallThickness * 0.5f;
	// Forty-five degrees across the corner with its back into it, which is the angle a chair in a
	// corner is always at: square to the room it has a wall behind one shoulder, and across the
	// corner it has the whole room in front of it.
	//
	// ArmChair_01 measures 85 by 77, and the 77 is not symmetric — its box runs from -41.7 to
	// +34.8 in Y, and the long end is the backrest and the curve behind it. So the chair faces its
	// local **+Y**, not its local +X, and the yaw that points that out of the south-west corner is
	// minus a hundred and thirty-five. At minus forty-five it sat with its back to the open room.
	Build.PropSeated(RoomProps::Armchair, FVector(WestFace + 68.f, SouthFace - 76.f, 0.f), FRotator(0.f, -135.f, 0.f), 0.f);

	// The press, filling the corner past the head of the bed. That corner was the one piece of
	// this room with nothing in it and nothing to say, and an empty corner in a room that is
	// otherwise furnished does not read as space, it reads as a room that has not been finished.
	//
	// GothicCabinet_01 is 172 by 112 by 236 as authored and goes in at 208 tall, which is nearly
	// the ceiling: the bed is the biggest thing in the room and this is the second, and between
	// them they close that end of it off.
	//
	// Back to the west wall and facing the window, alongside the head of the bed. Which axis that
	// is comes off the box rather than off a guess: 172 by 112 means the 172 is the width and the
	// 112 is the depth, so the doors are on its local **Y**, and a yaw of minus ninety turns them
	// east. The bed faces the window for the same reason — the window is the only thing in this
	// room worth facing.
	Build.PropSeated(RoomProps::Press, FVector(WestFace + 52.f, -225.f, 0.f), FRotator(0.f, -88.f, 0.f), 208.f);

	// The nightstand at the head of the bed, on the other side of it.
	const FVector NightstandSeat(WestFace + 46.f, BedCentre.Y - BedHalf.Y - 32.f, 0.f);
	Build.PropSeated(RoomProps::Nightstand, NightstandSeat, FRotator(0.f, -88.f, 0.f), 0.f);

	// And what is on it: a candle burnt down to nothing in its own wax, on a saucer. The one thing
	// in the corner that was last touched by somebody rather than left by somebody.
	const FVector CandleBase = NightstandSeat + FVector(-4.f, 3.f, 70.f);
	Build.Cyl(CandleBase + FVector(0.f, 0.f, 0.6f), FRotator::ZeroRotator, FVector(11.f, 11.f, 1.2f), MatIron, /*bBlockingCollision*/ false);
	Build.Cyl(CandleBase + FVector(0.f, 0.f, 1.8f), FRotator::ZeroRotator, FVector(7.6f, 7.6f, 1.6f), MatPaper, /*bBlockingCollision*/ false);
	Build.Cyl(CandleBase + FVector(1.f, -0.5f, 4.4f), FRotator(0.f, 0.f, 3.f), FVector(3.4f, 3.4f, 4.4f), MatPaper, /*bBlockingCollision*/ false);
	Build.Cyl(CandleBase + FVector(1.f, -0.5f, 6.8f), FRotator(0.f, 0.f, 3.f), FVector(1.1f, 1.1f, 1.6f), MatVoid, /*bBlockingCollision*/ false);

	// The bedding, and it is generated cloth rather than anything assembled.
	//
	// It was three slabs first, with a fourth standing over the foot rail like a crate; then a
	// heap of squashed spheres, which came out as a clutch of eggs on the mattress, because each
	// sphere closes its own outline and a quilt has exactly one. Cloth laid over something is a
	// continuous surface that sags where nothing holds it up and falls away over the edges, which
	// is what FRoomBuilder::Cloth generates — and what neither a box nor a ball can be.
	Build.Cloth(OnBed(0.f, 2.f, MattressZ + 1.5f), BedFacing, FVector2D(150.f, 208.f),
		/*Rumple*/ 4.f, /*EdgeFall*/ 12.f, 4416, MatBedding);
	// The quilt: thrown back off the pillows towards the foot, and over the near side of the frame.
	// Kept up the bed rather than over the foot rail: at forty-four centimetres of fall it hung
	// off the footboard as a pale sheet with a bite out of it, which is a flag, not a quilt.
	Build.Cloth(OnBed(-14.f, 30.f, MattressZ + 9.f), BedFacing + FRotator(0.f, -7.f, 0.f), FVector2D(148.f, 124.f),
		/*Rumple*/ 11.f, /*EdgeFall*/ 26.f, 1955, MatBedding);

	// Two pillows, both flattened and one shoved sideways. Ellipsoids are right here and wrong for
	// the quilt, for the same reason either way: a pillow is a closed sack and a quilt is a sheet.
	Build.Add(FRoomShapes::Sphere(), OnBed(-32.f, -HalfLength + 40.f, MattressZ + 8.f),
		BedFacing + FRotator(0.f, 8.f, 0.f), FVector(62.f, 40.f, 18.f), MatBedding, /*bBlockingCollision*/ false);
	Build.Add(FRoomShapes::Sphere(), OnBed(34.f, -HalfLength + 33.f, MattressZ + 7.f),
		BedFacing + FRotator(0.f, -21.f, 0.f), FVector(57.f, 37.f, 16.f), MatBedding, /*bBlockingCollision*/ false);

	// The wall above the headboard. Frames, most of them empty — the reference has a block of five
	// hung over the bed, and it is the detail that makes the corner somebody's rather than a set
	// of furniture standing in one.
	//
	// hanging_picture_frame_01 measures 59 by 1.6 by 84: a portrait frame lying in its own XZ
	// plane, so its face looks along local **Y**. On the west wall it has to look east, which is
	// a yaw of minus ninety — at yaw zero it is edge-on, which is what hung over the bed the first
	// time round: five slivers a centimetre and a half wide. The crooked hang is *pitch*, which
	// turns the frame about its own face normal, the way a picture on one nail does. Roll would
	// tip it off the wall.
	struct FHungFrame { float Y; float Z; float Size; float Tilt; bool bGlazed; };
	const FHungFrame Hung[5] = {
		{ -68.f, 228.f, 48.f,  -4.f, true  },
		{ -14.f, 252.f, 36.f,   6.f, false },
		{  32.f, 222.f, 42.f, -11.f, true  },
		{ -10.f, 198.f, 30.f,   3.f, false },
		{  56.f, 258.f, 32.f,   8.f, false },
	};
	for (const FHungFrame& Frame : Hung)
	{
		UStaticMeshComponent* Hanging = Build.Prop(RoomProps::PictureFrame,
			FVector(WestFace + 5.f, BedCentre.Y + Frame.Y, Frame.Z),
			FRotator(Frame.Tilt, -90.f, 0.f), Frame.Size, /*bBlockingCollision*/ false);
		if (Hanging)
		{
			// hanging_picture_frame_01 ships with the engine checkerboard on its glass and its
			// frame and only its artwork slot textured — the same fault as the one in the bookcase.
			Hanging->SetMaterial(0, Frame.bGlazed ? Cast<UMaterialInterface>(MatGlass) : Cast<UMaterialInterface>(MatVoid));
			Hanging->SetMaterial(2, MatRoughWood);
		}
	}

	// And the two that came off the wall. What is left where they hung is a clean rectangle, which
	// is the only place in this room a hard edge is right: that is exactly the shape of the dirt
	// that did not land there. The nail is still in the plaster, which is what says the picture
	// was taken down rather than that it fell.
	const FVector2D Gone[2] = { FVector2D(-112.f, 246.f), FVector2D(94.f, 210.f) };
	for (const FVector2D& Ghost : Gone)
	{
		Build.Mark(FVector(WestFace + 1.f, BedCentre.Y + Ghost.X, Ghost.Y), FRotator(-90.f, 0.f, 0.f),
			FVector2D(BedRandom.FRandRange(30.f, 42.f), BedRandom.FRandRange(24.f, 34.f)), MatPaperDamp);
		Build.Cyl(FVector(WestFace + 3.f, BedCentre.Y + Ghost.X, Ghost.Y + 20.f), FRotator(0.f, 0.f, 90.f),
			FVector(1.f, 1.f, 5.f), MatIron, /*bBlockingCollision*/ false);
	}
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

	// Nothing on a shelf is square to it — but a row of books can only be *turned*, never leant.
	//
	// book_encyclopedia_set_01 is one rigid bar of twenty matched volumes, so anything built out
	// of it starts out looking machine-set, and four of them at the same angle at the same depth
	// on four shelves is a shop display, which is the one thing a room nobody has been into for
	// fifty years is not. Each row therefore gets its own height, its own angle across the shelf
	// and its own distance back from the edge.
	//
	// REVERSED: they used to get a lean as well — thirteen degrees of roll on the top one — on the
	// argument that a row with nothing holding up one end goes over and stays gone over. That is
	// true of books and false of this mesh. Rolling a rigid bar tips it like a brick: every volume
	// leans by exactly the same amount, the row stays perfectly straight while it does it, and
	// PropSeated then rests its lowest corner on the board, so the far end hangs in the air with
	// daylight under it. What it reads as is a solid block someone has propped up, and the end of
	// the bar turns to face the room while it is at it. Only yaw keeps the books on the shelf.
	// The angles are small, and that is the second half of the same lesson. A row half a metre
	// long swings its ends five centimetres in depth for every ten degrees of yaw, and the boards
	// are twenty deep: at eleven degrees one end of a row hung out past the front edge of the
	// carcass with nothing under it, which reads as a row of books falling out of the shelf.
	// Four degrees is a row nobody straightened; eleven is a row nobody could have put there.
	// Top shelf, shoved to one end, across the space the missing volumes left.
	Shelf.PropSeated(RoomProps::ShelfBooks, FVector(-24.f * S, Front + 3.f * S, ShelfZ[5]),
		FRotator(0.f, 4.f, 0.f), 26.f, /*bBlockingCollision*/ false);

	// The shelf below it: pushed the other way, because what is left of a row that has been raided
	// is groups at odd angles, not a row.
	Shelf.PropSeated(RoomProps::ShelfBooks, FVector(14.f * S, Front - 6.f * S, ShelfZ[4]),
		FRotator(0.f, -3.f, 0.f), 24.f, /*bBlockingCollision*/ false);

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
		FRotator(0.f, 2.f, 0.f), 23.f, /*bBlockingCollision*/ false);

	// The shelf under that is empty. It is the one the carcass lost, and an empty shelf is what
	// makes the full ones read as having been emptied rather than as decoration.

	// The bottom shelf and the floor in front of it are empty, and that is deliberate.
	//
	// Both had a row of books on their side. book_encyclopedia_set_01 is one rigid bar of twenty
	// matched volumes, and a rigid bar of twenty matched volumes lying down does not read as books
	// that fell — it reads as a chain of identical blocks laid end to end by somebody, which is
	// the opposite of what it is there to say. Tipped onto its side it also turns its fifty-five
	// centimetres broadside, so it ran out across the boards and back through the carcass.
	//
	// Books that have fallen off a shelf want individual books, which this asset cannot give: it
	// is a row or it is nothing. Nothing is better. The floor under the bookcase is bare, and the
	// only things standing along that stretch of skirting are the bottles.
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

	// The overturned chair, lying on its side: it went over backwards and nobody ever picked it up.
	//
	// Moved under the hook — near it rather than under it. A chair lying directly beneath a hook
	// in the ceiling of a house where a man hanged himself is not a detail, it is the answer, and
	// the room is not supposed to give that up in the first five minutes. A metre off to the side
	// is the distance at which the player can notice the two things are in the same part of the
	// room without being told what to do with it.
	//
	// The far side of the collapse, too: the hook is directly over the hole, and there is no floor
	// under it to stand a chair on.
	if (AClueActor* Chair = SpawnClue(FVector(178.f, 6.f, 0.f), FRotator(0.f, 34.f, 0.f), TEXT("Examine the chair"),
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
	// Built out of primitives rather than placed, and the reason is that the problem was never
	// colour. wall_clock is a clock somebody could buy this afternoon: a flat drum, a bezel flush
	// with its own face, a printed dial. Tinting that only produces a dark modern clock, and a
	// dark modern clock in a room where nothing is newer than the house is worse than a bright
	// one, because it is trying to hide. What reads as old is the silhouette — a turned wooden
	// drum, a brass bezel standing proud of a dial sunk behind it, roman numerals, and a glazed
	// trunk underneath with the pendulum dead still inside it. The mesh has none of those and
	// every one of them is a box or a cylinder. (The asset is still in the manifest; nothing in
	// the room places it any more.)
	//
	// The local frame, which everything below depends on: the dial looks along -Y, into the room,
	// and Y = 0 is the face of the plaster, so every part is built out from the wall at negative
	// Y. Pitch is rotation about Y, which is the axis the clock faces along, so pitch is the only
	// rotation that moves anything *on* the dial.
	//
	// The detective is on the low-Y side of this wall looking towards +Y, and for somebody facing
	// +Y the right hand points at **-X**. The old note here said the opposite and nothing ever
	// caught it, because a ring of twelve identical tick marks is its own mirror image: the clock
	// has been running backwards the whole time and only the numerals could show it. Twelve
	// arrived as IIX and four minutes past eleven was being read off as fifty-six minutes past
	// one. So an angle clockwise from twelve is (-sin, 0, cos) and a part turned to it is pitched
	// by the angle itself, not by its negative.
	//
	// Which is also why the whole clue is pitched rather than rolled: a roll tips the top of the
	// case away from the wall, and what a clock hung on one nail for fifty winters actually does
	// is sit crooked against it.
	if (AClueActor* Clock = SpawnClue(FVector(-40.f, DepthHalf + Setup.WallThickness * 0.5f, 215.f), FRotator(-3.f, 0.f, 0.f), TEXT("Examine the clock"),
		TEXT("Walnut gone black with the damp, and a brass bezel nobody has polished in fifty years. The pendulum hangs dead still behind its glass. It stopped at four minutes past eleven — and a clock this size runs eight days on a winding, so somebody was here to wind it, right up until they were not.")))
	{
		FRoomBuilder ClockBuild(Clock, Clock->GetRootScene());

		// Its own stream, so that tuning the clock does not reshuffle every clue built after it.
		FRandomStream ClockRandom(1104);

		// Walnut that has been in a wet room for decades: darker and browner than the beams, which
		// are bare softwood. The bezel and the bob are the one warm metal in the room — the tint
		// has to drag green_metal_rust's paint all the way to gold, the same correction the iron
		// and the rust already carry, only aimed somewhere else. Low roughness on it deliberately:
		// a bezel that catches the lantern as the detective turns is most of what says brass.
		UMaterialInstanceDynamic* CaseWood = ClockBuild.Surface(RoomSurfaces::RoughWood, FLinearColor(0.208f, 0.132f, 0.086f));
		UMaterialInstanceDynamic* Brass = ClockBuild.Surface(RoomSurfaces::RustedIron, FLinearColor(0.980f, 0.492f, 0.222f), 0.78f);
		UMaterialInstanceDynamic* HandMat = ClockBuild.Flat(FLinearColor(0.012f, 0.011f, 0.010f), 0.5f);

		// The dial: an enamelled plate that has yellowed. Flat, and that is not the mistake it
		// usually is. The rule everywhere else in this room is that a flat colour reads as a
		// rectangle of paint and a photograph reads as a surface — but that rule is about damage,
		// and a dial is the one thing in this house that genuinely is a smooth painted disc. It
		// was on the linen photograph first, and linen tiled onto a thirty-four-centimetre circle
		// is a panel of vertical stripes: the clock arrived with a dial made of floorboards. What
		// gives it variation is the foxing further down, which is the honest place for it.
		//
		// Held down to about what the plaster reflects, and no more. The first value was three
		// times that, on the argument that a dial is the one thing the detective has to be able to
		// read from across the floor — and it came out a disc of pure white, the brightest object
		// in a room whose entire point is that the only honest light in it is a flame. Contrast is
		// what makes numerals readable, not brightness, and near-black on old ivory is contrast
		// enough at a tenth of the exposure.
		UMaterialInstanceDynamic* DialMat = ClockBuild.Flat(FLinearColor(0.175f, 0.158f, 0.126f), 0.74f);

		// Depths, out from the plaster. The case hangs a little off the wall on its board, the
		// drum carries the movement, and the bezel stands two centimetres in front of the dial —
		// that inset is the difference between a cased clock and a plate screwed to a wall.
		const float BoardY = -1.6f;
		// The drum stops short of the dial on purpose. Both were built to end at the same depth,
		// which put the front cap of the case and the face of the dial on exactly the same plane:
		// the depth buffer cannot choose between two coincident surfaces, so the wood came through
		// the enamel in bands and the clock read as a dial made of floorboards. Half a centimetre
		// of daylight between them is the whole fix, and the bezel covers the step.
		const float DrumY = -8.5f;
		const float DialFaceY = -14.4f;
		const float MarkY = DialFaceY - 0.2f;
		const float BezelY = -15.5f;
		const float CrystalY = -16.2f;

		const float DialRadius = 17.2f;

		// A point on the dial, and the two directions that go with it: Out is twelve o'clock at
		// that angle, Side is the way the angle increases. A box pitched by -Angle has its own X
		// along Side and its own Z along Out, which is what makes numerals and marks possible at
		// all without writing a rotation for each one.
		// Out is twelve o clock carried round to this angle; Side is the way the angle increases,
		// which is the reading direction for a numeral standing at it.
		auto Radial = [](float Angle) { return FVector(-FMath::Sin(FMath::DegreesToRadians(Angle)), 0.f, FMath::Cos(FMath::DegreesToRadians(Angle))); };
		auto Lateral = [](float Angle) { return FVector(-FMath::Cos(FMath::DegreesToRadians(Angle)), 0.f, -FMath::Sin(FMath::DegreesToRadians(Angle))); };

		// --- The case ------------------------------------------------------------------------

		// The board it hangs on, then the drum. The board is narrower than the drum, so the case
		// reads as standing off the wall rather than as a disc glued to it.
		ClockBuild.Cyl(FVector(0.f, BoardY, 0.f), FRotator(0.f, 0.f, 90.f), FVector(30.f, 30.f, 3.2f), CaseWood);
		ClockBuild.Cyl(FVector(0.f, DrumY, 0.f), FRotator(0.f, 0.f, 90.f), FVector(39.f, 39.f, 10.6f), CaseWood);

		// The bezel, as a ring of forty segments. A cylinder cannot be a ring — it is solid, and a
		// solid disc in front of the dial is the flush modern bezel all over again, which is what
		// the asset already had. Forty facets on a forty-centimetre circle is a three-centimetre
		// chord; the polygon is there if you go looking for it and the depth is not.
		for (int32 Segment = 0; Segment < 40; ++Segment)
		{
			const float Angle = Segment * 9.f;
			ClockBuild.Box(Radial(Angle) * 18.3f + FVector(0.f, BezelY, 0.f), FRotator(Angle, 0.f, 0.f),
				FVector(3.05f, 2.2f, 3.2f), Brass, /*bBlockingCollision*/ false);
		}

		// --- The dial ------------------------------------------------------------------------

		ClockBuild.Cyl(FVector(0.f, DialFaceY + 0.6f, 0.f), FRotator(0.f, 0.f, 90.f),
			FVector(DialRadius * 2.f, DialRadius * 2.f, 1.2f), DialMat, /*bBlockingCollision*/ false);

		// The chapter ring: one continuous engraved circle, drawn as sixty-four overlapping
		// segments. Not a minute track of sixty separate ticks — at three millimetres apiece those
		// are isolated slivers thinner than a pixel from the far side of the room, and a ring of
		// crawling dots is worse than no ring. A closed line has no slivers in it.
		for (int32 Segment = 0; Segment < 64; ++Segment)
		{
			const float Angle = Segment * (360.f / 64.f);
			ClockBuild.Box(Radial(Angle) * 15.6f + FVector(0.f, MarkY, 0.f), FRotator(Angle, 0.f, 0.f),
				FVector(1.65f, 0.34f, 0.55f), HandMat, /*bBlockingCollision*/ false);
		}

		// Roman numerals, which is the single thing on a dial that cannot be mistaken for modern.
		//
		// Each is drawn as strokes in the numeral's own frame — up is outward, across is the way
		// the hour increases — so the whole ring reads the right way round without a hand-written
		// transform per hour. I is one bar; V is two bars meeting at the foot; X is two crossed.
		// Four is IIII and not IV: that is what clockmakers actually painted, for the balance of
		// it against the VIII opposite, and it is free.
		const float NumeralHeight = 4.0f;
		auto Numeral = [&](float Angle, const TCHAR* Glyphs)
		{
			const FVector Out = Radial(Angle);
			const FVector Side = Lateral(Angle);
			const float Lean = 15.f;
			const float LeanSpread = FMath::Sin(FMath::DegreesToRadians(Lean)) * NumeralHeight * 0.5f;

			auto Advance = [](TCHAR Glyph) { return Glyph == TEXT('I') ? 1.30f : 2.70f; };

			float Width = 0.f;
			for (const TCHAR* Scan = Glyphs; *Scan; ++Scan)
			{
				Width += Advance(*Scan);
			}

			float Cursor = -Width * 0.5f;
			for (const TCHAR* Scan = Glyphs; *Scan; ++Scan)
			{
				const float Step = Advance(*Scan);
				const float Middle = Cursor + Step * 0.5f;
				Cursor += Step;

				// Painted marks that have been damp for decades do not stay square to anything.
				const float Skew = ClockRandom.FRandRange(-1.8f, 1.8f);
				const float Height = NumeralHeight * ClockRandom.FRandRange(0.94f, 1.f);

				auto Stroke = [&](float Tilt, float Offset)
				{
					ClockBuild.Box(
						Side * (Middle + Offset) + Out * 12.3f + FVector(0.f, MarkY, 0.f),
						FRotator(Angle + Skew + Tilt, 0.f, 0.f),
						FVector(0.62f, 0.34f, Height),
						HandMat, /*bBlockingCollision*/ false);
				};

				if (*Scan == TEXT('I'))
				{
					Stroke(0.f, 0.f);
				}
				else if (*Scan == TEXT('V'))
				{
					Stroke(Lean, LeanSpread);
					Stroke(-Lean, -LeanSpread);
				}
				else
				{
					Stroke(17.f, 0.f);
					Stroke(-17.f, 0.f);
				}
			}
		};

		static const TCHAR* Hours[12] = {
			TEXT("XII"), TEXT("I"), TEXT("II"), TEXT("III"), TEXT("IIII"), TEXT("V"),
			TEXT("VI"), TEXT("VII"), TEXT("VIII"), TEXT("IX"), TEXT("X"), TEXT("XI") };
		for (int32 Hour = 0; Hour < 12; ++Hour)
		{
			Numeral(Hour * 30.f, Hours[Hour]);
		}

		// Two winding holes, because this is a clock that had to be wound — one train for the
		// going, one for the strike. They are the detail that makes the dial a mechanism's face
		// rather than a printed circle, and they are two discs.
		for (int32 Hole = 0; Hole < 2; ++Hole)
		{
			const float Angle = Hole == 0 ? 118.f : 242.f;
			ClockBuild.Cyl(Radial(Angle) * 7.4f + FVector(0.f, DialFaceY + 0.1f, 0.f), FRotator(0.f, 0.f, 90.f),
				FVector(2.2f, 2.2f, 0.8f), HandMat, /*bBlockingCollision*/ false);
		}

		// Foxing: the brown bloom an enamelled dial grows in a house with water in the walls. A
		// stain rather than a painted disc — the whole point of the decal is that it has no edge.
		ClockBuild.Stain(RoomSurfaces::Damp, FVector(-6.f, DialFaceY - 4.5f, -7.5f), FRotator(0.f, 90.f, 0.f),
			FVector2D(12.f, 10.f), FLinearColor(0.300f, 0.196f, 0.120f), 0.20f, 1.f);

		// --- The hands ----------------------------------------------------------------------

		// Spade hands with a counterpoised tail: shaft, a diamond two thirds of the way out, and a
		// weight behind the centre. Two plain bars is a wristwatch; the diamond and the tail are
		// what a clock of this age has, and they are three boxes each.
		auto Hand = [&](float Degrees, float Reach, float Tail, float Thickness, float Y)
		{
			const FVector Out = Radial(Degrees);
			ClockBuild.Box(Out * ((Reach - Tail) * 0.5f) + FVector(0.f, Y, 0.f), FRotator(Degrees, 0.f, 0.f),
				FVector(Thickness, 0.55f, Reach + Tail), HandMat, /*bBlockingCollision*/ false);
			ClockBuild.Box(Out * (Reach * 0.62f) + FVector(0.f, Y, 0.f), FRotator(Degrees + 45.f, 0.f, 0.f),
				FVector(Thickness * 2.9f, 0.5f, Thickness * 2.9f), HandMat, /*bBlockingCollision*/ false);
			ClockBuild.Box(Out * (-Tail * 0.72f) + FVector(0.f, Y, 0.f), FRotator(Degrees + 45.f, 0.f, 0.f),
				FVector(Thickness * 2.2f, 0.5f, Thickness * 2.2f), HandMat, /*bBlockingCollision*/ false);
		};

		// Stopped at four minutes past eleven. The hour is not a clue and is not meant to be
		// solved — it is the date the house keeps coming back to, and the detective has no way
		// yet of knowing why that should mean anything.
		Hand(332.f, 10.6f, 2.6f, 1.05f, -14.95f); // hour
		Hand(24.f, 15.2f, 3.4f, 0.8f, -15.35f);   // minute

		// The collet the hands sit on, and the cap over it.
		ClockBuild.Cyl(FVector(0.f, -15.75f, 0.f), FRotator(0.f, 0.f, 90.f), FVector(3.2f, 3.2f, 1.1f), Brass, /*bBlockingCollision*/ false);
		ClockBuild.Cyl(FVector(0.f, -15.95f, 0.f), FRotator(0.f, 0.f, 90.f), FVector(1.4f, 1.4f, 0.7f), HandMat, /*bBlockingCollision*/ false);

		// The crystal, held under the inner lip of the bezel. Very nearly nothing, and far clearer
		// than the window glass that is the room default: the window is meant to be dirty and this
		// is meant to be looked through. A mirror-smooth disc thirty-four centimetres across
		// standing in front of a lantern is not a crystal, it is a white plate — at the value the
		// panes use, the reflection took the numerals and then the whole dial with them.
		ClockBuild.Cyl(FVector(0.f, CrystalY, 0.f), FRotator(0.f, 0.f, 90.f), FVector(34.f, 34.f, 0.5f),
			ClockBuild.Glass(FLinearColor(0.16f, 0.17f, 0.17f), 0.035f, 0.14f), /*bBlockingCollision*/ false);

		// No starred glass on it, and the clue text carries that instead.
		//
		// It was seven thin bars radiating from a point off the middle of the dial, which on a
		// face that already has two bars radiating from the middle of it is not a crack — it is
		// nine hands. The same lesson as the flaking paint on the door: at this size a box is
		// never a hairline, it is a stick, and a split in glass needs a torn alpha to be a split
		// at all. Better an uncracked crystal than a clock with too many hands.

		// --- The trunk ------------------------------------------------------------------------

		// The drop under the drum, which is the part doing most of the work. A round clock on a
		// wall is a round clock on a wall whatever it is made of; a drum with a glazed trunk
		// hanging off it is a weight-driven clock, and there has not been one of those in a house
		// anybody would call modern.
		//
		// Built as a frame rather than a box with a dark rectangle painted on the front: the
		// pendulum has to be genuinely inside something for the glass to be glass.
		ClockBuild.Box(FVector(0.f, -8.2f, -21.f), FRotator::ZeroRotator, FVector(19.f, 10.f, 8.f), CaseWood);
		ClockBuild.Box(FVector(0.f, -3.9f, -40.f), FRotator::ZeroRotator, FVector(22.f, 1.4f, 30.f), CaseWood, /*bBlockingCollision*/ false);
		ClockBuild.Box(FVector(-9.3f, -8.2f, -40.f), FRotator::ZeroRotator, FVector(3.4f, 10.f, 30.f), CaseWood);
		ClockBuild.Box(FVector(9.3f, -8.2f, -40.f), FRotator::ZeroRotator, FVector(3.4f, 10.f, 30.f), CaseWood);
		ClockBuild.Box(FVector(0.f, -8.2f, -26.7f), FRotator::ZeroRotator, FVector(22.f, 10.f, 3.4f), CaseWood);
		ClockBuild.Box(FVector(0.f, -8.2f, -53.2f), FRotator::ZeroRotator, FVector(22.f, 10.f, 3.6f), CaseWood);

		// The trunk glass is the dirty one. Nobody has opened this door in decades and the room
		// has been coming through it the whole time.
		ClockBuild.Box(FVector(0.f, -12.9f, -40.f), FRotator::ZeroRotator, FVector(15.2f, 0.5f, 23.f),
			ClockBuild.Glass(RoomPalette::GlassShard, 0.14f, 0.22f), /*bBlockingCollision*/ false);

		// The pendulum, hanging plumb inside a case that is not.
		//
		// That is the whole trick of it: the clue is pitched three degrees, so a pendulum built
		// straight down the trunk would be three degrees off vertical — which is the one thing in
		// the world that never is. Turned back by the same three about its suspension point, it
		// hangs true and the case leans past it, and the case reads as crooked instead of the
		// room. Dead still, because the clock is stopped: a pendulum at rest is a plumb line.
		const float PendulumY = -8.4f;
		ClockBuild.Box(FVector(0.73f, PendulumY, -32.97f), FRotator(3.f, 0.f, 0.f), FVector(0.9f, 0.9f, 28.f), Brass, /*bBlockingCollision*/ false);
		ClockBuild.Cyl(FVector(1.47f, PendulumY, -46.96f), FRotator(0.f, 0.f, 90.f), FVector(8.2f, 8.2f, 1.6f), Brass, /*bBlockingCollision*/ false);
		ClockBuild.Cyl(FVector(1.6f, PendulumY, -51.6f), FRotator(0.f, 0.f, 90.f), FVector(2.2f, 2.2f, 1.2f), Brass, /*bBlockingCollision*/ false);

		// The finial under the case. A turned drop, which is three primitives and the last of the
		// silhouette — a trunk that stops square at the bottom is a cupboard.
		ClockBuild.Cyl(FVector(0.f, -8.2f, -56.4f), FRotator(0.f, 0.f, 90.f), FVector(6.4f, 6.4f, 2.4f), CaseWood, /*bBlockingCollision*/ false);
		ClockBuild.Sph(FVector(0.f, -8.2f, -59.4f), 5.2f, CaseWood);
		ClockBuild.Cyl(FVector(0.f, -8.2f, -62.8f), FRotator(0.f, 0.f, 90.f), FVector(2.2f, 2.2f, 2.6f), CaseWood, /*bBlockingCollision*/ false);
	}

	// The mirror, on the same wall as the clock, between it and the door.
	//
	// Nothing shows in it, and that is the whole point of it rather than a limitation. A real
	// mirror is a planar reflection — expensive, and in a room lit by one flame it would hand the
	// player a second view of everything the lantern is carefully not showing them. What this one
	// is instead is the honest end state of a mirror that has hung in a wet house since the
	// sixties: the silvering behind the glass has gone black, so the glass holds no lantern, no
	// room and no detective. It is also the first quiet placement of the thing the whole story
	// ends on — the dusty mirror he wipes at the finale — and a mirror that has already refused
	// him once is worth more then.
	//
	// The glass is a photographed surface held at three per cent albedo rather than a flat black,
	// for the usual reason: flat black at this size is a hole cut in the wall, and what makes it
	// read as glass is that there is *something* in it, faint, that does not move.
	//
	// Same frame as the clock: the face looks along -Y into the room, Y = 0 is the plaster, and
	// the crooked hang is pitch, which is rotation about the axis the mirror faces along.
	if (AClueActor* Mirror = SpawnClue(FVector(48.f, DepthHalf + Setup.WallThickness * 0.5f, 168.f), FRotator(-2.2f, 0.f, 0.f), TEXT("Examine the mirror"),
		TEXT("A mirror, in a frame somebody once thought a great deal of, and broken — struck once, low and off centre, and left. The silver behind what is left of the glass has gone black: it gives back no lantern, no room, and no detective. Only the dark. He holds the light closer, and the dark does not move.")))
	{
		FRoomBuilder MirrorBuild(Mirror, Mirror->GetRootScene());

		UMaterialInstanceDynamic* Frame = MirrorBuild.Surface(RoomSurfaces::RoughWood, FLinearColor(0.152f, 0.096f, 0.062f));
		UMaterialInstanceDynamic* Gilt = MirrorBuild.Surface(RoomSurfaces::RustedIron, FLinearColor(0.870f, 0.436f, 0.196f), 0.8f);
		UMaterialInstanceDynamic* Dead = MirrorBuild.Surface(RoomSurfaces::Damp, FLinearColor(0.031f, 0.032f, 0.031f), 1.35f);
		UMaterialInstanceDynamic* Iron = MirrorBuild.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));

		// The board the glass is bedded on. It is what the player is looking at wherever the glass
		// is gone, so it is the darkest thing in the frame rather than the back of a cupboard.
		MirrorBuild.Box(FVector(0.f, -1.6f, 0.f), FRotator::ZeroRotator, FVector(62.f, 3.2f, 100.f), Frame);
		MirrorBuild.Box(FVector(0.f, -2.6f, 0.f), FRotator::ZeroRotator, FVector(54.f, 0.8f, 84.f), MatVoid, /*bBlockingCollision*/ false);

		// And the glass, which is not a pane any more.
		//
		// Struck once, low and off centre. What is left is seven pieces still in the rebate, each
		// turned off the others and each at its own depth in it — that last part is what does the
		// work, because a mirror reads as broken through the *edges* of its pieces: every shard
		// catches the lantern along a different line, and a network of bright lines that do not
		// join is the one thing a whole sheet of glass can never produce. Two are gone altogether
		// and the board shows through where they were.
		//
		// Overhanging the opening is fine and deliberate: the rails stand five centimetres in
		// front of the glass, so a shard wider than the rebate is hidden behind the frame, exactly
		// as a real one is. And the rotation is pitch — the axis the mirror faces along, so the
		// pieces turn *in* the glass rather than out of it.
		struct FShard { float X; float Z; float Wide; float Tall; float Turn; float Depth; };
		const FShard Shards[7] = {
			{ -17.f,  26.f, 27.f, 39.f,  12.f, -3.4f },
			{  13.f,  30.f, 29.f, 31.f, -18.f, -4.1f },
			{ -21.f,  -8.f, 25.f, 45.f,  -7.f, -3.6f },
			{  11.f,  -3.f, 23.f, 37.f,  24.f, -4.4f },
			{  -7.f, -32.f, 31.f, 27.f,   9.f, -3.5f },
			{  20.f, -27.f, 23.f, 35.f, -14.f, -4.2f },
			{   3.f,   7.f, 19.f, 17.f,  38.f, -3.9f },
		};
		for (const FShard& Piece : Shards)
		{
			MirrorBuild.Box(FVector(Piece.X, Piece.Depth, Piece.Z), FRotator(Piece.Turn, 0.f, 0.f),
				FVector(Piece.Wide, 0.9f, Piece.Tall), Dead, /*bBlockingCollision*/ false);
		}

		// The fracture network over the whole of it, projected rather than modelled: a split in
		// glass is a contour, and no arrangement of boxes is a hairline.
		MirrorBuild.Crack(FVector(4.f, -8.f, -6.f), FRotator(0.f, 90.f, 0.f), FVector2D(58.f, 86.f), 0.9f, 21.f);

		// What came out of it is on the floor under it, because nobody swept this room either.
		for (int32 Piece = 0; Piece < 7; ++Piece)
		{
			MirrorBuild.Box(
				FVector(Random.FRandRange(-34.f, 34.f), -Random.FRandRange(14.f, 46.f), -162.f),
				FRotator(0.f, Random.FRandRange(0.f, 360.f), Random.FRandRange(-9.f, 9.f)),
				FVector(Random.FRandRange(4.f, 13.f), Random.FRandRange(3.f, 9.f), 0.9f),
				Dead, /*bBlockingCollision*/ false);
		}

		// The frame: four rails standing five centimetres proud of the glass, with a block at each
		// corner. The depth is what makes it a frame — a flat border painted round a mirror is a
		// picture of a frame, and the shadow the rail throws onto the glass is most of what says
		// there is a rebate behind it.
		MirrorBuild.Box(FVector(0.f, -5.6f, 46.f), FRotator::ZeroRotator, FVector(68.f, 4.8f, 8.f), Frame, /*bBlockingCollision*/ false);
		MirrorBuild.Box(FVector(0.f, -5.6f, -46.f), FRotator::ZeroRotator, FVector(68.f, 4.8f, 8.f), Frame, /*bBlockingCollision*/ false);
		MirrorBuild.Box(FVector(-30.f, -5.6f, 0.f), FRotator::ZeroRotator, FVector(8.f, 4.8f, 100.f), Frame, /*bBlockingCollision*/ false);
		MirrorBuild.Box(FVector(30.f, -5.6f, 0.f), FRotator::ZeroRotator, FVector(8.f, 4.8f, 100.f), Frame, /*bBlockingCollision*/ false);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float CornerX = (Corner % 2 == 0) ? -30.f : 30.f;
			const float CornerZ = (Corner < 2) ? 46.f : -46.f;
			MirrorBuild.Box(FVector(CornerX, -6.2f, CornerZ), FRotator::ZeroRotator, FVector(10.f, 5.6f, 10.f), Frame, /*bBlockingCollision*/ false);
		}

		// The gilt bead round the opening. Most of the gold is gone; what is left of it is the one
		// warm thing on this wall, and it is the detail that separates a mirror somebody paid for
		// from a sheet of glass in a box.
		MirrorBuild.Box(FVector(0.f, -6.6f, 41.f), FRotator::ZeroRotator, FVector(56.f, 2.4f, 2.2f), Gilt, /*bBlockingCollision*/ false);
		MirrorBuild.Box(FVector(0.f, -6.6f, -41.f), FRotator::ZeroRotator, FVector(56.f, 2.4f, 2.2f), Gilt, /*bBlockingCollision*/ false);
		MirrorBuild.Box(FVector(-26.f, -6.6f, 0.f), FRotator::ZeroRotator, FVector(2.2f, 2.4f, 84.f), Gilt, /*bBlockingCollision*/ false);
		MirrorBuild.Box(FVector(26.f, -6.6f, 0.f), FRotator::ZeroRotator, FVector(2.2f, 2.4f, 84.f), Gilt, /*bBlockingCollision*/ false);

		// A crest over the top of it, which is the silhouette that says the frame was carved
		// rather than cut: a stepped cornice and a turned finial.
		MirrorBuild.Box(FVector(0.f, -5.2f, 52.5f), FRotator::ZeroRotator, FVector(32.f, 4.6f, 7.f), Frame, /*bBlockingCollision*/ false);
		MirrorBuild.Box(FVector(0.f, -5.2f, 57.5f), FRotator::ZeroRotator, FVector(19.f, 4.2f, 5.f), Frame, /*bBlockingCollision*/ false);
		MirrorBuild.Sph(FVector(0.f, -5.2f, 62.f), 7.f, Frame);

		// The nail it hangs off, which is why it is not level. Just the nail: the cord is behind the
		// frame, where a cord is.
		//
		// It had a visible pair of them before, and they were antennas. Two faults at once, both
		// worth remembering. They were angled with *roll*, and roll on this wall tips a part out
		// of the plaster into the room — in-plane is pitch, the axis the mirror faces along, which
		// is the same thing the picture frames over the bed got wrong. And they were thirty-two
		// centimetres long against a crest sixty-five high, so even aimed correctly they would
		// have stood a hand's width over the top of the frame. Nothing about a hanging cord is
		// visible on a mirror this size except the nail above it.
		MirrorBuild.Cyl(FVector(0.f, -2.4f, 69.f), FRotator(0.f, 0.f, 90.f), FVector(1.1f, 1.1f, 4.4f), Iron, /*bBlockingCollision*/ false);

		// Damp in the glass rather than on it: the blooms where the silvering let go first. Aimed
		// along +Y, into the front of the mirror, and the nine-centimetre reach stops well short
		// of the plaster behind it.
		for (int32 Bloom = 0; Bloom < 3; ++Bloom)
		{
			MirrorBuild.Stain(RoomSurfaces::Damp,
				FVector(Random.FRandRange(-16.f, 16.f), -10.f, Random.FRandRange(-30.f, 30.f)),
				FRotator(0.f, 90.f, Random.FRandRange(0.f, 360.f)),
				FVector2D(Random.FRandRange(20.f, 38.f), Random.FRandRange(18.f, 34.f)),
				FLinearColor(0.055f, 0.050f, 0.044f), Random.FRandRange(0.5f, 0.8f), 1.f, 1.4f);
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

	// Bottles against the skirting, lined up rather than thrown. Someone sat here and drank,
	// methodically.
	//
	// Along the north wall, not in the corner the bookcase is standing in: at the old spot the row
	// was inside the carcass, so six translucent bottles showed through the bottom of the shelves
	// like something half-materialised. This stretch of wall is the only one long enough for them
	// — the wardrobe takes everything left of 175 and the bookcase everything right of 327.
	if (AClueActor* Bottles = SpawnClue(FVector(240.f, -DepthHalf + 26.f, 0.f), FRotator(0.f, 90.f, 0.f), TEXT("Examine the bottles"),
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
	//
	// Built, because decorative_book_set_01 is not a stack of books — it is a *catalogue*. Ninety
	// different volumes stood side by side in a single mesh, the way a library ships them for
	// picking, so placing it as one prop laid a two-metre rank of pale upright books across the
	// boards beside the shelf: a white comb on the floor of a room in which nothing is white. That
	// was already known when the same asset came off the bookcase floor, and this clue kept it.
	// The manifest still fetches it; nothing places it now.
	//
	// A volume is a block of leaves between two cloth boards with a spine down one side — four
	// boxes, and the leaves have to be left showing on the other three edges, because a single
	// box wrapped round them is a brick. Cloth and dusty page edges are both already in the room:
	// the linen photograph, tinted the way the curtains and the loose papers are.
	if (AClueActor* Books = SpawnClue(FVector(WidthHalf - 78.f, -186.f, 4.f), FRotator(0.f, 27.f, 0.f), TEXT("Examine the books"),
		TEXT("Ledgers, not novels — columns of dates and figures in the same tight hand as the letter. The dust on top is thick enough to write in. Nobody has.")))
	{
		FRoomBuilder BookBuild(Books, Books->GetRootScene());

		// Its own stream, so the size of the stack does not relay every clue built after it.
		FRandomStream StackRandom(1955);

		// Spine to fore-edge along the volume’s own X, head to tail along its Y.
		auto Ledger = [&](const FVector& Middle, const FRotator& Lie, float Wide, float Deep, float Thick, bool bBlocks)
		{
			BookBuild.Box(Middle, Lie, FVector(Wide - 1.8f, Deep - 1.8f, Thick - 1.6f), MatPaperDamp, bBlocks);
			BookBuild.Box(Middle + Lie.RotateVector(FVector(0.f, 0.f, (Thick - 0.8f) * 0.5f)), Lie, FVector(Wide, Deep, 0.8f), MatCloth, /*bBlockingCollision*/ false);
			BookBuild.Box(Middle - Lie.RotateVector(FVector(0.f, 0.f, (Thick - 0.8f) * 0.5f)), Lie, FVector(Wide, Deep, 0.8f), MatCloth, /*bBlockingCollision*/ false);
			BookBuild.Box(Middle + Lie.RotateVector(FVector(-Wide * 0.5f, 0.f, 0.f)), Lie, FVector(1.4f, Deep, Thick), MatCloth, /*bBlockingCollision*/ false);
		};

		// Five of them. No two the same size and no two square to each other: a pile somebody put
		// down is not a pile somebody built.
		float Rest = 0.f;
		for (int32 Volume = 0; Volume < 5; ++Volume)
		{
			const float Wide = StackRandom.FRandRange(21.f, 27.f);
			const float Deep = StackRandom.FRandRange(30.f, 36.f);
			const float Thick = StackRandom.FRandRange(4.f, 7.f);
			Ledger(
				FVector(StackRandom.FRandRange(-2.5f, 2.5f), StackRandom.FRandRange(-2.5f, 2.5f), Rest + Thick * 0.5f),
				FRotator(0.f, StackRandom.FRandRange(-11.f, 11.f), 0.f),
				Wide, Deep, Thick, /*bBlocks*/ Volume == 0);
			Rest += Thick;
		}

		// And one that came off the top and stayed where it landed, on its side against the pile.
		// A stack with a straight top edge is a stack somebody squared up.
		Ledger(FVector(19.f, -8.f, 12.f), FRotator(0.f, 58.f, 76.f), 23.f, 33.f, 5.4f, /*bBlocks*/ false);
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
