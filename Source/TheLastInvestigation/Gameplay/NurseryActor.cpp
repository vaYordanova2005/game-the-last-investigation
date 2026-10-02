#include "NurseryActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "StormWindowActor.h"
#include "DustMotesComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Engine/World.h"

namespace
{
	/** Collision that only a walking man meets: the interaction trace and the camera pass through. */
	void NurseryPawnOnly(UStaticMeshComponent* Part)
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

	/** A child scene component to build a group of parts in its own frame: a desk top, a shelf. */
	USceneComponent* NurseryPivot(AActor* Owner, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation, const TCHAR* Name)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(Owner, MakeUniqueObjectName(Owner, USceneComponent::StaticClass(), Name));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		Pivot->SetRelativeLocationAndRotation(Location, Rotation);
		Pivot->RegisterComponent();
		Owner->AddInstanceComponent(Pivot);
		return Pivot;
	}

	/**
	 * Keeps the wall's decals off things pinned to it (the kitchen calendar's note): paper a
	 * centimetre off the plaster is inside the box of every crack and stain behind it.
	 */
	void NurseryNoDecals(AActor* Actor)
	{
		TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
		for (UPrimitiveComponent* Part : Parts)
		{
			Part->SetReceivesDecals(false);
		}
	}

	/** Sets every sheet instance used on a model's own UVs, or a generated cloth's, to one repeat. */
	void NurserySheet(UMaterialInstanceDynamic* Mat)
	{
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}

	/** How far off the wall face anything pinned over the wallpaper goes: the paper's face is at 1.1. */
	constexpr float PaperProud = 1.5f;

	/** Height the drawings' A4 page takes of one cell in the atlas: 724 of 2048 (make_nursery_art.py). */
	constexpr float DrawingPageV = 724.f / 2048.f;
}

ANurseryActor::ANurseryActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);
	RoomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void ANurseryActor::Configure(const FNurserySetup& InSetup, AStormWindowActor* InLeadStorm)
{
	Setup = InSetup;
	LeadStorm = InLeadStorm;

	// Here and not in BeginPlay: the motes are scattered in the component's own BeginPlay, which
	// runs before the owner's (see ARoomDressingActor::Configure).
	DustMotes->ConfigureVolume(
		FVector(RoomWidth * 0.48f, RoomDepth * 0.48f, RoomHeight * 0.46f),
		FVector(MidX(), MidY(), RoomHeight * 0.5f));

	Openings = {
		{ EWall::North, Setup.DoorX, Setup.DoorHalf, 0.f, Setup.DoorHeight },
		{ EWall::East, WindowY(), WindowWidth * 0.5f, WindowSill, WindowTop },
	};
}

void ANurseryActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning this room never relays the corridor or the bedroom. Her birthday.
	Random.Initialize(20130611);

	FRoomBuilder Build(this, RoomRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildFloor(Build);
	BuildWalls(Build);
	BuildCeiling(Build);
	BuildDamage(Build);
	BuildBed(Build);
	BuildNightstand(Build);
	BuildWardrobe(Build);
	BuildDesk(Build);
	BuildShelves(Build);
	BuildToys(Build);
	BuildDrawings(Build);
	BuildFloorThings(Build);

	SpawnWindow();
	BuildClues();
}

void ANurseryActor::CacheMaterials(FRoomBuilder& Build)
{
	// The house's rule holds in here too: around a tenth reflectance, and warm, because the only
	// fill is the cold window. What changes is the hue. The room was pink and cream once, and the
	// tints are worked back from each photograph's measured mean so that what is left of that is
	// a dusty rose and an old ivory rather than a stain — the same arithmetic as the curtains.
	MatPlaster = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f));
	// nursery_wallpaper measures linear (0.740, 0.502, 0.449): this lands it at about
	// (0.125, 0.088, 0.078), shell pink gone to the colour of old skin.
	MatWallpaper = Build.Surface(RoomSurfaces::NurseryWallpaper, FLinearColor(0.169f, 0.175f, 0.174f));
	// The strips coming away are generated sheets on their own UVs (Cloth), so their own instance.
	MatWallpaperPeel = Build.Surface(RoomSurfaces::NurseryWallpaper, FLinearColor(0.150f, 0.152f, 0.150f), 1.05f);
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.45f, 0.44f, 0.42f));
	MatFloorboards = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.58f, 0.55f, 0.52f));
	MatFloorboardsWorn = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.40f, 0.36f, 0.32f));
	// Cream-painted joinery: skirting, picture rail, the door and window casings.
	MatTrim = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.300f, 0.300f, 0.310f));

	// The furniture's paint, on the cracked plaster photograph (0.510, 0.441, 0.345), whose crazing
	// reads on wood as paint that has cracked with it. These go on the generated models' own UVs,
	// laid out in repeats of the photograph, so every instance here has its tiling at one.
	// Lands at about (0.17, 0.085, 0.085). The first figure, (0.15, 0.095, 0.09), was a dusty rose
	// on paper and beige under the lantern's warm light, which takes the pink out of anything that
	// is not plainly pink.
	MatPink = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.340f, 0.192f, 0.248f), 1.05f);
	MatPinkInside = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.226f, 0.130f, 0.165f), 1.1f);
	// White paint: the brightest thing in the room by intent, and still under a fifth.
	MatWhite = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.333f, 0.355f, 0.405f), 1.05f);
	MatBareWood = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.80f, 0.78f, 0.74f));
	// Cloth on the linen photograph (0.284, 0.408, 0.614 — a blue linen; see the bedroom's notes).
	MatMattress = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.50f, 0.32f, 0.19f), 1.1f);
	MatSheet = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.52f, 0.34f, 0.20f), 1.1f);
	// The quilt is the curtains' rose print, a shade lighter: one set, bought together.
	MatQuilt = Build.Surface(RoomSurfaces::FloralFabric, FLinearColor(0.160f, 0.172f, 0.165f), 1.1f);
	// floral_jacquard is a dark grey (0.054, 0.053, 0.060); a dusty rose rug from it.
	MatRug = Build.Surface(RoomSurfaces::Carpet, FLinearColor(1.65f, 1.05f, 0.95f), 1.3f);
	MatBearFur = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.36f, 0.165f, 0.06f), 1.2f);
	MatRabbitFur = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.42f, 0.27f, 0.165f), 1.2f);
	MatPad = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.56f, 0.32f, 0.165f), 1.2f);
	MatDollSkin = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.56f, 0.27f, 0.14f), 1.2f);
	// The linen's own blue, faded: the one blue thing in the room is the doll's dress.
	MatDollDress = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.25f, 0.20f, 0.17f), 1.2f);
	MatDollHair = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.28f, 0.10f, 0.025f), 1.2f);
	// A lilac backpack: twelve, not six.
	MatBackpack = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.25f, 0.125f, 0.15f), 1.1f);
	// Lilac linen: on the quilt's own print they were a pink slab with no edges.
	MatPajamas = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.36f, 0.20f, 0.19f), 1.1f);
	MatGarment = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.10f, 0.08f, 0.09f), 1.2f);
	MatGarmentDark = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.20f, 0.12f, 0.10f), 1.2f);
	for (UMaterialInstanceDynamic* Sheet : { MatPink.Get(), MatPinkInside.Get(), MatWhite.Get(), MatMattress.Get(), MatSheet.Get(),
		MatQuilt.Get(), MatRug.Get(), MatBearFur.Get(), MatRabbitFur.Get(), MatPad.Get(), MatDollSkin.Get(),
		MatDollDress.Get(), MatDollHair.Get(), MatBackpack.Get(), MatPajamas.Get(), MatGarment.Get(), MatGarmentDark.Get(),
		MatWallpaperPeel.Get() })
	{
		NurserySheet(Sheet);
	}

	MatBrass = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatPaper = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.464f, 0.245f, 0.108f));
	MatPaperDamp = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.260f, 0.142f, 0.065f));
	MatPageEdge = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.380f, 0.200f, 0.090f));
	MatRubble = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.145f, 0.127f, 0.110f));
	MatTape = Build.Flat(FLinearColor(0.20f, 0.17f, 0.09f), 0.45f);
	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.035f, 0.06f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);
	MatShadow = Build.Flat(FLinearColor(0.002f, 0.002f, 0.002f), 1.f);
	// Blue ballpoint, faded, and pencil.
	MatInk = Build.Flat(FLinearColor(0.010f, 0.016f, 0.045f), 0.7f);
	MatPencil = Build.Flat(FLinearColor(0.030f, 0.030f, 0.032f), 0.5f);

	// Flat colours, by name: the small hard things, and the dull pastels of a child's colours gone
	// to dust. Nothing in here is saturated any more, and nothing is bright.
	struct FFlat { const TCHAR* Name; FLinearColor Color; float Roughness; float Metallic; };
	const FFlat FlatList[] = {
		{ TEXT("Button"), FLinearColor(0.010f, 0.008f, 0.007f), 0.3f, 0.f },
		{ TEXT("Nose"), FLinearColor(0.030f, 0.012f, 0.010f), 0.6f, 0.f },
		{ TEXT("Ribbon"), FLinearColor(0.150f, 0.035f, 0.050f), 0.55f, 0.f },
		{ TEXT("Cheek"), FLinearColor(0.180f, 0.060f, 0.060f), 0.9f, 0.f },
		{ TEXT("Shoe"), FLinearColor(0.040f, 0.012f, 0.010f), 0.5f, 0.f },
		{ TEXT("Zip"), FLinearColor(0.090f, 0.088f, 0.090f), 0.35f, 1.f },
		{ TEXT("Strap"), FLinearColor(0.025f, 0.020f, 0.028f), 0.8f, 0.f },
		{ TEXT("Charm"), FLinearColor(0.200f, 0.110f, 0.170f), 0.95f, 0.f },
		{ TEXT("Plastic"), FLinearColor(0.200f, 0.140f, 0.180f), 0.45f, 0.f },
		{ TEXT("Cushion"), FLinearColor(0.030f, 0.030f, 0.034f), 0.85f, 0.f },
		// Silicone, dusty pink.
		{ TEXT("Case"), FLinearColor(0.260f, 0.095f, 0.125f), 0.65f, 0.f },
		// Black glass: the one surface in here with no dust under the dust.
		{ TEXT("Screen"), FLinearColor(0.003f, 0.003f, 0.0035f), 0.07f, 0.f },
		{ TEXT("Lens"), FLinearColor(0.010f, 0.010f, 0.012f), 0.1f, 0.f },
		{ TEXT("Velvet"), FLinearColor(0.070f, 0.012f, 0.022f), 0.95f, 0.f },
		{ TEXT("Mirror"), FLinearColor(0.030f, 0.030f, 0.028f), 0.06f, 1.f },
		{ TEXT("Figure"), FLinearColor(0.240f, 0.210f, 0.200f), 0.4f, 0.f },
		{ TEXT("Canvas"), FLinearColor(0.200f, 0.190f, 0.180f), 0.95f, 0.f },
		{ TEXT("Sole"), FLinearColor(0.220f, 0.215f, 0.205f), 0.8f, 0.f },
		{ TEXT("Lace"), FLinearColor(0.240f, 0.235f, 0.225f), 0.9f, 0.f },
		{ TEXT("LampMetal"), FLinearColor(0.200f, 0.100f, 0.120f), 0.45f, 0.f },
		{ TEXT("Bulb"), FLinearColor(0.200f, 0.200f, 0.190f), 0.2f, 0.f },
		{ TEXT("HouseWalls"), FLinearColor(0.240f, 0.200f, 0.130f), 0.85f, 0.f },
		{ TEXT("HouseRoof"), FLinearColor(0.100f, 0.025f, 0.035f), 0.8f, 0.f },
		{ TEXT("HouseTrim"), FLinearColor(0.260f, 0.250f, 0.235f), 0.7f, 0.f },
		// Her school books, by subject: maths blue, literature red, biology green, history brown,
		// English purple. No titles: the colours are what she knew them by.
		{ TEXT("Maths"), FLinearColor(0.025f, 0.050f, 0.120f), 0.6f, 0.f },
		{ TEXT("Literature"), FLinearColor(0.120f, 0.025f, 0.025f), 0.6f, 0.f },
		{ TEXT("Biology"), FLinearColor(0.035f, 0.085f, 0.040f), 0.6f, 0.f },
		{ TEXT("History"), FLinearColor(0.085f, 0.050f, 0.025f), 0.6f, 0.f },
		{ TEXT("English"), FLinearColor(0.070f, 0.035f, 0.100f), 0.6f, 0.f },
		// Novels, paperbacks.
		{ TEXT("NovelA"), FLinearColor(0.018f, 0.018f, 0.026f), 0.5f, 0.f },
		{ TEXT("NovelB"), FLinearColor(0.120f, 0.050f, 0.075f), 0.5f, 0.f },
		{ TEXT("NovelC"), FLinearColor(0.040f, 0.080f, 0.110f), 0.5f, 0.f },
		{ TEXT("NovelD"), FLinearColor(0.140f, 0.110f, 0.040f), 0.5f, 0.f },
		{ TEXT("NovelE"), FLinearColor(0.060f, 0.100f, 0.090f), 0.5f, 0.f },
		// Pencils, crayons, stickers, notes and blocks.
		{ TEXT("Red"), FLinearColor(0.160f, 0.025f, 0.020f), 0.6f, 0.f },
		{ TEXT("Orange"), FLinearColor(0.190f, 0.075f, 0.015f), 0.6f, 0.f },
		{ TEXT("Yellow"), FLinearColor(0.210f, 0.160f, 0.020f), 0.6f, 0.f },
		{ TEXT("Green"), FLinearColor(0.035f, 0.100f, 0.035f), 0.6f, 0.f },
		{ TEXT("Blue"), FLinearColor(0.025f, 0.060f, 0.150f), 0.6f, 0.f },
		{ TEXT("Purple"), FLinearColor(0.080f, 0.035f, 0.120f), 0.6f, 0.f },
		{ TEXT("Pink"), FLinearColor(0.220f, 0.070f, 0.110f), 0.6f, 0.f },
		{ TEXT("NoteYellow"), FLinearColor(0.260f, 0.220f, 0.070f), 0.8f, 0.f },
		{ TEXT("NotePink"), FLinearColor(0.260f, 0.110f, 0.160f), 0.8f, 0.f },
		{ TEXT("Juice"), FLinearColor(0.200f, 0.090f, 0.020f), 0.5f, 0.f },
		{ TEXT("Card"), FLinearColor(0.240f, 0.120f, 0.150f), 0.8f, 0.f },
		{ TEXT("Bead"), FLinearColor(0.240f, 0.200f, 0.220f), 0.3f, 0.f },
	};
	for (const FFlat& Flat : FlatList)
	{
		Flats.Add(FName(Flat.Name), Build.Flat(Flat.Color, Flat.Roughness, Flat.Metallic));
	}
}

UMaterialInterface* ANurseryActor::F(const TCHAR* Name) const
{
	const TObjectPtr<UMaterialInstanceDynamic>* Found = Flats.Find(FName(Name));
	return Found ? Found->Get() : MatShadow.Get();
}

void ANurseryActor::Dress(UStaticMeshComponent* Mesh, const TMap<FName, UMaterialInterface*>& Slots) const
{
	if (!Mesh)
	{
		return;
	}
	for (const TPair<FName, UMaterialInterface*>& Slot : Slots)
	{
		if (Mesh->GetMaterialIndex(Slot.Key) != INDEX_NONE)
		{
			Mesh->SetMaterialByName(Slot.Key, Slot.Value);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers, the kitchen's: U runs along a wall (X on north/south, Y on east/west), Z is
// height, and a wall's normal points into the room.
// ---------------------------------------------------------------------------------------------

float ANurseryActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector ANurseryActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector ANurseryActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

float ANurseryActor::FacingYaw(EWall Wall)
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

bool ANurseryActor::IsOnOpening(EWall Wall, float U, float Z, float HalfU, float HalfZ) const
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

TArray<FBox2D> ANurseryActor::CutAround(EWall Wall, float U0, float U1, float Z0, float Z1) const
{
	TArray<FBox2D> Holes;
	for (const FOpening& Opening : Openings)
	{
		if (Opening.Wall == Wall)
		{
			Holes.Add(FBox2D(FVector2D(Opening.CenterU - Opening.HalfU, Opening.BottomZ), FVector2D(Opening.CenterU + Opening.HalfU, Opening.TopZ)));
		}
	}
	return RoomWalls::CutAround(Holes, U0, U1, Z0, Z1);
}

void ANurseryActor::WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud)
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

void ANurseryActor::WallBox(FRoomBuilder& Build, EWall Wall, float U, float Z, float SizeU, float SizeZ, float Depth, float ProudBase, UMaterialInterface* Mat)
{
	const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
	Build.Box(WallPoint(Wall, U, Z, ProudBase + Depth * 0.5f), FRotator::ZeroRotator,
		bAlongX ? FVector(SizeU, Depth, SizeZ) : FVector(Depth, SizeU, SizeZ), Mat, false);
}

void ANurseryActor::AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const
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

bool ANurseryActor::IsFloorSpotClear(float X, float Y, float Radius) const
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

UStaticMeshComponent* ANurseryActor::Drawing(FRoomBuilder& Build, int32 Index, const FVector& Centre, const FVector& Normal, const FVector& Up, float Scale)
{
	// One instance per cell of the atlas, made straight from the asset and never registered with
	// the builder: Add() would otherwise re-tile it to the size of the sheet, and the whole point is
	// that one sheet shows exactly one drawing.
	if (DrawingMats.Num() == 0)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_child_drawings.MI_child_drawings"));
		for (int32 i = 0; i < 8; ++i)
		{
			UMaterialInstanceDynamic* Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
			if (Mat)
			{
				for (const TCHAR* Parameter : { TEXT("BaseColorMap"), TEXT("NormalMap"), TEXT("ARMMap") })
				{
					UTexture* Texture = nullptr;
					if (Parent->GetTextureParameterValue(FMaterialParameterInfo(Parameter), Texture) && Texture)
					{
						Mat->SetTextureParameterValue(Parameter, Texture);
					}
				}
				// Cartridge paper at about an eighth. First at a fifth, which is honest for paper and
				// under a lantern at arm's length burned every drawing out to a blank white sheet.
				Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.150f, 0.140f, 0.130f));
				Mat->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
				Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(0.25f, DrawingPageV, 0.f, 1.f));
				Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor((i % 4) * 0.25f, (i / 4) * 0.5f, 0.f, 1.f));
			}
			DrawingMats.Add(Mat);
		}
	}
	UMaterialInterface* Mat = DrawingMats.IsValidIndex(Index) && DrawingMats[Index] ? DrawingMats[Index].Get() : MatPaper.Get();

	// Local Z out of the sheet, local X to the viewer's right, so local -Y is the top of the page.
	const FVector Right = FVector::CrossProduct(Normal, Up).GetSafeNormal();
	const FRotator Facing = FRotationMatrix::MakeFromZX(Normal, Right).Rotator();
	UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Facing, FVector(21.f * Scale, 29.7f * Scale, 1.f), Mat, false);
	if (Sheet)
	{
		Sheet->SetReceivesDecals(false);
		Sheet->SetCastShadow(false);
	}
	return Sheet;
}

void ANurseryActor::WallDrawing(FRoomBuilder& Build, EWall Wall, float U, float Z, int32 Index, float Tilt, bool bTapeBottom)
{
	const FVector N = WallNormal(Wall);
	const FVector Up0 = FVector::UpVector;
	const FVector Right0 = FVector::CrossProduct(N, Up0);
	const float T = FMath::DegreesToRadians(Tilt);
	const FVector Up = Up0 * FMath::Cos(T) + Right0 * FMath::Sin(T);
	const FVector Right = FVector::CrossProduct(N, Up);
	// In front of the paper: a Mark stands 0.4 to 0.8 proud of where it is put, and the wallpaper is
	// put 0.3 out, so its face is at 1.1. At 0.35 every drawing was behind it.
	const FVector Centre = WallPoint(Wall, U, Z, PaperProud);
	Drawing(Build, Index, Centre, N, Up);

	// Tape over the corners, turned across them, a little yellow, and gone brittle: one corner has
	// lost its tape and curled forward off the wall.
	auto Tape = [&](float SideU, float SideV, float Angle)
	{
		const FVector Corner = Centre + Right * (SideU * 9.6f) + Up * (SideV * 13.9f) + N * 0.12f;
		const float A = FMath::DegreesToRadians(Angle);
		const FVector Along = Right * FMath::Cos(A) + Up * FMath::Sin(A);
		if (UStaticMeshComponent* Strip = Build.Add(FRoomShapes::Plane(), Corner, FRotationMatrix::MakeFromZX(N, Along).Rotator(), FVector(5.8f, 2.1f, 1.f), MatTape, false))
		{
			Strip->SetReceivesDecals(false);
			Strip->SetCastShadow(false);
		}
	};
	Tape(-1.f, 1.f, -38.f + Random.FRandRange(-8.f, 8.f));
	Tape(1.f, 1.f, 38.f + Random.FRandRange(-8.f, 8.f));
	if (bTapeBottom)
	{
		Tape(-1.f, -1.f, 40.f + Random.FRandRange(-8.f, 8.f));
	}
}

void ANurseryActor::Book(FRoomBuilder& Build, const FVector& Base, float Yaw, const FVector& Size, UMaterialInterface* Cover) const
{
	// Two boards, a spine and the block of leaves between, the leaves showing on three edges: one
	// box wrapped round them is a brick (the bedroom's ledgers note).
	const FRotator Turn(0.f, Yaw, 0.f);
	const float Board = FMath::Min(0.22f, Size.Z * 0.2f);
	Build.Box(Base + Turn.RotateVector(FVector(0.f, 0.f, Board * 0.5f)), Turn, FVector(Size.X, Size.Y, Board), Cover, false);
	Build.Box(Base + Turn.RotateVector(FVector(0.f, 0.f, Size.Z - Board * 0.5f)), Turn, FVector(Size.X, Size.Y, Board), Cover, false);
	Build.Box(Base + Turn.RotateVector(FVector(0.f, -Size.Y * 0.5f + 0.15f, Size.Z * 0.5f)), Turn, FVector(Size.X, 0.3f, Size.Z), Cover, false);
	Build.Box(Base + Turn.RotateVector(FVector(0.f, 0.15f, Size.Z * 0.5f)), Turn, FVector(Size.X - 0.6f, Size.Y - 0.6f, Size.Z - Board * 2.f), MatPageEdge, false);
}

void ANurseryActor::StandingBook(FRoomBuilder& Build, const FVector& Foot, const FVector& Out, float Lean, const FVector& Size, UMaterialInterface* Cover) const
{
	// Size is (thickness along the shelf, depth into it, height). Local Y is Out, so the spine is
	// on +Y; a lean turns it about Out, over towards its neighbour.
	const FQuat Q = FRotationMatrix::MakeFromYZ(Out, FVector::UpVector).ToQuat() * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Lean));
	const FRotator R = Q.Rotator();
	auto At = [&](const FVector& Local) { return Foot + Q.RotateVector(Local); };
	const float Board = FMath::Min(0.22f, Size.X * 0.2f);
	Build.Box(At(FVector(-Size.X * 0.5f + Board * 0.5f, 0.f, Size.Z * 0.5f)), R, FVector(Board, Size.Y, Size.Z), Cover, false);
	Build.Box(At(FVector(Size.X * 0.5f - Board * 0.5f, 0.f, Size.Z * 0.5f)), R, FVector(Board, Size.Y, Size.Z), Cover, false);
	Build.Box(At(FVector(0.f, Size.Y * 0.5f - 0.15f, Size.Z * 0.5f)), R, FVector(Size.X, 0.3f, Size.Z), Cover, false);
	Build.Box(At(FVector(0.f, -0.15f, Size.Z * 0.5f)), R, FVector(Size.X - Board * 2.f, Size.Y - 0.6f, Size.Z - 0.6f), MatPageEdge, false);
}

void ANurseryActor::Stroke(FRoomBuilder& Build, const FTransform& Sheet, float X, float Y, float Length, float Weight, UMaterialInterface* Ink) const
{
	// A plane in the paper's frame and no shadow: a box stands up off the page and throws its own
	// shadow, and a page of them reads as a grille (the kitchen recipe book's note).
	if (UStaticMeshComponent* Line = Build.Add(FRoomShapes::Plane(), Sheet.TransformPosition(FVector(X, Y, 0.f)), Sheet.Rotator(),
		FVector(Length, Weight, 1.f), Ink, false))
	{
		Line->SetCastShadow(false);
		Line->SetReceivesDecals(false);
	}
}

void ANurseryActor::Writing(FRoomBuilder& Build, const FTransform& Sheet, float Width, float Height, int32 Lines, int32 Seed, UMaterialInterface* Ink) const
{
	FRandomStream Hand(Seed);
	const float Pitch = Height / FMath::Max(1, Lines);
	for (int32 l = 0; l < Lines; ++l)
	{
		const bool bLast = Hand.FRand() < 0.24f || l == Lines - 1;
		const float Y = -Height * 0.5f + Pitch * (l + 0.5f);
		float Cursor = -Width * 0.5f + (l > 0 && Hand.FRand() < 0.2f ? Width * 0.08f : 0.f);
		const float End = -Width * 0.5f + Width * (bLast ? Hand.FRandRange(0.3f, 0.65f) : Hand.FRandRange(0.9f, 1.f));
		while (Cursor < End - 0.6f)
		{
			const float Length = FMath::Min(Hand.FRandRange(0.7f, 2.4f), End - Cursor);
			Stroke(Build, Sheet, Cursor + Length * 0.5f, Y + Hand.FRandRange(-0.06f, 0.06f), Length, Hand.FRandRange(0.22f, 0.30f), Ink);
			Cursor += Length + Hand.FRandRange(0.4f, 0.6f);
		}
	}
}

void ANurseryActor::Pencil(FRoomBuilder& Build, const FVector& Base, float Yaw, float Length, float Diameter, UMaterialInterface* Mat) const
{
	// Lying along Yaw: the painted body, the sharpened wood, and the point.
	const FRotator Lay(90.f, Yaw, 0.f);
	const FVector Along = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(1.f, 0.f, 0.f));
	const FVector Mid = Base + FVector(0.f, 0.f, Diameter * 0.5f);
	Build.Cyl(Mid, Lay, FVector(Diameter, Diameter, Length - 1.8f), Mat, false);
	Build.Cyl(Mid + Along * (Length * 0.5f - 0.5f), Lay, FVector(Diameter * 0.62f, Diameter * 0.62f, 1.4f), MatBareWood, false);
	Build.Cyl(Mid + Along * (Length * 0.5f + 0.35f), Lay, FVector(Diameter * 0.26f, Diameter * 0.26f, 0.6f), Mat, false);
}

void ANurseryActor::Dust(FRoomBuilder& Build, const FVector& Centre, const FVector2D& Size, float Opacity) const
{
	// From four centimetres over the top, aimed down: a decal reaches nine either way, and one
	// started higher never touches the surface it was meant for (the 09-29 floor-stain note).
	Build.Stain(RoomSurfaces::Damp, Centre + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, 0.f), Size,
		FLinearColor(0.42f, 0.40f, 0.36f), Opacity, 1.35f);
}

// ---------------------------------------------------------------------------------------------

void ANurseryActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float Z0 = -20.f;
	const float Z1 = RoomHeight + 10.f;

	// Solid, colliding and never seen. The north wall is the corridor's south wall, already built
	// to the corridor's height; only the strip over that, up to this room's taller ceiling, is ours.
	Build.Box(FVector(WestX() - T * 0.5f, (NorthY() - T + SouthY() + T) * 0.5f, (Z0 + Z1) * 0.5f), FRotator::ZeroRotator,
		FVector(T, RoomDepth + T * 2.f, Z1 - Z0), MatShell);
	Build.Box(FVector(MidX(), SouthY() + T * 0.5f, (Z0 + Z1) * 0.5f), FRotator::ZeroRotator,
		FVector(RoomWidth + T * 2.f, T, Z1 - Z0), MatShell);
	for (const FBox2D& Piece : CutAround(EWall::East, NorthY(), SouthY() + T, Z0, Z1))
	{
		const FVector2D C = Piece.GetCenter();
		const FVector2D S = Piece.GetSize();
		Build.Box(FVector(EastX() + T * 0.5f, C.X, C.Y), FRotator::ZeroRotator, FVector(T, S.X, S.Y), MatShell);
	}
	Build.Box(FVector(MidX(), NorthY() - T * 0.5f, (300.f + Z1) * 0.5f), FRotator::ZeroRotator, FVector(RoomWidth + T * 2.f, T, Z1 - 300.f), MatShell);

	// The ceiling, and under the boards a dark subfloor that only ever shows through their gaps.
	Build.Box(FVector(MidX(), MidY(), RoomHeight + 5.f), FRotator::ZeroRotator, FVector(RoomWidth + T * 2.f, RoomDepth + T * 2.f, 10.f), MatShell);
	Build.Box(FVector(MidX(), MidY(), -6.f), FRotator::ZeroRotator, FVector(RoomWidth, RoomDepth, 6.f), MatShell);
}

void ANurseryActor::BuildFloor(FRoomBuilder& Build)
{
	// Dark boards running from the door into the room, each row laid from random lengths so no two
	// joints line up; a few lifted, one in thirty gone. Tops at about half a centimetre, so the
	// furniture stands on them at zero.
	const float BoardWidth = 19.f;
	const int32 Rows = FMath::CeilToInt(RoomWidth / BoardWidth);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const float X0 = WestX() + BoardWidth * Row;
		const float W = FMath::Min(BoardWidth, EastX() - X0);
		const float X = X0 + W * 0.5f;
		float Cursor = NorthY();
		while (Cursor < SouthY() - 1.f)
		{
			const float Length = FMath::Min(Random.FRandRange(130.f, 380.f), SouthY() - Cursor);
			const bool bGone = Random.FRand() < 0.03f;
			const float Lift = Random.FRandRange(-0.25f, 0.35f);
			const FRotator Warp(Random.FRandRange(-0.25f, 0.25f), 0.f, Random.FRandRange(-0.6f, 0.6f));
			const bool bWorn = Random.FRand() < 0.3f;
			if (!bGone && Length > 8.f)
			{
				Build.Box(FVector(X, Cursor + Length * 0.5f, -2.5f + Lift), FRotator(Warp.Pitch, 90.f, Warp.Roll),
					FVector(Length - 1.2f, W - 1.4f, 6.f), bWorn ? MatFloorboardsWorn.Get() : MatFloorboards.Get());
			}
			Cursor += Length;
		}
	}

	// Dust along the walls, where it gathers. Aimed down, the first size runs along world Y: roll 90
	// along the north and south walls, none along the east and west (the 09-29 note).
	const FLinearColor DustTint(0.42f, 0.40f, 0.36f);
	for (int32 i = 0; i < 16; ++i)
	{
		const int32 Side = i % 4;
		FVector2D Spot;
		float Roll = 0.f;
		switch (Side)
		{
		case 0: Spot = FVector2D(Random.FRandRange(WestX() + 40.f, EastX() - 40.f), NorthY() + Random.FRandRange(20.f, 30.f)); Roll = 90.f; break;
		case 1: Spot = FVector2D(Random.FRandRange(WestX() + 40.f, EastX() - 40.f), SouthY() - Random.FRandRange(20.f, 30.f)); Roll = 90.f; break;
		case 2: Spot = FVector2D(EastX() - Random.FRandRange(20.f, 30.f), Random.FRandRange(NorthY() + 40.f, SouthY() - 40.f)); break;
		default: Spot = FVector2D(WestX() + Random.FRandRange(20.f, 30.f), Random.FRandRange(NorthY() + 40.f, SouthY() - 40.f)); break;
		}
		// Kept short of the skirting: across the wall's foot a pale dust decal lands on the board's
		// face as white splashes.
		Build.Stain(RoomSurfaces::Damp, FVector(Spot.X, Spot.Y, 4.f), FRotator(-90.f, 0.f, Roll + Random.FRandRange(-6.f, 6.f)),
			FVector2D(Random.FRandRange(60.f, 140.f), Random.FRandRange(18.f, 30.f)), DustTint, Random.FRandRange(0.22f, 0.36f), 1.35f);
	}
	// A whole floor's dust, thinner, and a darker trodden way from the door to the bed and the desk:
	// somebody walked it every day.
	for (int32 i = 0; i < 6; ++i)
	{
		Build.Stain(RoomSurfaces::Damp, FVector(Random.FRandRange(WestX() + 120.f, EastX() - 120.f), Random.FRandRange(NorthY() + 100.f, SouthY() - 100.f), 4.f),
			FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)), FVector2D(Random.FRandRange(150.f, 240.f), Random.FRandRange(120.f, 200.f)),
			DustTint, Random.FRandRange(0.15f, 0.24f), 1.4f);
	}
	// The window has been letting the rain in at the sill for years: the boards under it are darker
	// and lifted.
	Build.Stain(RoomSurfaces::Damp, FVector(EastX() - 40.f, WindowY(), 4.f), FRotator(-90.f, 0.f, 0.f), FVector2D(150.f, 80.f),
		FLinearColor(0.16f, 0.12f, 0.09f), 0.65f, 1.25f, 0.6f);
}

void ANurseryActor::BuildWalls(FRoomBuilder& Build)
{
	const float H = RoomHeight;

	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Walls[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), SouthY() },
	};

	for (const FRun& R : Walls)
	{
		// Plaster everywhere, and the paper over it from the skirting to the picture rail. One sheet
		// a wall rather than strips: a papered room reads as papered by its pattern carrying on, and
		// what it has lost it has lost in patches (the decals in BuildDamage), not in stripes.
		WallFill(Build, R.Wall, R.U0, R.U1, 0.f, H, MatPlaster);
		WallFill(Build, R.Wall, R.U0, R.U1, Skirting, PictureRail, MatWallpaper, 0.3f);

		for (const FBox2D& Piece : CutAround(R.Wall, R.U0, R.U1, 0.f, H))
		{
			const FVector2D C = Piece.GetCenter();
			const FVector2D S = Piece.GetSize();
			if (S.X < 1.f)
			{
				continue;
			}
			if (Piece.Min.Y <= 1.f)
			{
				WallBox(Build, R.Wall, C.X, Skirting * 0.5f, S.X, Skirting, 1.8f, 0.f, MatTrim);
				WallBox(Build, R.Wall, C.X, Skirting - 1.f, S.X, 2.f, 2.6f, 0.f, MatTrim);
			}
			if (Piece.Max.Y >= H - 1.f)
			{
				WallBox(Build, R.Wall, C.X, H - 7.f, S.X, 14.f, 9.f, 0.f, MatTrim);
				WallBox(Build, R.Wall, C.X, H - 17.f, S.X, 6.f, 4.f, 0.f, MatTrim);
			}
		}
		for (const FBox2D& Piece : CutAround(R.Wall, R.U0, R.U1, PictureRail - 2.f, PictureRail + 2.f))
		{
			WallBox(Build, R.Wall, Piece.GetCenter().X, PictureRail, Piece.GetSize().X, 4.f, 2.4f, 0.f, MatTrim);
		}
	}

	// The door from the corridor, cased on this side. Nothing stands in the arc it swings through.
	Footprints.Add(FBox2D(FVector2D(Setup.DoorX - Setup.DoorHalf - 24.f, NorthY()), FVector2D(Setup.DoorX + Setup.DoorHalf + 24.f, NorthY() + Setup.DoorHalf * 2.f + 20.f)));
	{
		const float U = Setup.DoorX;
		const float Top = Setup.DoorHeight;
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, EWall::North, U + S * (Setup.DoorHalf + 5.5f), (Top + 11.f) * 0.5f, 11.f, Top + 11.f, 2.6f, 0.f, MatTrim);
			WallBox(Build, EWall::North, U + S * (Setup.DoorHalf + 6.f), 12.f, 13.f, 24.f, 3.6f, 0.f, MatTrim);
		}
		WallBox(Build, EWall::North, U, Top + 5.5f, Setup.DoorHalf * 2.f + 22.f, 11.f, 2.6f, 0.f, MatTrim);
		WallBox(Build, EWall::North, U, Top + 13.f, Setup.DoorHalf * 2.f + 30.f, 4.f, 4.2f, 0.f, MatTrim);
	}

	// The window's architrave. The sill and its apron are the window's own (AStormWindowActor).
	{
		const float U = WindowY();
		for (const float S : { -1.f, 1.f })
		{
			WallBox(Build, EWall::East, U + S * (WindowWidth * 0.5f + 6.f), (WindowSill + WindowTop + 12.f) * 0.5f, 12.f, WindowTop + 12.f - WindowSill, 2.6f, 0.f, MatTrim);
		}
		WallBox(Build, EWall::East, U, WindowTop + 6.f, WindowWidth + 24.f, 12.f, 2.6f, 0.f, MatTrim);
	}

	// Paper coming away at the corners, as the brief has it: the top of a strip has let go and
	// hangs off the wall, a generated sheet so its torn edge is a torn edge (the corridor's peel).
	// Behind each one the plaster shows, ragged, where it came from.
	struct FPeel { EWall Wall; float U; float Z; float Width; float Drop; float Turn; };
	const FPeel Peels[] = {
		{ EWall::North, WestX() + 34.f, 214.f, 48.f, 78.f, 6.f },
		{ EWall::North, EastX() - 30.f, 226.f, 44.f, 62.f, -8.f },
		{ EWall::South, WestX() + 30.f, 206.f, 50.f, 88.f, -5.f },
		{ EWall::South, EastX() - 40.f, 220.f, 42.f, 58.f, 7.f },
	};
	int32 Seed = 6101;
	for (const FPeel& P : Peels)
	{
		const FVector Normal = WallNormal(P.Wall);
		const FVector Base = WallPoint(P.Wall, P.U, P.Z, 0.f);
		Build.Cloth(Base + Normal * 7.f, FRotator(0.f, 0.f, (P.Wall == EWall::North ? -90.f : 90.f) + P.Turn),
			FVector2D(P.Width, P.Drop), 3.5f, 0.f, Seed++, MatWallpaperPeel, RoomSurfaces::NurseryWallpaper.TexelSizeCm);
		FVector Location;
		FRotator Rotation;
		AimAt(P.Wall, P.U, P.Z + 6.f, 0.f, Location, Rotation);
		Build.Stain(RoomSurfaces::Plaster, Location, Rotation, FVector2D(P.Width + 18.f, P.Drop * 0.8f + 30.f), FLinearColor(0.150f, 0.142f, 0.130f), 1.f, 0.85f);
	}
}

void ANurseryActor::BuildCeiling(FRoomBuilder& Build)
{
	const float C = RoomHeight;
	Build.Mark(FVector(MidX(), MidY(), C), FRotator(0.f, 0.f, 180.f), FVector2D(RoomWidth, RoomDepth), MatCeiling);

	// A ceiling rose, and the light that hung from it: a fabric shade, pink once, its frame showing
	// through where the silk has rotted. Nothing in it works and nothing in it glows.
	const FVector Rose(MidX() - 10.f, MidY(), C);
	Build.Cyl(Rose - FVector(0.f, 0.f, 1.5f), FRotator::ZeroRotator, FVector(46.f, 46.f, 3.f), MatTrim, false);
	Build.Cyl(Rose - FVector(0.f, 0.f, 4.f), FRotator::ZeroRotator, FVector(26.f, 26.f, 3.f), MatTrim, false);
	Build.Cyl(Rose - FVector(0.f, 0.f, 30.f), FRotator::ZeroRotator, FVector(0.8f, 0.8f, 50.f), F(TEXT("Strap")), false);
	// Open at both ends, as a lampshade is: the profile is a closed loop round the cloth's section.
	TArray<FVector2D> Shade;
	Shade.Add(FVector2D(19.f, 0.f));
	Shade.Add(FVector2D(18.6f, 1.6f));
	Shade.Add(FVector2D(12.2f, 16.8f));
	Shade.Add(FVector2D(10.8f, 18.f));
	Shade.Add(FVector2D(10.2f, 16.8f));
	Shade.Add(FVector2D(17.6f, 0.8f));
	Shade.Add(FVector2D(19.f, 0.f));
	UMaterialInstanceDynamic* ShadeMat = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.42f, 0.20f, 0.17f), 1.2f);
	NurserySheet(ShadeMat);
	Build.Lathe(Rose - FVector(0.f, 0.f, 74.f), FRotator(0.f, 0.f, 3.f), Shade, 28, ShadeMat, 34.f);

	// Water through the ceiling, heaviest along the outside wall over the window.
	for (int32 i = 0; i < 7; ++i)
	{
		const float X = (i < 3) ? EastX() - Random.FRandRange(40.f, 200.f) : Random.FRandRange(WestX() + 60.f, EastX() - 60.f);
		Build.Stain(RoomSurfaces::Damp, FVector(X, Random.FRandRange(NorthY() + 60.f, SouthY() - 60.f), C - 2.f), FRotator(90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(100.f, 220.f), Random.FRandRange(80.f, 170.f)), FLinearColor(0.32f, 0.27f, 0.21f), Random.FRandRange(0.35f, 0.6f), 1.2f);
	}
	Build.Crack(FVector(MidX() + 80.f, MidY() - 60.f, C - 2.f), FRotator(90.f, 0.f, 30.f), FVector2D(240.f, 140.f), 0.75f, 18.f);

	// Cobwebs in the four upper corners, planes turned across each, and along the cornice.
	const FVector2D Corners[4] = { { WestX(), NorthY() }, { EastX(), NorthY() }, { WestX(), SouthY() }, { EastX(), SouthY() } };
	for (const FVector2D& Corner : Corners)
	{
		const float SX = Corner.X < MidX() ? 1.f : -1.f;
		const float SY = Corner.Y < MidY() ? 1.f : -1.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Inset = 22.f + i * 18.f;
			Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SX * Inset, Corner.Y + SY * Inset, C - 26.f - i * 7.f),
				FRotator(0.f, SX * SY > 0.f ? 45.f : -45.f, 180.f), FVector(Inset * 1.6f, Inset * 1.6f, 1.f), MatWeb, false);
		}
	}
	// And one slung from the shade's frame to its flex.
	Build.Add(FRoomShapes::Plane(), Rose - FVector(0.f, 0.f, 50.f), FRotator(0.f, 30.f, 90.f), FVector(24.f, 34.f, 1.f), MatWeb, false);
}

void ANurseryActor::BuildDamage(FRoomBuilder& Build)
{
	const float H = RoomHeight;
	const FLinearColor DampTint(0.46f, 0.38f, 0.30f);
	// Mould is a dark warm bloom, never a green one (the house's rule, and the reason for it: under
	// a cold window a green stain reads as paint).
	const FLinearColor MouldTint(0.20f, 0.19f, 0.13f);
	auto Stain = [&](EWall Wall, float U, float Z, float SU, float SZ, const FRoomSurface& Set, const FLinearColor& Tint, float Opacity, float Roll, float Edge)
	{
		FVector Location;
		FRotator Rotation;
		AimAt(Wall, U, Z, Roll, Location, Rotation);
		Build.Stain(Set, Location, Rotation, FVector2D(SU, SZ), Tint, Opacity, Edge);
	};

	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Walls[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), SouthY() },
	};
	for (const FRun& R : Walls)
	{
		// Water down from the ceiling line, the brief's stains near the ceiling.
		for (int32 i = 0; i < 4; ++i)
		{
			const float Drop = Random.FRandRange(50.f, 120.f);
			Stain(R.Wall, Random.FRandRange(R.U0, R.U1), H - Drop * 0.38f, Random.FRandRange(90.f, 200.f), Drop,
				RoomSurfaces::Damp, DampTint * 0.75f, Random.FRandRange(0.4f, 0.6f), 0.f, 1.25f);
		}
		// Faint blooms across the paper: the paper is faded, not ruined.
		for (int32 i = 0; i < 5; ++i)
		{
			Stain(R.Wall, Random.FRandRange(R.U0, R.U1), Random.FRandRange(40.f, H - 40.f), Random.FRandRange(90.f, 200.f), Random.FRandRange(80.f, 180.f),
				RoomSurfaces::Damp, DampTint * 0.85f, Random.FRandRange(0.10f, 0.2f), Random.FRandRange(0.f, 360.f), 1.15f);
		}
		// A few cracks, high, where the house has moved.
		for (int32 i = 0; i < 2; ++i)
		{
			const float U = Random.FRandRange(R.U0, R.U1);
			const float Z = Random.FRandRange(PictureRail - 40.f, H - 30.f);
			if (!IsOnOpening(R.Wall, U, Z, 50.f, 50.f))
			{
				FVector Location;
				FRotator Rotation;
				AimAt(R.Wall, U, Z, 0.f, Location, Rotation);
				Build.Crack(Location, Rotation, FVector2D(Random.FRandRange(80.f, 160.f), Random.FRandRange(90.f, 180.f)), Random.FRandRange(0.6f, 0.9f), Random.FRandRange(16.f, 26.f));
			}
		}
		// Paper gone in the corners, low and high, the plaster showing ragged through it.
		for (const float Corner : { R.U0, R.U1 })
		{
			const float U = Corner + (Corner == R.U0 ? 1.f : -1.f) * Random.FRandRange(14.f, 30.f);
			Stain(R.Wall, U, Random.FRandRange(30.f, 60.f), Random.FRandRange(30.f, 60.f), Random.FRandRange(40.f, 80.f),
				RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f), 1.f, Random.FRandRange(0.f, 360.f), 0.8f);
			Stain(R.Wall, U, H - 30.f, Random.FRandRange(50.f, 100.f), Random.FRandRange(50.f, 90.f), RoomSurfaces::Damp, MouldTint, 0.55f, 0.f, 1.1f);
		}
	}

	// The mould round the window, where the rain has been coming in round the frame. Patches, not a
	// border: the brief's "small patches around the windows".
	for (int32 i = 0; i < 6; ++i)
	{
		const float Side = (i % 2 == 0) ? -1.f : 1.f;
		const float U = WindowY() + Side * (WindowWidth * 0.5f + Random.FRandRange(10.f, 26.f));
		Stain(EWall::East, U, Random.FRandRange(WindowSill - 20.f, WindowTop), Random.FRandRange(18.f, 40.f), Random.FRandRange(26.f, 70.f),
			RoomSurfaces::Damp, MouldTint, Random.FRandRange(0.6f, 0.85f), 0.f, 1.1f);
	}
	Stain(EWall::East, WindowY(), WindowTop + 26.f, WindowWidth + 60.f, 40.f, RoomSurfaces::Damp, MouldTint * 1.2f, 0.5f, 0.f, 1.2f);
	Stain(EWall::East, WindowY() + 40.f, WindowSill - 34.f, 80.f, 46.f, RoomSurfaces::Damp, DampTint * 0.6f, 0.6f, 0.f, 1.2f);
}

void ANurseryActor::BuildBed(FRoomBuilder& Build)
{
	// A small white bed, head to the south wall, the foot towards the door: the first thing the
	// player sees past the open door is the end of it and what is lying on it.
	const FVector Centre(BedX(), SouthY() - BedLength * 0.5f - 1.5f, 0.f);
	const FRotator Turn(0.f, 180.f, 0.f);
	Dress(Build.Prop(RoomProps::NurseryBed, Centre, Turn, 0.f, false),
		{ { TEXT("PaintWhite"), MatWhite }, { TEXT("Wood"), MatBareWood }, { TEXT("Mattress"), MatMattress } });
	NurseryPawnOnly(Build.Box(Centre + FVector(0.f, 0.f, 40.f), FRotator::ZeroRotator, FVector(BedHalfWidth * 2.f, BedLength, 80.f), MatVoid));
	Footprints.Add(FBox2D(FVector2D(Centre.X - BedHalfWidth, Centre.Y - BedLength * 0.5f), FVector2D(Centre.X + BedHalfWidth, Centre.Y + BedLength * 0.5f)));

	// The paint has chipped off the posts and rails where a child's feet and a hoover knocked them.
	auto Chip = [&](const FVector& At, const FVector& Into, const FVector2D& Size)
	{
		Build.Stain(RoomSurfaces::RoughWood, At - Into * 3.f, FRotationMatrix::MakeFromX(Into).Rotator(), Size, FLinearColor(1.1f, 1.0f, 0.95f), 0.95f, 0.8f);
	};
	const float HeadY = SouthY() - 4.f;
	const float FootY = Centre.Y - BedLength * 0.5f + 4.f;
	Chip(FVector(Centre.X - 45.f, FootY - 3.f, 30.f), FVector(0.f, 1.f, 0.f), FVector2D(5.f, 9.f));
	Chip(FVector(Centre.X + 45.f, FootY - 3.f, 66.f), FVector(0.f, 1.f, 0.f), FVector2D(4.f, 6.f));
	Chip(FVector(Centre.X - 12.f, FootY - 3.f, 70.f), FVector(0.f, 1.f, 0.f), FVector2D(10.f, 4.f));
	Chip(FVector(Centre.X - BedHalfWidth - 1.f, Centre.Y + 10.f, 30.f), FVector(1.f, 0.f, 0.f), FVector2D(16.f, 5.f));
	Chip(FVector(Centre.X + BedHalfWidth + 1.f, Centre.Y - 40.f, 28.f), FVector(-1.f, 0.f, 0.f), FVector2D(9.f, 4.f));

	// The bottom sheet, over the mattress and down its sides; the quilt, thrown back from the
	// pillow the way it was got out from under that morning, and its top edge turned down.
	const float Top = MattressTop;
	Build.Cloth(FVector(Centre.X, Centre.Y + 2.f, Top + 1.2f), FRotator::ZeroRotator, FVector2D(104.f, 192.f), 2.f, 14.f, 7101, MatSheet, 34.f, /*bWorn*/ false);
	// Not worn: a quilt this size is cut on a grid several centimetres a step, and its holes and
	// hem came out as square notches (the 09-27 Cloth note). Her quilt is old, not rotten.
	Build.Cloth(FVector(Centre.X - 3.f, Centre.Y - 34.f, Top + 6.f), FRotator(0.f, -2.5f, 0.f), FVector2D(120.f, 136.f), 7.f, 22.f, 7102, MatQuilt, 34.f, /*bWorn*/ false);
	Build.Cloth(FVector(Centre.X - 1.f, Centre.Y + 40.f, Top + 8.5f), FRotator(0.f, 3.f, 0.f), FVector2D(112.f, 22.f), 3.5f, 4.f, 7103, MatQuilt, 34.f, /*bWorn*/ false);

	// One pillow where it belongs, and the teddy sitting against it: still the first thing on the
	// bed, at twelve.
	// The pillowcases are the quilt's print: a set. In plain linen the one on the floor was a grey
	// boulder.
	Dress(Build.Prop(RoomProps::Pillow, FVector(Centre.X - 4.f, HeadY - 26.f, Top + 1.5f), FRotator(0.f, 182.f, 0.f), 0.f, false), { { TEXT("Fabric"), MatQuilt } });
	Dress(Build.Prop(RoomProps::Teddy, FVector(Centre.X + 22.f, HeadY - 46.f, Top + 6.f), FRotator(-4.f, 180.f + 24.f, 0.f), 0.f, false),
		{ { TEXT("Fur"), MatBearFur }, { TEXT("Pad"), MatPad }, { TEXT("Button"), F(TEXT("Button")) }, { TEXT("Nose"), F(TEXT("Nose")) }, { TEXT("Ribbon"), F(TEXT("Ribbon")) } });

	// Her pyjamas, folded at the foot: put away that morning, to be put on again that night.
	Dress(Build.Prop(RoomProps::Pajamas, FVector(Centre.X + 14.f, FootY + 34.f, Top + 9.f), FRotator(0.f, 8.f, 0.f), 0.f, false),
		{ { TEXT("Fabric"), MatPajamas }, { TEXT("Ribbon"), F(TEXT("Ribbon")) } });

	// A novel lying open face down on the quilt, where she stopped.
	{
		const FVector Spine(Centre.X - 24.f, Centre.Y - 6.f, Top + 12.f);
		const float Yaw = 72.f;
		for (const float S : { -1.f, 1.f })
		{
			const FRotator Half(0.f, Yaw, S * 16.f);
			const FVector Out = Half.RotateVector(FVector(0.f, S * 6.8f, -1.6f));
			Build.Box(Spine + Out, Half, FVector(19.f, 13.f, 0.25f), F(TEXT("NovelB")), false);
			Build.Box(Spine + Out + Half.RotateVector(FVector(0.f, 0.f, -0.8f)), Half, FVector(18.4f, 12.4f, 1.3f), MatPageEdge, false);
		}
	}

	// The other pillow, on the floor between the bed and the toy shelf, and her trainers beside the
	// bed, kicked off, one on its side.
	Dress(Build.Prop(RoomProps::Pillow, FVector(Centre.X - BedHalfWidth - 34.f, HeadY - 58.f, 0.5f), FRotator(0.f, 64.f, 6.f), 0.f, false), { { TEXT("Fabric"), MatQuilt } });
	const TMap<FName, UMaterialInterface*> ShoeSlots = { { TEXT("Canvas"), F(TEXT("Canvas")) }, { TEXT("Sole"), F(TEXT("Sole")) }, { TEXT("Lace"), F(TEXT("Lace")) }, { TEXT("Shadow"), MatShadow } };
	Dress(Build.Prop(RoomProps::Sneaker, FVector(Centre.X - BedHalfWidth - 18.f, Centre.Y - 34.f, 0.f), FRotator(0.f, 168.f, 0.f), 0.f, false), ShoeSlots);
	if (UStaticMeshComponent* Right = Build.Prop(RoomProps::Sneaker, FVector(Centre.X - BedHalfWidth - 32.f, Centre.Y - 60.f, 4.2f), FRotator(0.f, 214.f, 82.f), 0.f, false))
	{
		Right->SetRelativeScale3D(Right->GetRelativeScale3D() * FVector(-1.f, 1.f, 1.f));
		Dress(Right, ShoeSlots);
	}

	// Dust over all of it, even and thin, which is what makes it look left rather than slept in.
	Build.Stain(RoomSurfaces::Damp, FVector(Centre.X, Centre.Y, Top + 22.f), FRotator(-90.f, 0.f, 0.f), FVector2D(200.f, 104.f),
		FLinearColor(0.42f, 0.40f, 0.36f), 0.32f, 1.35f);
}

void ANurseryActor::BuildNightstand(FRoomBuilder& Build)
{
	const FVector Seat = NightstandSeat();
	const FRotator Turn(0.f, FacingYaw(EWall::South), 0.f);
	Dress(Build.Prop(RoomProps::NurseryNightstand, Seat, Turn, 0.f, false), { { TEXT("Paint"), MatPink }, { TEXT("Brass"), MatBrass }, { TEXT("Shadow"), MatShadow } });
	NurseryPawnOnly(Build.Box(Seat + FVector(0.f, 0.f, 30.f), FRotator::ZeroRotator, FVector(46.f, 38.f, 60.f), MatVoid));
	Footprints.Add(FBox2D(FVector2D(Seat.X - 24.f, Seat.Y - 20.f), FVector2D(Seat.X + 24.f, Seat.Y + 20.f)));
	const float Top = 60.f;

	// On the top: her alarm clock turned to the pillow, a bracelet of plastic beads, a hair ribbon.
	// The family photograph is a clue (BuildClues).
	if (UStaticMeshComponent* Clock = Build.PropSeated(RoomProps::AlarmClock, Seat + FVector(12.f, 6.f, Top), FRotator(0.f, 90.f + 34.f, 0.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Clock, FLinearColor(0.55f, 0.50f, 0.46f), 0);
		Clock->SetMaterial(1, MatGlass);
	}
	{
		const FVector Ring = Seat + FVector(9.f, -10.f, Top + 0.45f);
		static const TCHAR* Beads[] = { TEXT("Pink"), TEXT("Bead"), TEXT("Purple"), TEXT("Bead"), TEXT("Blue"), TEXT("Bead"), TEXT("Yellow"), TEXT("Bead") };
		for (int32 i = 0; i < 18; ++i)
		{
			const float A = 2.f * PI * i / 18.f;
			Build.Sph(Ring + FVector(FMath::Cos(A) * 3.4f, FMath::Sin(A) * 2.9f, 0.f), 0.9f, F(Beads[i % 8]));
		}
	}
	Dress(Build.Prop(RoomProps::HairBow, Seat + FVector(-14.f, -10.f, Top), FRotator(0.f, 30.f, 0.f), 0.f, false), { { TEXT("Ribbon"), F(TEXT("Ribbon")) } });
	Dust(Build, Seat + FVector(0.f, 0.f, Top), FVector2D(40.f, 48.f), 0.42f);

	// Novels on the shelf under the drawer: three standing, two lying. The books she read in bed.
	const FVector Shelf = Seat + FVector(0.f, 0.f, 11.f);
	const FVector Out(0.f, -1.f, 0.f);
	const TCHAR* Covers[] = { TEXT("NovelA"), TEXT("NovelC"), TEXT("NovelD") };
	float X = -16.f;
	for (int32 i = 0; i < 3; ++i)
	{
		const float Thick = 2.2f + i * 0.4f;
		StandingBook(Build, Shelf + FVector(X + Thick * 0.5f, 0.f, 0.f), Out, i == 2 ? -9.f : 0.f, FVector(Thick, 13.f, 19.5f - i * 0.6f), F(Covers[i]));
		X += Thick + 0.2f;
	}
	Book(Build, Shelf + FVector(8.f, -1.f, 0.f), 3.f, FVector(19.f, 13.f, 2.6f), F(TEXT("NovelE")));
	Book(Build, Shelf + FVector(8.5f, -1.5f, 2.6f), -6.f, FVector(18.f, 12.5f, 2.2f), F(TEXT("NovelB")));
}

void ANurseryActor::BuildWardrobe(FRoomBuilder& Build)
{
	const FVector Seat = WardrobeSeat();
	const FRotator Turn(0.f, FacingYaw(EWall::West), 0.f);
	Dress(Build.Prop(RoomProps::NurseryWardrobe, Seat, Turn, 0.f, false),
		{ { TEXT("Paint"), MatPink }, { TEXT("Inside"), MatPinkInside }, { TEXT("Brass"), MatBrass }, { TEXT("Shadow"), MatShadow } });
	NurseryPawnOnly(Build.Box(Seat + FVector(0.f, 0.f, 95.f), Turn, FVector(104.f, 54.f, 190.f), MatVoid));
	Footprints.Add(FBox2D(FVector2D(Seat.X - 27.f, Seat.Y - 52.f), FVector2D(Seat.X + 70.f, Seat.Y + 52.f)));

	// The right-hand door stands open on her clothes. Its hinge is on the carcass's outside edge;
	// opening is a negative turn in the door's own frame (make_nursery.py).
	const float Open = 42.f;
	Dress(Build.Prop(RoomProps::NurseryWardrobeDoor, Seat + Turn.RotateVector(FVector(48.f, 26.f, 0.f)), FRotator(0.f, Turn.Yaw - Open, 0.f), 0.f, false),
		{ { TEXT("Paint"), MatPink }, { TEXT("Inside"), MatPinkInside }, { TEXT("Brass"), MatBrass } });

	// Three things on hangers on the rail, edge on to the open door: a school cardigan, a dress, a
	// coat. Each a hanging cloth turned upright (pitch 90 lays the cloth's X down the drop).
	USceneComponent* Inside = NurseryPivot(this, RoomRoot, Seat, Turn, TEXT("WardrobeInside"));
	FRoomBuilder W(this, Inside);
	struct FGarment { float X; float Length; float Width; UMaterialInterface* Mat; int32 Seed; };
	const FGarment Garments[] = {
		{ 12.f, 64.f, 40.f, MatGarment, 7201 },
		{ 24.f, 92.f, 38.f, MatQuilt, 7202 },
		{ 36.f, 84.f, 44.f, MatGarmentDark, 7203 },
	};
	for (const FGarment& G : Garments)
	{
		const float Rail = 152.f;
		W.Cloth(FVector(G.X, -3.f, Rail - 6.f - G.Length * 0.5f), FRotator(90.f, 0.f, 0.f), FVector2D(G.Length, G.Width), 1.6f, 0.f, G.Seed, G.Mat, 34.f);
		// The hanger: a hook over the rail and the bar.
		W.Box(FVector(G.X, -3.f, Rail - 5.f), FRotator::ZeroRotator, FVector(0.6f, 38.f, 1.2f), MatBareWood, false);
		W.Cyl(FVector(G.X, -3.f, Rail - 1.2f), FRotator(0.f, 0.f, 90.f), FVector(3.f, 3.f, 0.4f), MatIron, false);
	}
	// Two pairs of shoes on the floor of it.
	W.Box(FVector(28.f, 6.f, 39.f), FRotator(0.f, 8.f, 0.f), FVector(8.f, 20.f, 5.f), F(TEXT("Shoe")), false);
	W.Box(FVector(38.f, 4.f, 39.f), FRotator(0.f, -4.f, 0.f), FVector(8.f, 20.f, 5.f), F(TEXT("Shoe")), false);

	// Chipped paint on the doors' edges and round the knob, where a hand opened them every day.
	auto Chip = [&](const FVector& Local, const FVector2D& Size)
	{
		const FVector Into = Turn.RotateVector(FVector(0.f, -1.f, 0.f));
		Build.Stain(RoomSurfaces::RoughWood, Seat + Turn.RotateVector(Local) - Into * 4.f, FRotationMatrix::MakeFromX(Into).Rotator(), Size,
			FLinearColor(1.1f, 1.0f, 0.95f), 0.95f, 0.8f);
	};
	Chip(FVector(-3.f, 27.f, 108.f), FVector2D(6.f, 12.f));
	Chip(FVector(-30.f, 27.f, 22.f), FVector2D(14.f, 4.f));
	Chip(FVector(30.f, 27.f, 12.f), FVector2D(9.f, 5.f));
	Dust(Build, Seat + FVector(0.f, 0.f, 189.f), FVector2D(110.f, 60.f), 0.5f);
}

void ANurseryActor::BuildDesk(FRoomBuilder& Build)
{
	// Her desk, against the east wall beside the window, so the light falls on it from the left.
	const FVector Seat = DeskSeat();
	const FRotator Turn(0.f, FacingYaw(EWall::East), 0.f);
	Dress(Build.Prop(RoomProps::NurseryDesk, Seat, Turn, 0.f, false), { { TEXT("Paint"), MatPink }, { TEXT("Brass"), MatBrass }, { TEXT("Shadow"), MatShadow } });
	NurseryPawnOnly(Build.Box(Seat + FVector(0.f, 0.f, 40.f), Turn, FVector(112.f, 56.f, 80.f), MatVoid));
	Footprints.Add(FBox2D(FVector2D(Seat.X - 28.f, Seat.Y - 56.f), FVector2D(Seat.X + 28.f, Seat.Y + 56.f)));

	// The chair, pulled out from it and turned a little towards the room: she got up from it.
	const FVector ChairSeat = Seat + Turn.RotateVector(FVector(-14.f, 60.f, 0.f));
	const FRotator ChairTurn(0.f, Turn.Yaw + 180.f + 22.f, 0.f);
	Dress(Build.Prop(RoomProps::NurseryChair, ChairSeat, ChairTurn, 0.f, false), { { TEXT("Paint"), MatPink } });
	NurseryPawnOnly(Build.Box(ChairSeat + FVector(0.f, 0.f, 40.f), ChairTurn, FVector(42.f, 40.f, 80.f), MatVoid));
	Footprints.Add(FBox2D(FVector2D(ChairSeat.X - 26.f, ChairSeat.Y - 26.f), FVector2D(ChairSeat.X + 26.f, ChairSeat.Y + 26.f)));

	// Her school backpack, leaning against the desk's open end, its front to the room.
	{
		const FVector At = Seat + Turn.RotateVector(FVector(-46.f, 44.f, 0.f));
		Dress(Build.PropSeated(RoomProps::Backpack, At, FRotator(0.f, Turn.Yaw + 6.f, 13.f), 0.f, false),
			{ { TEXT("Fabric"), MatBackpack }, { TEXT("Trim"), F(TEXT("Strap")) }, { TEXT("Zip"), F(TEXT("Zip")) }, { TEXT("Charm"), F(TEXT("Charm")) } });
	}

	// Everything on the top is laid out in the desk's own frame: X along it (south, towards the
	// window, is +X), Y out to the room, the top at 75.
	USceneComponent* TopPivot = NurseryPivot(this, RoomRoot, Seat + FVector(0.f, 0.f, DeskTop), Turn, TEXT("DeskTop"));
	FRoomBuilder D(this, TopPivot);

	// The lamp at the back corner, turned in over the work. Not working, and not lit.
	Dress(D.Prop(RoomProps::DeskLamp, FVector(-48.f, -12.f, 0.f), FRotator(0.f, -40.f, 0.f), 0.f, false),
		{ { TEXT("Metal"), F(TEXT("LampMetal")) }, { TEXT("Bulb"), F(TEXT("Bulb")) } });

	// The textbooks, stacked on the right where she pushed them: maths, literature, biology,
	// history, English. Each a different size, as school books are, none quite square on the last.
	{
		struct FText { const TCHAR* Cover; FVector Size; };
		const FText Texts[] = {
			{ TEXT("Maths"), FVector(28.f, 21.f, 2.4f) },
			{ TEXT("Biology"), FVector(27.f, 21.f, 2.0f) },
			{ TEXT("History"), FVector(25.f, 19.f, 2.8f) },
			{ TEXT("Literature"), FVector(24.f, 17.f, 2.2f) },
			{ TEXT("English"), FVector(23.f, 17.f, 1.6f) },
		};
		float Z = 0.f;
		for (const FText& Text : Texts)
		{
			Book(D, FVector(40.f + Random.FRandRange(-1.5f, 1.5f), -14.f + Random.FRandRange(-1.f, 1.f), Z), Random.FRandRange(-7.f, 7.f), Text.Size, F(Text.Cover));
			// A school label on the cover, the one pale thing on it.
			D.Box(FVector(40.f, -14.f, Z + Text.Size.Z + 0.05f), FRotator(0.f, 0.f, 0.f), FVector(7.f, 4.f, 0.1f), MatPaper, false);
			Z += Text.Size.Z;
		}
		// Her headphones, put down on top of the pile.
		Dress(D.PropSeated(RoomProps::Headphones, FVector(40.f, -14.f, Z + 0.1f), FRotator(0.f, 160.f, 90.f), 0.f, false),
			{ { TEXT("Plastic"), F(TEXT("Plastic")) }, { TEXT("Cushion"), F(TEXT("Cushion")) } });
	}

	// The exercise book is her homework, a clue (BuildClues).

	// Pens and pencils, an eraser, a sharpener (Poly Haven's stationery set), at the back.
	if (UStaticMeshComponent* Pens = D.PropSeated(RoomProps::Stationery, FVector(-28.f, -21.f, 0.f), FRotator(0.f, 6.f, 0.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Pens, FLinearColor(0.55f, 0.50f, 0.46f));
	}
	// Her pencil case, and coloured pencils come out of it along the front of the top.
	Dress(D.Prop(RoomProps::PencilCase, FVector(-45.f, 17.f, 0.f), FRotator(0.f, 22.f, 0.f), 0.f, false),
		{ { TEXT("Fabric"), MatBackpack }, { TEXT("Trim"), F(TEXT("Strap")) }, { TEXT("Zip"), F(TEXT("Zip")) } });
	{
		const TCHAR* Colours[] = { TEXT("Red"), TEXT("Orange"), TEXT("Yellow"), TEXT("Green"), TEXT("Blue"), TEXT("Purple"), TEXT("Pink") };
		for (int32 i = 0; i < 7; ++i)
		{
			const float Yaw = 10.f + Random.FRandRange(-26.f, 26.f);
			Pencil(D, FVector(-26.f + i * 2.2f + Random.FRandRange(-1.f, 1.f), 19.f + Random.FRandRange(-1.5f, 5.f), 0.f), Yaw, Random.FRandRange(13.f, 17.5f), 0.75f, F(Colours[i]));
		}
	}

	// Sticky notes: three on the gallery in front of her, one on the lamp's base, two on the top.
	{
		const TCHAR* Notes[] = { TEXT("NoteYellow"), TEXT("NotePink"), TEXT("NoteYellow") };
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector At(-28.f + i * 12.f + Random.FRandRange(-2.f, 2.f), -25.45f, 6.f + Random.FRandRange(-0.6f, 0.6f));
			const FRotator Face = FRotationMatrix::MakeFromZX(FVector(0.f, 1.f, 0.f), FVector(1.f, 0.f, 0.f)).Rotator();
			if (UStaticMeshComponent* Note = D.Add(FRoomShapes::Plane(), At, Face, FVector(7.6f, 7.6f, 1.f), F(Notes[i]), false))
			{
				Note->SetRelativeRotation(FQuat(Face) * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Random.FRandRange(-8.f, 8.f))));
				Note->SetCastShadow(false);
			}
			Writing(D, FTransform(Face, At + FVector(0.f, 0.05f, 0.f)), 5.5f, 4.5f, 3, 500 + i, MatInk);
		}
		for (int32 i = 0; i < 2; ++i)
		{
			const FVector At(-6.f + i * 9.f, 22.f - i * 2.f, 0.06f);
			D.Add(FRoomShapes::Plane(), At, FRotator(0.f, Random.FRandRange(-20.f, 20.f), 0.f), FVector(7.6f, 7.6f, 1.f), F(i ? TEXT("NotePink") : TEXT("NoteYellow")), false);
		}
	}

	// The juice box she finished, its straw still in it, by the books.
	{
		const FVector At(52.f, 17.f, 0.f);
		const FRotator Tilt(0.f, 24.f, 0.f);
		D.Box(At + FVector(0.f, 0.f, 5.1f), Tilt, FVector(6.2f, 4.0f, 10.2f), F(TEXT("Juice")), false);
		D.Box(At + Tilt.RotateVector(FVector(0.f, 2.02f, 6.f)), Tilt, FVector(4.6f, 0.05f, 5.f), MatPaper, false);
		D.Cyl(At + Tilt.RotateVector(FVector(1.4f, 0.f, 12.2f)), Tilt + FRotator(0.f, 0.f, 8.f), FVector(0.5f, 0.5f, 6.f), F(TEXT("Pink")), false);
		D.Cyl(At + Tilt.RotateVector(FVector(1.4f, 1.3f, 15.4f)), Tilt + FRotator(0.f, 0.f, 60.f), FVector(0.5f, 0.5f, 3.f), F(TEXT("Pink")), false);
	}

	Build.Stain(RoomSurfaces::Damp, Seat + FVector(0.f, 0.f, DeskTop + 10.f), FRotator(-90.f, 0.f, 0.f), FVector2D(118.f, 60.f),
		FLinearColor(0.42f, 0.40f, 0.36f), 0.30f, 1.35f);

	// Chips on the desk's front edge and the drawer fronts, where knees and the chair knocked them.
	auto Chip = [&](const FVector& Local, const FVector& LocalInto, const FVector2D& Size)
	{
		const FVector Into = Turn.RotateVector(LocalInto);
		Build.Stain(RoomSurfaces::RoughWood, Seat + Turn.RotateVector(Local) - Into * 3.f, FRotationMatrix::MakeFromX(Into).Rotator(), Size, FLinearColor(1.1f, 1.0f, 0.95f), 0.95f, 0.8f);
	};
	Chip(FVector(-10.f, 28.f, 73.5f), FVector(0.f, -1.f, 0.f), FVector2D(16.f, 3.f));
	Chip(FVector(34.f, 27.f, 40.f), FVector(0.f, -1.f, 0.f), FVector2D(7.f, 9.f));
}

void ANurseryActor::BuildShelves(FRoomBuilder& Build)
{
	// The bookcase on the west wall: her books, more grown-up the higher the shelf, and on its top
	// the things a girl keeps where she can see them from the mirror she does not have yet.
	{
		const FVector Seat = BookcaseSeat();
		const FRotator Turn(0.f, FacingYaw(EWall::West), 0.f);
		Dress(Build.Prop(RoomProps::NurseryBookcase, Seat, Turn, 0.f, false), { { TEXT("Paint"), MatPink }, { TEXT("Inside"), MatPinkInside } });
		NurseryPawnOnly(Build.Box(Seat + FVector(0.f, 0.f, 64.f), Turn, FVector(74.f, 30.f, 128.f), MatVoid));
		Footprints.Add(FBox2D(FVector2D(Seat.X - 15.f, Seat.Y - 38.f), FVector2D(Seat.X + 30.f, Seat.Y + 38.f)));

		USceneComponent* Pivot = NurseryPivot(this, RoomRoot, Seat, Turn, TEXT("Bookcase"));
		FRoomBuilder B(this, Pivot);
		const FVector Out(0.f, 1.f, 0.f);
		const TCHAR* Covers[] = { TEXT("NovelA"), TEXT("NovelB"), TEXT("NovelC"), TEXT("NovelD"), TEXT("NovelE"), TEXT("Pink"), TEXT("Blue"), TEXT("Purple") };
		// Two shelves of books, run along each with gaps, the last few leaning into a gap.
		for (const float ShelfZ : { 39.f, 70.f })
		{
			float X = -32.f;
			int32 Count = 0;
			while (X < 30.f)
			{
				const float Thick = Random.FRandRange(1.4f, 3.8f);
				const float Height = ShelfZ < 50.f ? Random.FRandRange(19.f, 27.f) : Random.FRandRange(17.f, 22.f);
				const bool bLean = (Count > 0 && Random.FRand() < 0.12f);
				StandingBook(B, FVector(X + Thick * 0.5f, 1.f + Random.FRandRange(-1.f, 1.f), ShelfZ), Out, bLean ? -11.f : 0.f,
					FVector(Thick, Random.FRandRange(13.f, 18.f), Height), F(Covers[Random.RandRange(0, 7)]));
				X += Thick + (bLean ? 2.6f : 0.15f);
				if (++Count % 9 == 0)
				{
					X += 8.f; // a gap where a few went out and never came back
				}
			}
		}
		// The top shelf inside: a pile of colouring books from when she was small, lying flat.
		for (int32 i = 0; i < 4; ++i)
		{
			Book(B, FVector(-14.f + Random.FRandRange(-2.f, 2.f), 1.f, 100.f + i * 0.9f), Random.FRandRange(-6.f, 6.f), FVector(29.f, 22.f, 0.85f),
				F(i % 2 ? TEXT("Yellow") : TEXT("Green")));
		}
		Dress(B.Prop(RoomProps::Teddy, FVector(16.f, 2.f, 100.f), FRotator(0.f, -6.f, 0.f), 21.f, false),
			{ { TEXT("Fur"), MatRabbitFur }, { TEXT("Pad"), MatPad }, { TEXT("Button"), F(TEXT("Button")) }, { TEXT("Nose"), F(TEXT("Nose")) }, { TEXT("Ribbon"), F(TEXT("Blue")) } });
		// The bottom shelf: board games, boxes, a stack of magazines.
		B.Box(FVector(-14.f, 0.f, 7.f + 2.5f), FRotator(0.f, 2.f, 0.f), FVector(30.f, 24.f, 5.f), F(TEXT("Red")), false);
		B.Box(FVector(-13.f, 0.5f, 12.f + 2.f), FRotator(0.f, -3.f, 0.f), FVector(28.f, 22.f, 4.f), F(TEXT("Blue")), false);
		for (int32 i = 0; i < 6; ++i)
		{
			B.Box(FVector(17.f, 1.f, 7.f + 0.3f + i * 0.6f), FRotator(0.f, Random.FRandRange(-8.f, 8.f), 0.f), FVector(21.f, 27.f, 0.6f), i % 2 ? MatPaper.Get() : F(TEXT("NotePink")), false);
		}
		// On top: the music box, her hairbrush, a ribbon. The music box is a clue (BuildClues).
		Dress(B.Prop(RoomProps::Hairbrush, FVector(-20.f, 4.f, 127.f), FRotator(0.f, 130.f, 0.f), 0.f, false), { { TEXT("Plastic"), F(TEXT("Plastic")) }, { TEXT("Cushion"), F(TEXT("Cushion")) } });
		Dress(B.Prop(RoomProps::HairBow, FVector(-6.f, 8.f, 127.f), FRotator(0.f, -20.f, 0.f), 0.f, false), { { TEXT("Ribbon"), F(TEXT("Purple")) } });
		Build.Stain(RoomSurfaces::Damp, Seat + FVector(0.f, 0.f, 137.f), FRotator(-90.f, 0.f, 0.f), FVector2D(80.f, 36.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.42f, 1.35f);
	}

	// The low shelf of cubbies along the south wall: toys below, the doll's house on top.
	{
		const FVector Seat = ToyShelfSeat();
		const FRotator Turn(0.f, FacingYaw(EWall::South), 0.f);
		Dress(Build.Prop(RoomProps::NurseryToyShelf, Seat, Turn, 0.f, false), { { TEXT("Paint"), MatPink }, { TEXT("Inside"), MatPinkInside } });
		NurseryPawnOnly(Build.Box(Seat + FVector(0.f, 0.f, 40.f), Turn, FVector(122.f, 35.f, 80.f), MatVoid));
		Footprints.Add(FBox2D(FVector2D(Seat.X - 62.f, Seat.Y - 20.f), FVector2D(Seat.X + 62.f, Seat.Y + 20.f)));

		USceneComponent* Pivot = NurseryPivot(this, RoomRoot, Seat, Turn, TEXT("ToyShelf"));
		FRoomBuilder S(this, Pivot);
		const TMap<FName, UMaterialInterface*> HouseSlots = { { TEXT("Paint"), F(TEXT("HouseWalls")) }, { TEXT("Roof"), F(TEXT("HouseRoof")) },
			{ TEXT("Trim"), F(TEXT("HouseTrim")) }, { TEXT("Shadow"), MatShadow }, { TEXT("Brass"), MatBrass } };
		Dress(S.Prop(RoomProps::DollHouse, FVector(-28.f, -1.f, 80.f), FRotator(0.f, 4.f, 0.f), 0.f, false), HouseSlots);
		// The cloth doll on the end of the top, sitting with her legs out over the edge.
		Dress(S.Prop(RoomProps::RagDoll, FVector(34.f, -2.f, 80.f), FRotator(0.f, -8.f, 0.f), 0.f, false),
			{ { TEXT("Skin"), MatDollSkin }, { TEXT("Dress"), MatDollDress }, { TEXT("Hair"), MatDollHair }, { TEXT("Ribbon"), F(TEXT("Ribbon")) },
			  { TEXT("Button"), F(TEXT("Button")) }, { TEXT("Cheek"), F(TEXT("Cheek")) }, { TEXT("Shoe"), F(TEXT("Shoe")) } });

		// The cubbies: blocks, the old colouring books, a box of crayons, a small bear and the games.
		const float Lower = 6.f;
		const float Upper = 42.f;
		const TCHAR* BlockColours[] = { TEXT("Red"), TEXT("Blue"), TEXT("Yellow"), TEXT("Green") };
		for (int32 i = 0; i < 9; ++i)
		{
			const int32 Layer = i < 5 ? 0 : (i < 8 ? 1 : 2);
			const float X = -39.f + (i % 5 - 2) * 5.4f + Layer * 2.7f + Random.FRandRange(-0.6f, 0.6f);
			if (UStaticMeshComponent* Block = S.Prop(RoomProps::ToyBlock, FVector(X, Random.FRandRange(-4.f, 4.f), Lower + Layer * 5.f), FRotator(0.f, Random.FRandRange(-20.f, 20.f), 0.f), 0.f, false))
			{
				Block->SetMaterialByName(TEXT("Paint"), i % 3 == 0 ? MatBareWood.Get() : F(BlockColours[i % 4]));
			}
		}
		for (int32 i = 0; i < 5; ++i)
		{
			Book(S, FVector(0.f + Random.FRandRange(-1.5f, 1.5f), 0.f, Lower + i * 0.8f), Random.FRandRange(-5.f, 5.f), FVector(28.f, 21.f, 0.75f),
				F(i % 3 == 0 ? TEXT("Pink") : (i % 3 == 1 ? TEXT("Yellow") : TEXT("Blue"))));
		}
		// The crayon box, open, a few of them left in it.
		S.Box(FVector(39.f, 2.f, Lower + 2.f), FRotator(0.f, 6.f, 0.f), FVector(14.f, 9.f, 4.f), F(TEXT("Yellow")), false);
		for (int32 i = 0; i < 6; ++i)
		{
			S.Cyl(FVector(34.f + i * 1.9f, 2.f, Lower + 4.5f), FRotator(0.f, 0.f, 0.f), FVector(0.9f, 0.9f, 5.f), F(BlockColours[i % 4]), false);
		}
		Dress(S.Prop(RoomProps::Teddy, FVector(-39.f, 0.f, Upper), FRotator(0.f, 10.f, 0.f), 24.f, false),
			{ { TEXT("Fur"), MatBearFur }, { TEXT("Pad"), MatPad }, { TEXT("Button"), F(TEXT("Button")) }, { TEXT("Nose"), F(TEXT("Nose")) }, { TEXT("Ribbon"), F(TEXT("Green")) } });
		S.Box(FVector(0.f, 0.f, Upper + 3.5f), FRotator(0.f, -3.f, 0.f), FVector(32.f, 24.f, 7.f), F(TEXT("Green")), false);
		S.Box(FVector(1.f, 0.f, Upper + 9.f), FRotator(0.f, 5.f, 0.f), FVector(30.f, 22.f, 4.f), F(TEXT("Purple")), false);
		for (int32 i = 0; i < 3; ++i)
		{
			StandingBook(S, FVector(33.f + i * 3.f, 0.f, Upper), FVector(0.f, 1.f, 0.f), i == 2 ? -14.f : 0.f, FVector(2.6f, 22.f, 28.f), F(i ? TEXT("NovelC") : TEXT("Pink")));
		}
		Build.Stain(RoomSurfaces::Damp, Seat + FVector(0.f, 0.f, 90.f), FRotator(-90.f, 0.f, 90.f), FVector2D(126.f, 38.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.42f, 1.35f);
	}
}

void ANurseryActor::BuildToys(FRoomBuilder& Build)
{
	// The toy chest at the foot of the bed, its lid thrown back, toys up to the brim and over it:
	// these are the ones she had outgrown, and still had not given away.
	const FVector Seat = ToyChestSeat();
	const FRotator Turn(0.f, FacingYaw(EWall::South), 0.f);
	Dress(Build.Prop(RoomProps::NurseryToyChest, Seat, Turn, 0.f, false), { { TEXT("Paint"), MatPink }, { TEXT("Inside"), MatPinkInside }, { TEXT("Brass"), MatBrass } });
	Dress(Build.Prop(RoomProps::NurseryToyChestLid, Seat + Turn.RotateVector(FVector(0.f, -22.2f, 44.f)), FRotator(0.f, Turn.Yaw, 104.f), 0.f, false),
		{ { TEXT("Paint"), MatPink }, { TEXT("Inside"), MatPinkInside }, { TEXT("Brass"), MatBrass } });
	NurseryPawnOnly(Build.Box(Seat + FVector(0.f, 0.f, 24.f), Turn, FVector(84.f, 46.f, 48.f), MatVoid));
	Footprints.Add(FBox2D(FVector2D(Seat.X - 42.f, Seat.Y - 34.f), FVector2D(Seat.X + 42.f, Seat.Y + 24.f)));

	USceneComponent* Pivot = NurseryPivot(this, RoomRoot, Seat, Turn, TEXT("ToyChest"));
	FRoomBuilder C(this, Pivot);
	// Inside, the heap: balls, blocks, a rabbit with its ears over the edge.
	C.Box(FVector(0.f, 0.f, 22.f), FRotator::ZeroRotator, FVector(74.f, 36.f, 30.f), MatShadow, false);
	C.Sph(FVector(-24.f, -4.f, 40.f), 15.f, F(TEXT("Red")));
	C.Sph(FVector(20.f, 6.f, 39.f), 11.f, F(TEXT("Blue")));
	const TCHAR* BlockColours[] = { TEXT("Red"), TEXT("Blue"), TEXT("Yellow"), TEXT("Green") };
	for (int32 i = 0; i < 6; ++i)
	{
		if (UStaticMeshComponent* Block = C.Prop(RoomProps::ToyBlock, FVector(Random.FRandRange(-30.f, 30.f), Random.FRandRange(-12.f, 12.f), Random.FRandRange(34.f, 38.f)),
			FRotator(Random.FRandRange(-30.f, 30.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-30.f, 30.f)), 0.f, false))
		{
			Block->SetMaterialByName(TEXT("Paint"), F(BlockColours[i % 4]));
		}
	}
	Dress(C.Prop(RoomProps::PlushRabbit, FVector(4.f, -4.f, 26.f), FRotator(-24.f, 0.f, 6.f), 0.f, false),
		{ { TEXT("Fur"), MatRabbitFur }, { TEXT("Pad"), MatPad }, { TEXT("Button"), F(TEXT("Button")) }, { TEXT("Nose"), F(TEXT("Cheek")) } });
	// And spilled on the boards in front: blocks, one still on another.
	for (int32 i = 0; i < 5; ++i)
	{
		const FVector At(Random.FRandRange(-40.f, 34.f), Random.FRandRange(32.f, 70.f), 0.f);
		if (UStaticMeshComponent* Block = C.Prop(RoomProps::ToyBlock, At + FVector(0.f, 0.f, i == 4 ? 5.f : 0.f), FRotator(0.f, Random.FRandRange(0.f, 90.f), 0.f), 0.f, false))
		{
			Block->SetMaterialByName(TEXT("Paint"), F(BlockColours[(i + 1) % 4]));
		}
	}
	Dust(Build, Seat + FVector(0.f, 0.f, 44.f), FVector2D(48.f, 86.f), 0.38f);

	// The rug in the middle of the room: a dusty rose, its pile flattened on the way to the bed.
	const FVector RugCentre(MidX() - 70.f, MidY() + 10.f, 1.6f);
	Build.Cloth(RugCentre, FRotator(0.f, 3.f, 0.f), FVector2D(230.f, 160.f), 0.3f, 0.f, 7301, MatRug, RoomSurfaces::Carpet.TexelSizeCm, /*bWorn*/ false);
	Build.Stain(RoomSurfaces::Damp, RugCentre + FVector(70.f, 30.f, 4.f), FRotator(-90.f, 0.f, 20.f), FVector2D(70.f, 150.f), FLinearColor(0.30f, 0.26f, 0.22f), 0.3f, 1.3f);

	// On it: a tower of blocks, half knocked down, and a colouring book open with its crayons.
	for (int32 i = 0; i < 5; ++i)
	{
		const bool bFallen = i >= 3;
		const FVector At = bFallen
			? RugCentre + FVector(-40.f + i * 7.f, 18.f + i * 4.f, 0.4f)
			: RugCentre + FVector(-50.f + Random.FRandRange(-0.4f, 0.4f), 10.f + Random.FRandRange(-0.4f, 0.4f), 0.4f + i * 5.f);
		if (UStaticMeshComponent* Block = Build.Prop(RoomProps::ToyBlock, At, FRotator(bFallen ? 90.f : 0.f, Random.FRandRange(-14.f, 14.f) + i * 9.f, 0.f), 0.f, false))
		{
			Block->SetMaterialByName(TEXT("Paint"), i % 2 ? MatBareWood.Get() : F(BlockColours[i % 4]));
		}
	}
	{
		const FVector At = RugCentre + FVector(30.f, -20.f, 0.5f);
		const float Yaw = 160.f;
		Build.Box(At + FVector(0.f, 0.f, 0.15f), FRotator(0.f, Yaw, 0.f), FVector(42.f, 29.f, 0.3f), F(TEXT("Yellow")), false);
		const FVector Across = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(1.f, 0.f, 0.f));
		const FVector Down = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.f, 1.f, 0.f));
		Build.Box(At - Across * 10.4f + FVector(0.f, 0.f, 0.6f), FRotator(0.f, Yaw, 0.f), FVector(20.4f, 28.f, 0.6f), MatPaper, false);
		// The right-hand page is one of the drawings: the rainbow, coloured in and over the lines.
		Drawing(Build, 4, At + Across * 10.4f + FVector(0.f, 0.f, 1.0f), FVector::UpVector, -Down, 0.94f);
		const TCHAR* Crayons[] = { TEXT("Red"), TEXT("Orange"), TEXT("Yellow"), TEXT("Green"), TEXT("Blue"), TEXT("Purple") };
		for (int32 i = 0; i < 6; ++i)
		{
			Pencil(Build, At + Across * Random.FRandRange(-30.f, 30.f) + Down * Random.FRandRange(18.f, 30.f), Random.FRandRange(0.f, 360.f), 8.5f, 1.1f, F(Crayons[i]));
		}
	}

	// The basket of soft toys in the corner past the shelf, the rabbit sitting up in it.
	{
		const FVector At(WestX() + 38.f, SouthY() - 40.f, 0.f);
		if (UStaticMeshComponent* Basket = Build.PropSeated(RoomProps::WickerBasket, At, FRotator(0.f, 34.f, 0.f), 0.f, false))
		{
			FRoomShapes::TintSlots(Basket, FLinearColor(0.50f, 0.46f, 0.42f));
		}
		Dress(Build.Prop(RoomProps::PlushRabbit, At + FVector(0.f, 0.f, 3.f), FRotator(0.f, 180.f - 44.f, 0.f), 0.f, false),
			{ { TEXT("Fur"), MatRabbitFur }, { TEXT("Pad"), MatPad }, { TEXT("Button"), F(TEXT("Button")) }, { TEXT("Nose"), F(TEXT("Cheek")) } });
		Footprints.Add(FBox2D(FVector2D(At.X - 22.f, At.Y - 22.f), FVector2D(At.X + 22.f, At.Y + 22.f)));
	}

	// Her ukulele, leaning against the west wall between the bookcase and the corner. She was
	// learning; there is a chord chart on the wall beside it (BuildDrawings has the drawings, this
	// is the one sheet she wrote rather than drew).
	if (UStaticMeshComponent* Uke = Build.Prop(RoomProps::Ukulele, FVector(WestX() + 12.f, SouthY() - 112.f, 0.f), FRotator(0.f, -90.f, 13.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Uke, FLinearColor(0.60f, 0.56f, 0.52f));
	}
}

void ANurseryActor::BuildDrawings(FRoomBuilder& Build)
{
	// Her drawings, taped up over the years and never taken down: the ones from when she was small
	// low on the walls, where she could reach. Atlas order: 0 flowers, 1 house, 2 cat, 3 hearts,
	// 4 rainbow, 5 the family, 6 the pencil sketch (on the desk), 7 the sea.
	WallDrawing(Build, EWall::North, WestX() + 70.f, 150.f, 2, -4.f, false);
	WallDrawing(Build, EWall::North, WestX() + 140.f, 172.f, 4, 3.f, true);
	WallDrawing(Build, EWall::South, BedX() - 34.f, 160.f, 1, 2.f, false);
	WallDrawing(Build, EWall::South, BedX() + 30.f, 176.f, 3, -6.f, false);
	WallDrawing(Build, EWall::South, ToyShelfSeat().X - 38.f, 178.f, 5, -2.f, true);
	WallDrawing(Build, EWall::South, ToyShelfSeat().X + 30.f, 166.f, 0, 5.f, false);
	WallDrawing(Build, EWall::East, DeskSeat().Y - 26.f, 160.f, 7, -3.f, false);

	// Where one hung and came down: a cleaner oblong of paper the light never reached, and the tape
	// still on the wall. And it on the floor below, face up, the tape still on one corner.
	{
		UMaterialInterface* Clean = Build.Surface(RoomSurfaces::NurseryWallpaper, FLinearColor(0.205f, 0.212f, 0.211f));
		const float U = EastX() - 46.f;
		Build.Mark(WallPoint(EWall::South, U, 150.f, 0.5f), FRotator(0.f, 0.f, -90.f + 4.f), FVector2D(21.f, 29.7f), Clean);
		Drawing(Build, 1, FVector(U - 6.f, SouthY() - 40.f, 0.65f), FVector::UpVector, FVector(0.7f, 0.7f, 0.f).GetSafeNormal());
	}
	Drawing(Build, 2, FVector(WestX() + 90.f, NorthY() + 30.f, 0.65f), FVector::UpVector, FVector(-0.3f, -1.f, 0.f).GetSafeNormal());
	Drawing(Build, 0, FVector(ToyShelfSeat().X + 70.f, SouthY() - 50.f, 0.7f), FVector::UpVector, FVector(1.f, 0.2f, 0.f).GetSafeNormal());

	// Her ukulele chord chart, the one sheet up here written rather than drawn: four little grids.
	{
		const FVector N = WallNormal(EWall::West);
		const FVector Right = FVector::CrossProduct(N, FVector::UpVector);
		const FVector Centre = WallPoint(EWall::West, SouthY() - 112.f, 108.f, PaperProud);
		const FRotator Face = FRotationMatrix::MakeFromZX(N, Right).Rotator();
		Build.Add(FRoomShapes::Plane(), Centre, Face, FVector(21.f, 29.7f, 1.f), MatPaper, false)->SetReceivesDecals(false);
		const FTransform Sheet(Face, Centre + N * 0.05f);
		for (int32 g = 0; g < 4; ++g)
		{
			const float GX = (g % 2 == 0 ? -5.f : 5.f);
			const float GY = (g < 2 ? -6.f : 6.f);
			for (int32 l = 0; l < 4; ++l)
			{
				Stroke(Build, Sheet, GX - 2.4f + l * 1.6f, GY, 0.12f, 6.f, MatPencil);
			}
			for (int32 l = 0; l < 5; ++l)
			{
				Stroke(Build, Sheet, GX, GY - 3.f + l * 1.5f, 4.8f, 0.12f, MatPencil);
			}
			Build.Add(FRoomShapes::Plane(), Sheet.TransformPosition(FVector(GX - 0.8f + g * 0.6f, GY - 0.8f, 0.02f)), Face, FVector(0.9f, 0.9f, 1.f), MatPencil, false);
		}
		Stroke(Build, Sheet, 0.f, -12.f, 9.f, 0.5f, MatInk);
	}
}

void ANurseryActor::BuildFloorThings(FRoomBuilder& Build)
{
	// Not much: this is a room somebody tidied, more or less, every day. A little plaster down from
	// the corners, and a sheet of homework that slid off the desk.
	for (int32 i = 0; i < 26; ++i)
	{
		const bool bWestSouth = (i % 2) == 0;
		const FVector2D Spot = bWestSouth
			? FVector2D(Random.FRandRange(WestX() + 6.f, WestX() + 40.f), Random.FRandRange(NorthY() + 10.f, SouthY() - 10.f))
			: FVector2D(Random.FRandRange(EastX() - 40.f, EastX() - 6.f), Random.FRandRange(WindowY() - 120.f, WindowY() + 120.f));
		const float S = Random.FRandRange(1.f, 4.5f);
		const FRotator R(Random.FRandRange(-30.f, 30.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-30.f, 30.f));
		if (!IsFloorSpotClear(Spot.X, Spot.Y, 4.f))
		{
			continue;
		}
		Build.Box(FVector(Spot.X, Spot.Y, 0.5f + S * 0.3f), R, FVector(S * 1.3f, S, S * 0.6f), MatRubble, false);
	}
	{
		const FVector At(DeskSeat().X - 70.f, DeskSeat().Y - 30.f, 0.65f);
		const FRotator Lay(0.f, 104.f, 0.f);
		Build.Mark(At - FVector(0.f, 0.f, 0.75f), Lay, FVector2D(21.f, 29.7f), MatPaper);
		Writing(Build, FTransform(Lay, At + FVector(0.f, 0.f, 0.1f)), 16.f, 22.f, 13, 611, MatInk);
	}
}

// ---------------------------------------------------------------------------------------------

void ANurseryActor::SpawnWindow()
{
	// The big window, a follower of the bedroom's storm on the same façade: one sky, one lightning,
	// and the lead's directional light comes straight in through it and across the toys. The
	// curtains are her pink rose print.
	const FTransform Transform(FRotator::ZeroRotator, GetActorTransform().TransformPosition(FVector(EastX() + Setup.WallThickness * 0.5f, WindowY(), 0.f)));
	Window = GetWorld()->SpawnActorDeferred<AStormWindowActor>(AStormWindowActor::StaticClass(), Transform, this);
	if (Window)
	{
		FStormWindowSetup WindowSetup;
		WindowSetup.OpeningWidth = WindowWidth;
		WindowSetup.SillHeight = WindowSill;
		WindowSetup.TopHeight = WindowTop;
		WindowSetup.WallThickness = Setup.WallThickness;
		WindowSetup.Seed = 20130611;
		// The opening is a good deal more sky than the bedroom's, and it lit the desk beside it like
		// a summer afternoon: at the kitchen's figure the storm is a cold grey on the wall again.
		WindowSetup.PortalScale = 0.42f;
		// floral_fabric measures (0.771, 0.468, 0.485): this lands at about (0.11, 0.072, 0.070).
		WindowSetup.CurtainSurface = &RoomSurfaces::FloralFabric;
		WindowSetup.CurtainTint = FLinearColor(0.143f, 0.154f, 0.144f);
		Window->Configure(WindowSetup);
		Window->SetLead(LeadStorm);
		Window->FinishSpawning(Transform);
	}
}

AClueActor* ANurseryActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AClueActor* Clue = GetWorld()->SpawnActor<AClueActor>(AClueActor::StaticClass(), GetActorTransform().TransformPosition(LocalLocation), Rotation, SpawnParams);
	if (Clue)
	{
		Clues.Add(Clue);
	}
	return Clue;
}

void ANurseryActor::BuildClues()
{
	// Where her clues will go. Set dressing only, as everywhere in the house until the game is
	// built end to end: no text, no prompt (the 09-30 decision).

	// The tablet on her desk, in its pink case, its screen black and dusted over.
	{
		const FVector Seat = DeskSeat();
		const FRotator Turn(0.f, FacingYaw(EWall::East), 0.f);
		if (AClueActor* Clue = SpawnClue(Seat + Turn.RotateVector(FVector(-14.f, -2.f, 0.f)) + FVector(0.f, 0.f, DeskTop), FRotator(0.f, Turn.Yaw + 8.f, 0.f)))
		{
			FRoomBuilder B(Clue, Clue->GetRootScene());
			Dress(B.Prop(RoomProps::Tablet, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false),
				{ { TEXT("Case"), F(TEXT("Case")) }, { TEXT("Screen"), F(TEXT("Screen")) }, { TEXT("Lens"), F(TEXT("Lens")) } });
			B.Stain(RoomSurfaces::Damp, FVector(0.f, 0.f, 6.f), FRotator(-90.f, 0.f, 0.f), FVector2D(24.f, 30.f), FLinearColor(0.42f, 0.40f, 0.36f), 0.3f, 1.4f);
		}
	}

	// The drawing she was doing: this house, in pencil, from the lawn, the left half shaded and the
	// right half not begun. The pencil on it and the eraser beside.
	{
		const FVector Seat = DeskSeat();
		const FRotator Turn(0.f, FacingYaw(EWall::East), 0.f);
		const FVector At = Seat + Turn.RotateVector(FVector(30.f, 15.f, 0.f)) + FVector(0.f, 0.f, DeskTop);
		if (AClueActor* Clue = SpawnClue(At, FRotator::ZeroRotator))
		{
			FRoomBuilder B(Clue, Clue->GetRootScene());
			// Lying flat, its top towards the wall, as she sat at it.
			const FVector Up = Turn.RotateVector(FVector(0.f, -1.f, 0.f)).RotateAngleAxis(-14.f, FVector::UpVector);
			Drawing(B, 6, FVector(0.f, 0.f, 0.08f), FVector::UpVector, Up);
			Pencil(B, FVector(4.f, 3.f, 0.12f), Turn.Yaw + 70.f, 16.f, 0.75f, F(TEXT("Yellow")));
			B.Box(FVector(-9.f, 13.f, 0.6f), FRotator(0.f, 20.f, 0.f), FVector(4.f, 2.f, 1.1f), F(TEXT("Pink")), false);
		}
	}

	// The homework: her exercise book open on the desk, the left page written up in blue and the
	// right one stopped a third of the way down, mid-line.
	{
		const FVector Seat = DeskSeat();
		const FRotator Turn(0.f, FacingYaw(EWall::East), 0.f);
		if (AClueActor* Clue = SpawnClue(Seat + Turn.RotateVector(FVector(6.f, 2.f, 0.f)) + FVector(0.f, 0.f, DeskTop), Turn + FRotator(0.f, -5.f, 0.f)))
		{
			FRoomBuilder B(Clue, Clue->GetRootScene());
			B.Box(FVector(0.f, 0.f, 0.15f), FRotator::ZeroRotator, FVector(35.f, 24.5f, 0.3f), F(TEXT("Blue")), false);
			for (const float S : { -1.f, 1.f })
			{
				// An open book lies in a shallow V: pitch is the tilt about the spine (the kitchen's note).
				const FTransform Page(FRotator(S * 2.f, 0.f, 0.f), FVector(S * 8.6f, 0.f, 0.7f));
				B.Box(Page.GetLocation(), Page.Rotator(), FVector(16.6f, 23.6f, 0.8f), MatPaper, false);
				// Read from the room side (+Y), so the lines run along -X with the sheet turned half round.
				const FTransform Sheet = FTransform(FRotator(0.f, 180.f, 0.f), FVector(0.f, S > 0.f ? -5.f : 0.f, 0.42f)) * Page;
				Writing(B, Sheet, 13.f, S < 0.f ? 19.f : 8.f, S < 0.f ? 15 : 5, S < 0.f ? 401 : 402, MatInk);
			}
			// Her pen, lying across the unfinished page where she put it down.
			Pencil(B, FVector(9.f, 3.f, 1.3f), 28.f, 14.f, 0.9f, F(TEXT("Purple")));
		}
	}

	// The birthday card, standing open on top of the toy shelf, between the doll's house and the
	// doll: a pink card with a balloon on it.
	{
		const FVector Seat = ToyShelfSeat();
		const FRotator Turn(0.f, FacingYaw(EWall::South), 0.f);
		const FVector At = Seat + Turn.RotateVector(FVector(10.f, -2.f, 80.f));
		// Yaw 0 keeps the front leaf (local -Y) to the room, a little turned.
		if (AClueActor* Clue = SpawnClue(At, FRotator(0.f, 14.f, 0.f)))
		{
			FRoomBuilder B(Clue, Clue->GetRootScene());
			// A tent of two leaves, each standing 70 degrees, the fold at the top.
			for (const float S : { -1.f, 1.f })
			{
				const FRotator Leaf(0.f, 0.f, S * 22.f);
				B.Box(FVector(0.f, S * 3.6f, 7.2f), Leaf, FVector(10.5f, 0.2f, 15.f), S < 0.f ? F(TEXT("Card")) : MatPaper.Get(), false);
			}
			// The balloon on the front, its string.
			// The front leaf leans in at 22 degrees, so its face at height Z is at Y = -3.7 + (Z - 7.2) * tan 22.
			const float Lean = FMath::Tan(FMath::DegreesToRadians(22.f));
			B.Add(FRoomShapes::Sphere(), FVector(1.5f, -3.9f + (10.f - 7.2f) * Lean, 10.f), FRotator(0.f, 0.f, -22.f), FVector(3.4f, 0.6f, 4.0f), F(TEXT("Red")), false);
			B.Box(FVector(1.5f, -3.85f + (5.5f - 7.2f) * Lean, 5.5f), FRotator(0.f, 0.f, -22.f), FVector(0.15f, 0.1f, 4.6f), MatPencil, false);
		}
	}

	// The family photograph on the bedside table, turned to the pillow.
	{
		const FVector At = NightstandSeat() + FVector(-10.f, 5.f, 60.f);
		if (AClueActor* Clue = SpawnClue(At, FRotator::ZeroRotator))
		{
			FRoomBuilder B(Clue, Clue->GetRootScene());
			if (UStaticMeshComponent* Frame = B.PropSeated(RoomProps::PhotoFrame, FVector::ZeroVector, FRotator(0.f, 90.f + 18.f, 0.f), 19.f, false))
			{
				// standing_picture_frame_01: glass, artwork, frame. The print is the asset's canal,
				// so it is faded to one brown and the clue, when it is written, says what it was.
				Frame->SetMaterial(0, MatGlass);
				Frame->SetMaterial(1, MatPaperDamp);
				FRoomShapes::TintSlots(Frame, FLinearColor(0.60f, 0.52f, 0.52f), 2);
			}
		}
	}

	// The music box on the bookcase, open, the dancer standing up on her spring. It stopped.
	{
		const FVector Seat = BookcaseSeat();
		const FRotator Turn(0.f, FacingYaw(EWall::West), 0.f);
		if (AClueActor* Clue = SpawnClue(Seat + Turn.RotateVector(FVector(12.f, 3.f, 127.f)), FRotator(0.f, Turn.Yaw - 16.f, 0.f)))
		{
			FRoomBuilder B(Clue, Clue->GetRootScene());
			Dress(B.Prop(RoomProps::MusicBox, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false),
				{ { TEXT("Box"), MatPink }, { TEXT("Velvet"), F(TEXT("Velvet")) }, { TEXT("Mirror"), F(TEXT("Mirror")) }, { TEXT("Figure"), F(TEXT("Figure")) }, { TEXT("Brass"), MatBrass } });
		}
	}

	// The calendar on the north wall beside the desk: a cat for the month, stickers on the days.
	{
		const float U = EastX() - 88.f;
		const float Z = 162.f;
		if (AClueActor* Clue = SpawnClue(WallPoint(EWall::North, U, Z, PaperProud), FRotator::ZeroRotator))
		{
			FRoomBuilder B(Clue, Clue->GetRootScene());
			const FVector N(0.f, 1.f, 0.f);
			// On the north wall the viewer looks along -Y, and their right is +X.
			const FVector Right = FVector::CrossProduct(N, FVector::UpVector);
			const FRotator Face = FRotationMatrix::MakeFromZX(N, Right).Rotator();
			B.Add(FRoomShapes::Plane(), FVector::ZeroVector, Face, FVector(30.f, 44.f, 1.f), MatPaper, false);
			// The month's picture, a kitten gone brown, on the top half.
			B.Add(FRoomShapes::Plane(), FVector(0.f, 0.05f, 11.f), Face, FVector(26.f, 18.f, 1.f), MatPaperDamp, false);
			const FTransform Sheet(Face, FVector(0.f, 0.1f, -9.f));
			// The grid of days: five rules across, eight down. The sheet's local -Y is up the wall.
			for (int32 i = 0; i < 6; ++i)
			{
				Stroke(B, Sheet, 0.f, -9.f + i * 3.6f, 26.f, 0.18f, MatPencil);
			}
			for (int32 i = 0; i < 8; ++i)
			{
				Stroke(B, Sheet, -13.f + i * (26.f / 7.f), 0.f, 0.18f, 18.f, MatPencil);
			}
			// Stickers: hearts and stars and smiling faces, on the days that mattered, the most of
			// them near the end of the month, and none after the day before the last one she saw.
			const TCHAR* Colours[] = { TEXT("Pink"), TEXT("Yellow"), TEXT("Blue"), TEXT("Purple"), TEXT("Green") };
			const FIntPoint Days[] = { { 1, 0 }, { 4, 1 }, { 2, 2 }, { 6, 2 }, { 0, 3 }, { 3, 3 }, { 5, 3 }, { 1, 4 } };
			int32 k = 0;
			for (const FIntPoint& Day : Days)
			{
				const FVector At = Sheet.TransformPosition(FVector(-11.2f + Day.X * (26.f / 7.f), -7.2f + Day.Y * 3.6f, 0.08f));
				B.Cyl(At, Face, FVector(2.2f, 2.2f, 0.12f), F(Colours[k++ % 5]), false);
			}
			B.Sph(FVector(0.f, 0.4f, 21.5f), 1.2f, MatIron);
			NurseryNoDecals(Clue);
		}
	}

	// Her heights, in pencil up the door casing on the room side, a line and a mark each birthday:
	// from not quite a metre to half again. The last two in pink felt pen, because by then she
	// wanted to.
	{
		const float U = Setup.DoorX + Setup.DoorHalf + 22.f;
		if (AClueActor* Clue = SpawnClue(WallPoint(EWall::North, U, 0.f, PaperProud), FRotator::ZeroRotator))
		{
			FRoomBuilder B(Clue, Clue->GetRootScene());
			const FVector N(0.f, 1.f, 0.f);
			const FRotator Face = FRotationMatrix::MakeFromZX(N, FVector(1.f, 0.f, 0.f)).Rotator();
			const float Heights[] = { 97.f, 104.f, 111.f, 118.f, 124.f, 131.f, 137.f, 143.f, 149.f, 153.f };
			FRandomStream Hand(9917);
			for (int32 i = 0; i < 10; ++i)
			{
				UMaterialInterface* Ink = i >= 8 ? F(TEXT("Pink")) : MatPencil.Get();
				const FTransform Sheet(Face, FVector(0.f, 0.f, Heights[i]));
				Stroke(B, Sheet, Hand.FRandRange(-0.5f, 0.5f), 0.f, Hand.FRandRange(7.f, 10.f), 0.22f, Ink);
				// The mark beside it, a couple of strokes that were her age and the year.
				Stroke(B, Sheet, 7.4f, -0.9f, 1.4f, 0.2f, Ink);
				Stroke(B, Sheet, 9.2f, -0.9f, 1.8f, 0.2f, Ink);
			}
			NurseryNoDecals(Clue);
		}
	}
}
