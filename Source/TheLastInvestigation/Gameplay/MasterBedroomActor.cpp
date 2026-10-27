#include "MasterBedroomActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "DustMotesComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

namespace
{
	/** The generated models (Tools/make_bedroom.py), /Game/Meshes/<name>. */
	namespace BedroomProps
	{
		const TCHAR* Bed = TEXT("bedroom_bed");
		const TCHAR* Nightstand = TEXT("bedroom_nightstand");
		const TCHAR* Bench = TEXT("bedroom_bench");
		const TCHAR* Wardrobe = TEXT("bedroom_wardrobe");
		const TCHAR* WardrobeOpen = TEXT("bedroom_wardrobe_open");
		const TCHAR* WardrobeDoor = TEXT("bedroom_wardrobe_door");
		const TCHAR* Dresser = TEXT("bedroom_dresser");
		const TCHAR* Media = TEXT("bedroom_media");
		const TCHAR* Television = TEXT("bedroom_tv");
		const TCHAR* Armchair = TEXT("bedroom_armchair");
		const TCHAR* Ottoman = TEXT("bedroom_ottoman");
		const TCHAR* SideTable = TEXT("bedroom_side_table");
		const TCHAR* FloorLamp = TEXT("bedroom_floor_lamp");
		const TCHAR* Mirror = TEXT("bedroom_mirror");
		const TCHAR* Suitcase = TEXT("bedroom_suitcase");
		const TCHAR* Plant = TEXT("bedroom_plant");
		const TCHAR* Clock = TEXT("bedroom_clock");
		const TCHAR* CeilingLight = TEXT("bedroom_ceiling_light");
		const TCHAR* Artwork = TEXT("bedroom_artwork");
		const TCHAR* FrameWedding = TEXT("bedroom_frame_wedding");
		const TCHAR* FrameFamily = TEXT("bedroom_frame_family");
		const TCHAR* Remote = TEXT("bedroom_remote");
		const TCHAR* Phone = TEXT("bedroom_phone");
		const TCHAR* Glasses = TEXT("bedroom_glasses");
		const TCHAR* Mug = TEXT("bedroom_mug");
		const TCHAR* Book = TEXT("bedroom_book");
		const TCHAR* Novel = TEXT("bedroom_novel");
		const TCHAR* Sculptures = TEXT("bedroom_sculptures");
		const TCHAR* JewelryBox = TEXT("bedroom_jewelry_box");
		const TCHAR* Perfume = TEXT("bedroom_perfume");
		const TCHAR* Hairbrush = TEXT("bedroom_hairbrush");
		const TCHAR* Watch = TEXT("bedroom_watch");
		const TCHAR* Tray = TEXT("bedroom_tray");
		const TCHAR* FoldedClothes = TEXT("bedroom_folded_clothes");
		const TCHAR* FoldedBlanket = TEXT("bedroom_folded_blanket");
		const TCHAR* PanelMoulding = TEXT("bedroom_panel_moulding");
		const TCHAR* Dado = TEXT("bedroom_dado");
		const TCHAR* Skirting = TEXT("bedroom_skirting");
		// Shared with the other rooms.
		const TCHAR* Cornice = TEXT("library_cornice");
		const TCHAR* Shirt = TEXT("laundry_shirt");
		const TCHAR* Dress = TEXT("girl_dress");
	}

	/** The house's dust, the colour it is everywhere. */
	const FLinearColor BedroomDust(0.42f, 0.40f, 0.36f);

	/** laundry_shirt's hook rises this far over its bar (make_laundry.py: SHIRT_HOOK). */
	constexpr float BedroomShirtHook = 7.f;
	/** The wardrobe's rail (make_bedroom.py: wardrobe_carcass), its centre in the module's frame. */
	constexpr float BedroomRailZ = 212.f;
	constexpr float BedroomRailY = -2.f;
	/** The tray's base, which the perfume stands on (make_bedroom.py: tray). */
	constexpr float BedroomTrayBase = 1.f;
	/** A decal reaches nine either way along its aim (FRoomBuilder::Stain); floor ones go at +4. */
	constexpr float BedroomFloorDecalZ = 4.f;

	/** For a model's own UVs, already in repeats of the slot's texel size: tiling at one. */
	void BedroomSheet(UMaterialInstanceDynamic* Mat)
	{
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}

	/** Keeps the walls' decals off something on a wall (the 10-02 rule for paper and frames). */
	void BedroomNoDecals(UPrimitiveComponent* Part)
	{
		if (Part)
		{
			Part->SetReceivesDecals(false);
		}
	}

	/** A child scene component to build a group of parts in its own frame: a wardrobe module's inside. */
	USceneComponent* BedroomPivot(AActor* Owner, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation, const TCHAR* Name)
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
	 * A sheet laid over pieces of a wall, its UVs in centimetres from the wall's own origin, so the
	 * paint's texture runs on across every cut (the music room's facade). As builder-tiled slabs,
	 * every narrow pier would carry its own stretched crop (the 10-09 pitfall). Both windings, the
	 * normal forced (the 09-21 pane lesson).
	 */
	UProceduralMeshComponent* BedroomFacade(AActor* Owner, USceneComponent* Parent, const TArray<FBox2D>& Pieces, const FVector& Origin,
		const FVector& Along, const FVector& Face, float TexelCm, UMaterialInterface* Mat)
	{
		if (Pieces.Num() == 0 || !Mat)
		{
			return nullptr;
		}
		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Tris;
		for (const FBox2D& Piece : Pieces)
		{
			const int32 First = Verts.Num();
			const FVector2D Corners[4] = { Piece.Min, FVector2D(Piece.Max.X, Piece.Min.Y), Piece.Max, FVector2D(Piece.Min.X, Piece.Max.Y) };
			for (const FVector2D& C : Corners)
			{
				Verts.Add(Origin + Along * C.X + FVector(0.f, 0.f, C.Y));
				Normals.Add(Face);
				UVs.Add(FVector2D(C.X / TexelCm, -C.Y / TexelCm));
				Tangents.Add(FProcMeshTangent(Along, false));
			}
			Tris.Append({ First, First + 1, First + 2, First, First + 2, First + 1, First, First + 2, First + 3, First, First + 3, First + 2 });
		}
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("WallPaint")));
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

AMasterBedroomActor::AMasterBedroomActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);
	RoomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void AMasterBedroomActor::Configure(const FMasterBedroomSetup& InSetup)
{
	Setup = InSetup;

	// Here and not in BeginPlay: the motes are scattered in the component's own BeginPlay, which runs
	// before the owner's (see ARoomDressingActor::Configure).
	DustMotes->ConfigureVolume(
		FVector(Setup.Width * 0.46f, Setup.Depth * 0.46f, Setup.Height * 0.46f),
		FVector(0.f, 0.f, Setup.Height * 0.5f));
}

void AMasterBedroomActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream: the room is the same every time the detective opens his eyes in it.
	Random.Initialize(19551104);

	FRoomBuilder Build(this, RoomRoot);
	CacheMaterials(Build);

	BuildFloor(Build);
	BuildWalls(Build);
	BuildJoinery(Build);
	BuildCeiling(Build);
	BuildHook(Build);
	BuildBed(Build);
	BuildWardrobes(Build);
	BuildDresser(Build);
	BuildTelevision(Build);
	BuildMirror(Build);
	BuildSittingCorner(Build);
	BuildFloorThings(Build);
	BuildDamage(Build);
	BuildClues();
}

void AMasterBedroomActor::CacheMaterials(FRoomBuilder& Build)
{
	// A sheet instance per surface for the generated models' own UVs, a hair off any tint the
	// builder's boxes use so the cache hands back a separate instance (the living room's rule).
	auto Sheet = [&](const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale = 1.f)
	{
		UMaterialInstanceDynamic* Mat = Build.Surface(Set, Tint + FLinearColor(0.0007f, 0.f, 0.f, 0.f), RoughnessScale);
		BedroomSheet(Mat);
		return Mat;
	};

	// The white room, on the room's own paint (RoomSurfaces::Paint, linear 0.596 / 0.580 / 0.534),
	// gone the colour white paint goes in a shut house: the walls at about (0.155, 0.14, 0.115), an
	// ivory yellowed and greyed, the furniture a shade over that. Still the palest room in the
	// house, as the brief has it; clean white (0.21) read as a showroom that had nothing to do with
	// the rest of the mansion. Warm rather than neutral, as everywhere: the only fill is the window.
	// Poly Haven's ceiling_interior was the first paint, and is a knock-down stucco: a wall of it
	// came out as heavy blobs.
	MatWall = Sheet(RoomSurfaces::Paint, FLinearColor(0.260f, 0.241f, 0.215f), 1.1f);
	MatCeiling = Build.Surface(RoomSurfaces::Paint, FLinearColor(0.252f, 0.238f, 0.215f), 1.1f);
	MatPaint = Sheet(RoomSurfaces::Paint, FLinearColor(0.294f, 0.284f, 0.262f), 0.9f);
	MatPaintBox = Build.Surface(RoomSurfaces::Paint, FLinearColor(0.294f, 0.284f, 0.262f), 0.9f);
	MatInside = Sheet(RoomSurfaces::Paint, FLinearColor(0.218f, 0.207f, 0.187f), 1.f);

	// Cloth on the linen photograph (0.284, 0.408, 0.614 — a blue linen; see the bedroom's notes),
	// each tint worked back from it: ivory upholstery, white bedding gone the yellow-grey of linen
	// nobody has washed or aired in years, a beige throw.
	MatUpholstery = Sheet(RoomSurfaces::Drapery, FLinearColor(0.60f, 0.40f, 0.23f), 1.15f);
	MatLinen = Sheet(RoomSurfaces::Drapery, FLinearColor(0.70f, 0.465f, 0.27f), 1.15f);
	MatMattress = Sheet(RoomSurfaces::Drapery, FLinearColor(0.64f, 0.43f, 0.25f), 1.15f);
	MatThrow = Sheet(RoomSurfaces::Drapery, FLinearColor(0.60f, 0.32f, 0.14f), 1.2f);
	MatShade = Sheet(RoomSurfaces::Drapery, FLinearColor(0.70f, 0.47f, 0.26f), 1.1f);
	MatShirt = Sheet(RoomSurfaces::Drapery, FLinearColor(0.70f, 0.49f, 0.34f), 1.1f);
	MatShirtBlue = Sheet(RoomSurfaces::Drapery, FLinearColor(0.25f, 0.22f, 0.21f), 1.1f);
	// Her dress, a deep wine, and a knit in oatmeal.
	MatDress = Sheet(RoomSurfaces::Drapery, FLinearColor(0.32f, 0.074f, 0.057f), 1.1f);
	MatKnit = Sheet(RoomSurfaces::Drapery, FLinearColor(0.53f, 0.29f, 0.15f), 1.25f);
	MatLining = Sheet(RoomSurfaces::Drapery, FLinearColor(0.14f, 0.10f, 0.073f), 1.1f);
	// The rug under the bed: a soft plain wool in a warm grey, on the linen at a coarser repeat.
	MatRug = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.53f, 0.34f, 0.20f), 1.3f);
	// Oak on the furniture's legs and the frames: the floor's boards, a shade lighter.
	MatOak = Sheet(RoomSurfaces::Oak, FLinearColor(0.50f, 0.60f, 0.73f), 1.3f);

	// The pictures, straight from their asset: the models carry UVs into it. Canvas and prints at a
	// third, as the music room's paper: paler burns out under the lantern.
	if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_bedroom_pictures.MI_bedroom_pictures")))
	{
		MatPictures = UMaterialInstanceDynamic::Create(Parent, this);
		MatPictures->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.30f, 0.29f, 0.27f));
		MatPictures->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
		BedroomSheet(MatPictures);
	}

	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	// Glass over the photographs and in the perfume bottles: the clock crystal's value (09-21).
	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.035f, 0.06f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);

	struct FFlat { const TCHAR* Name; FLinearColor Color; float Roughness; float Metallic; };
	const FFlat FlatList[] = {
		{ TEXT("Shadow"), FLinearColor(0.004f, 0.004f, 0.004f), 1.f, 0.f },
		// Brushed steel under dust: the handles, the rail, the frames' edges.
		{ TEXT("Metal"), FLinearColor(0.30f, 0.30f, 0.30f), 0.42f, 1.f },
		{ TEXT("Brass"), FLinearColor(0.22f, 0.16f, 0.07f), 0.4f, 1.f },
		// The television's white: the same white as the paint, a satin plastic.
		{ TEXT("Plastic"), FLinearColor(0.22f, 0.22f, 0.21f), 0.35f, 0.f },
		{ TEXT("Black"), FLinearColor(0.010f, 0.010f, 0.011f), 0.4f, 0.f },
		// Black glass: the screen, the phone.
		{ TEXT("Screen"), FLinearColor(0.003f, 0.003f, 0.0035f), 0.07f, 0.f },
		// The mirror by the door: black glass that still reflects. Dark and polished, it gives back
		// the lantern and the storm and very little else, which is what a mirror in a dark room does.
		// Smoked: at 0.04 it was a dark arch beside the door that read as a second doorway, and at
		// 0.13 a pale sheet of the white wall opposite. Between, a dark glass that still gives back
		// the room.
		{ TEXT("Mirror"), FLinearColor(0.075f, 0.075f, 0.078f), 0.04f, 1.f },
		{ TEXT("Frame"), FLinearColor(0.006f, 0.006f, 0.0065f), 0.45f, 0.f },
		{ TEXT("Liquid"), FLinearColor(0.18f, 0.08f, 0.02f), 0.2f, 0.f },
		{ TEXT("Ceramic"), FLinearColor(0.22f, 0.215f, 0.20f), 0.3f, 0.f },
		{ TEXT("Stone"), FLinearColor(0.03f, 0.03f, 0.03f), 0.5f, 0.f },
		{ TEXT("Soil"), FLinearColor(0.025f, 0.018f, 0.012f), 1.f, 0.f },
		{ TEXT("Stem"), FLinearColor(0.06f, 0.04f, 0.025f), 0.9f, 0.f },
		{ TEXT("Leaf"), FLinearColor(0.09f, 0.055f, 0.025f), 0.85f, 0.f },
		{ TEXT("Leather"), FLinearColor(0.03f, 0.015f, 0.008f), 0.6f, 0.f },
		{ TEXT("Velvet"), FLinearColor(0.05f, 0.01f, 0.015f), 0.95f, 0.f },
		{ TEXT("Diffuser"), FLinearColor(0.22f, 0.22f, 0.21f), 0.4f, 0.f },
		{ TEXT("Face"), FLinearColor(0.018f, 0.018f, 0.02f), 0.6f, 0.f },
		{ TEXT("Pages"), FLinearColor(0.20f, 0.18f, 0.14f), 0.9f, 0.f },
		// Book covers: navy, oxblood, a cream paperback, bottle green.
		{ TEXT("CoverA"), FLinearColor(0.02f, 0.03f, 0.06f), 0.6f, 0.f },
		{ TEXT("CoverB"), FLinearColor(0.10f, 0.02f, 0.02f), 0.6f, 0.f },
		{ TEXT("CoverC"), FLinearColor(0.15f, 0.13f, 0.10f), 0.7f, 0.f },
		{ TEXT("CoverD"), FLinearColor(0.02f, 0.05f, 0.03f), 0.6f, 0.f },
		// The hangers in the wardrobe.
		{ TEXT("Hanger"), FLinearColor(0.10f, 0.065f, 0.035f), 0.6f, 0.f },
	};
	for (const FFlat& Flat : FlatList)
	{
		Flats.Add(FName(Flat.Name), Build.Flat(Flat.Color, Flat.Roughness, Flat.Metallic));
	}
}

UMaterialInterface* AMasterBedroomActor::F(const TCHAR* Name) const
{
	const TObjectPtr<UMaterialInstanceDynamic>* Found = Flats.Find(FName(Name));
	return Found ? Found->Get() : MatVoid.Get();
}

void AMasterBedroomActor::Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots) const
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

UStaticMeshComponent* AMasterBedroomActor::Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
	std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	Dress(Mesh, Slots);
	return Mesh;
}

void AMasterBedroomActor::Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw)
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

void AMasterBedroomActor::Dust(FRoomBuilder& Build, const FVector& Top, const FVector2D& Size, float Yaw, float Opacity) const
{
	// Aimed down, a decal lays its first size along world Y and its second along X (09-27): Size is
	// given as (X, Y) in the piece's own frame and swapped here, the roll turning it with the piece.
	// The damp photograph's blotches read as camouflage on a white top at the full opacity and a
	// soft edge; most of it, torn harder at the edge, is a film of dust.
	Build.Stain(RoomSurfaces::Damp, Top + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, Yaw), FVector2D(Size.Y, Size.X), BedroomDust, Opacity * 0.8f, 1.8f);
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers.
// ---------------------------------------------------------------------------------------------

float AMasterBedroomActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector AMasterBedroomActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector AMasterBedroomActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

float AMasterBedroomActor::FacingYaw(EWall Wall)
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

TArray<FBox2D> AMasterBedroomActor::Openings(EWall Wall, float Trim) const
{
	TArray<FBox2D> Holes;
	if (Wall == EWall::South)
	{
		const float Half = Setup.DoorWidth * 0.5f + Trim;
		Holes.Add(FBox2D(FVector2D(Setup.DoorCenterX - Half, 0.f), FVector2D(Setup.DoorCenterX + Half, Setup.DoorHeight + Trim)));
	}
	else if (Wall == EWall::East)
	{
		const float Half = Setup.WindowWidth * 0.5f + Trim;
		Holes.Add(FBox2D(FVector2D(-Half, Setup.WindowSill - Trim), FVector2D(Half, Setup.WindowTop + Trim)));
	}
	return Holes;
}

void AMasterBedroomActor::Moulding(FRoomBuilder& Build, const TCHAR* Name, EWall Wall, float U, float Z, float Length, bool bVertical)
{
	// The moulding models are a metre along X with their profile in YZ, the wall at y = 0 and the
	// room at +Y. Laid along a wall, X runs along it and Z stays up; stood up, X runs up the wall.
	const FVector Normal = WallNormal(Wall);
	const FVector Up(0.f, 0.f, 1.f);
	const FRotator Rotation = bVertical
		? FRotationMatrix::MakeFromXY(Up, Normal).Rotator()
		: FRotationMatrix::MakeFromXY(Normal ^ Up, Normal).Rotator();
	if (UStaticMeshComponent* Mesh = Place(Build, Name, WallPoint(Wall, U, Z, 0.f), Rotation, { { TEXT("Paint"), MatPaint } }))
	{
		Mesh->SetRelativeScale3D(FVector(Length / 100.f, 1.f, 1.f));
	}
}

// ---------------------------------------------------------------------------------------------
// The shell's finish.
// ---------------------------------------------------------------------------------------------

void AMasterBedroomActor::BuildFloor(FRoomBuilder& Build)
{
	// Dark oak boards wall to wall, running towards the window: one box, collision on, its top at
	// zero. wood_floor's boards run along its V, which on a box's top face is local Y, so the box is
	// turned ninety and its local Y lies along world X. The floor has an instance of its own, made
	// straight from the asset with its repeats written out for each side (the music room's floor,
	// 10-24): through the builder the longer side's count would go on both.
	const float AlongBoards = EastX() - WestX();
	const float AcrossBoards = SouthY() - NorthY();
	// wood_floor measures linear (0.217, 0.117, 0.055), a honey oak: this takes it to a dark natural
	// oak at about (0.076, 0.044, 0.025), the dust taking the shine off its lacquer.
	const FLinearColor Tint(0.33f, 0.37f, 0.45f);
	const float RoughnessScale = 1.7f;
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr,
		*FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), RoomSurfaces::Oak.Set, RoomSurfaces::Oak.Set));
	MatFloor = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
	if (MatFloor)
	{
		MatFloor->SetVectorParameterValue(TEXT("Tint"), Tint);
		MatFloor->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
		MatFloor->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(AcrossBoards / RoomSurfaces::Oak.TexelSizeCm, AlongBoards / RoomSurfaces::Oak.TexelSizeCm, 0.f, 1.f));
		MatFloor->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	else
	{
		// The art pipeline has not been run: the builder's flat fallback.
		MatFloor = Build.Surface(RoomSurfaces::Oak, Tint, RoughnessScale);
	}
	Build.Box(FVector(0.f, 0.f, -2.f), FRotator(0.f, 90.f, 0.f), FVector(AcrossBoards, AlongBoards, 4.f), MatFloor);
}

void AMasterBedroomActor::BuildWalls(FRoomBuilder& Build)
{
	// White paint over the plaster, one sheet per wall cut round the door and the window.
	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Runs[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), SouthY() },
	};
	for (const FRun& Run : Runs)
	{
		const bool bAlongX = Run.Wall == EWall::North || Run.Wall == EWall::South;
		const TArray<FBox2D> Pieces = RoomWalls::CutAround(Openings(Run.Wall, 0.f), Run.U0, Run.U1, 0.f, Setup.Height);
		BedroomFacade(this, RoomRoot, Pieces, WallPoint(Run.Wall, 0.f, 0.f, 0.3f), bAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f),
			WallNormal(Run.Wall), RoomSurfaces::Paint.TexelSizeCm, MatWall);
	}

	// The openings' reveals, lined in the same white: the shell's own surface is a flat dark plate.
	// The window's reveal is the storm window's own; the door's is lined here, the full depth of the
	// wall, behind the leaf.
	const float Thick = Setup.WallThickness;
	const float RevealY = SouthY() + Thick * 0.5f;
	for (const float Side : { -1.f, 1.f })
	{
		Build.Box(FVector(Setup.DoorCenterX + Side * (Setup.DoorWidth * 0.5f - 0.6f), RevealY, Setup.DoorHeight * 0.5f), FRotator::ZeroRotator,
			FVector(1.2f, Thick, Setup.DoorHeight), MatPaintBox, false);
	}
	Build.Box(FVector(Setup.DoorCenterX, RevealY, Setup.DoorHeight - 0.6f), FRotator::ZeroRotator, FVector(Setup.DoorWidth, Thick, 1.2f), MatPaintBox, false);
}

void AMasterBedroomActor::BuildJoinery(FRoomBuilder& Build)
{
	// The Victorian part the renovation kept and painted: a moulded skirting, a dado rail, panels
	// laid out in bolection moulding above and below it, a plaster cornice. Every run is cut round
	// the door's and the window's architraves; nothing goes behind the built-in wardrobes.
	const float Casing = 9.f;
	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Runs[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), WardrobeStartY() },
	};
	// The U ranges of a wall left free over a band of height.
	auto FreeAlong = [&](const FRun& Run, float Z0, float Z1)
	{
		TArray<FVector2D> Free;
		for (const FBox2D& Piece : RoomWalls::CutAround(Openings(Run.Wall, Casing), Run.U0, Run.U1, Z0, Z1))
		{
			if (FMath::IsNearlyEqual(Piece.Min.Y, Z0, 0.5f) && FMath::IsNearlyEqual(Piece.Max.Y, Z1, 0.5f) && Piece.GetSize().X > 1.f)
			{
				Free.Add(FVector2D(Piece.Min.X, Piece.Max.X));
			}
		}
		return Free;
	};
	auto Run = [&](const TCHAR* Name, const FRun& R, float Z, float Height)
	{
		for (const FVector2D& Span : FreeAlong(R, Z, Z + Height))
		{
			const int32 Pieces = FMath::Max(1, FMath::CeilToInt((Span.Y - Span.X) / 100.f));
			const float Piece = (Span.Y - Span.X) / Pieces;
			for (int32 i = 0; i < Pieces; ++i)
			{
				Moulding(Build, Name, R.Wall, Span.X + Piece * (i + 0.5f), Z, Piece, false);
			}
		}
	};

	const float LowerZ0 = 34.f;
	const float LowerZ1 = 74.f;
	const float UpperZ0 = 118.f;
	const float UpperZ1 = Setup.Height - CorniceDrop - 14.f;
	const float Strip = 3.6f;
	for (const FRun& R : Runs)
	{
		Run(BedroomProps::Skirting, R, 0.f, 22.f);
		Run(BedroomProps::Dado, R, DadoZ, 7.f);

		// The panels: each free span divided into fields about a metre wide, ten in from its ends
		// and twelve apart. The moulding's profile stands to one side of where a vertical strip is
		// put (towards Up ^ Normal), so which edge a stile goes on is worked out per wall.
		const FVector UDir = (R.Wall == EWall::North || R.Wall == EWall::South) ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
		const bool bProfileAlongU = FVector::DotProduct(FVector(0.f, 0.f, 1.f) ^ WallNormal(R.Wall), UDir) > 0.f;
		for (const FVector2D Band : { FVector2D(LowerZ0, LowerZ1), FVector2D(UpperZ0, UpperZ1) })
		{
			for (const FVector2D& Span : FreeAlong(R, Band.X, Band.Y))
			{
				const float Length = Span.Y - Span.X;
				if (Length < 50.f)
				{
					continue;
				}
				const int32 Count = FMath::Max(1, FMath::RoundToInt((Length - 20.f) / 105.f));
				const float Width = (Length - 20.f - (Count - 1) * 12.f) / Count;
				for (int32 k = 0; k < Count; ++k)
				{
					const float A = Span.X + 10.f + k * (Width + 12.f);
					const float B = A + Width;
					Moulding(Build, BedroomProps::PanelMoulding, R.Wall, (A + B) * 0.5f, Band.X, Width, false);
					Moulding(Build, BedroomProps::PanelMoulding, R.Wall, (A + B) * 0.5f, Band.Y - Strip, Width, false);
					const float StileZ = (Band.X + Band.Y) * 0.5f;
					const float StileLength = Band.Y - Band.X - Strip * 2.f;
					Moulding(Build, BedroomProps::PanelMoulding, R.Wall, bProfileAlongU ? A : A + Strip, StileZ, StileLength, true);
					Moulding(Build, BedroomProps::PanelMoulding, R.Wall, bProfileAlongU ? B - Strip : B, StileZ, StileLength, true);
				}
			}
		}
	}

	// The architraves: the door's and the window's, flat and broad, as the renovation left them.
	const float DoorHalf = Setup.DoorWidth * 0.5f;
	for (const float Side : { -1.f, 1.f })
	{
		const FVector At = WallPoint(EWall::South, Setup.DoorCenterX + Side * (DoorHalf + Casing * 0.5f), Setup.DoorHeight * 0.5f, 1.1f);
		Build.Box(At, FRotator::ZeroRotator, FVector(Casing, 2.2f, Setup.DoorHeight), MatPaintBox, false);
	}
	Build.Box(WallPoint(EWall::South, Setup.DoorCenterX, Setup.DoorHeight + Casing * 0.5f, 1.1f), FRotator::ZeroRotator,
		FVector(Setup.DoorWidth + Casing * 2.f, 2.2f, Casing), MatPaintBox, false);
	// The window's: outside the storm window's own reveal lining, which stands 6 past the opening.
	const float WindowHalf = Setup.WindowWidth * 0.5f;
	const float WindowCasing = 7.f;
	for (const float Side : { -1.f, 1.f })
	{
		const float U = Side * (WindowHalf + 6.f + WindowCasing * 0.5f);
		Build.Box(WallPoint(EWall::East, U, (Setup.WindowSill + Setup.WindowTop + 6.f) * 0.5f, 1.1f), FRotator::ZeroRotator,
			FVector(2.2f, WindowCasing, Setup.WindowTop + 6.f - Setup.WindowSill), MatPaintBox, false);
	}
	Build.Box(WallPoint(EWall::East, 0.f, Setup.WindowTop + 6.f + 2.5f, 1.1f), FRotator::ZeroRotator,
		FVector(2.2f, Setup.WindowWidth + 12.f + WindowCasing * 2.f, 5.f), MatPaintBox, false);

	// The cornice: the library's plaster crown moulding, painted, laid end to end along each wall.
	// Along the west wall it stops at the wardrobes, which are built up to a bulkhead under the
	// ceiling instead.
	struct FCorniceRun { FVector Start; FVector Dir; float Length; float Yaw; };
	const float C = Setup.Height;
	const FCorniceRun Cornices[] = {
		{ FVector(WestX(), NorthY(), C), FVector(1.f, 0.f, 0.f), EastX() - WestX(), 0.f },
		{ FVector(EastX(), SouthY(), C), FVector(-1.f, 0.f, 0.f), EastX() - WestX(), 180.f },
		{ FVector(WestX(), WardrobeStartY(), C), FVector(0.f, -1.f, 0.f), WardrobeStartY() - NorthY(), -90.f },
		{ FVector(EastX(), NorthY(), C), FVector(0.f, 1.f, 0.f), SouthY() - NorthY(), 90.f },
	};
	for (const FCorniceRun& Each : Cornices)
	{
		const int32 Pieces = FMath::Max(1, FMath::CeilToInt(Each.Length / 100.f));
		const float Piece = Each.Length / Pieces;
		for (int32 i = 0; i < Pieces; ++i)
		{
			if (UStaticMeshComponent* Mesh = Place(Build, BedroomProps::Cornice, Each.Start + Each.Dir * (Piece * (i + 0.5f)), FRotator(0.f, Each.Yaw, 0.f),
				{ { TEXT("Trim"), MatPaint } }))
			{
				Mesh->SetRelativeScale3D(FVector(Piece / 100.f, 1.f, 1.f));
			}
		}
	}
}

void AMasterBedroomActor::BuildCeiling(FRoomBuilder& Build)
{
	const float C = Setup.Height;
	Build.Mark(FVector(0.f, 0.f, C - 1.f), FRotator(0.f, 0.f, 180.f), FVector2D(EastX() - WestX(), SouthY() - NorthY()), MatCeiling);

	// The flush light in the middle of the ceiling, off since the house was shut.
	Place(Build, BedroomProps::CeilingLight, FVector(0.f, 0.f, C - 1.f), FRotator::ZeroRotator,
		{ { TEXT("Plastic"), F(TEXT("Plastic")) }, { TEXT("Diffuser"), F(TEXT("Diffuser")) } });

	// Water through the ceiling, where the storm has been getting at the roof: over the window and
	// in two other places, a faint tide-mark with a darker one inside each. Aimed straight up. Every number drawn first.
	const FVector2D Leaks[4] = { FVector2D(290.f, -170.f), FVector2D(320.f, 190.f), FVector2D(-250.f, -230.f), FVector2D(30.f, 150.f) };
	for (const FVector2D& Leak : Leaks)
	{
		for (int32 Ring = 0; Ring < 2; ++Ring)
		{
			const float Size = Random.FRandRange(70.f, 120.f) * (1.f + Ring * 0.6f);
			const float Aspect = Random.FRandRange(0.7f, 1.3f);
			const float OffX = Random.FRandRange(-15.f, 15.f);
			const float OffY = Random.FRandRange(-15.f, 15.f);
			const float Roll = Random.FRandRange(0.f, 360.f);
			Build.Stain(RoomSurfaces::Damp, FVector(Leak.X + OffX, Leak.Y + OffY, C - 3.f), FRotator(90.f, 0.f, Roll),
				FVector2D(Size, Size * Aspect), Ring == 0 ? FLinearColor(0.30f, 0.28f, 0.24f) : FLinearColor(0.46f, 0.42f, 0.35f),
				Ring == 0 ? 0.45f : 0.22f, 1.1f + Ring * 0.15f);
		}
	}

	// A crack across the ceiling from the window's corner, the house settling under the new plaster.
	Build.Crack(FVector(250.f, -60.f, C - 2.f), FRotator(90.f, 0.f, 70.f), FVector2D(260.f, 90.f), 0.75f, 22.f);
	Build.Crack(FVector(-160.f, 90.f, C - 2.f), FRotator(90.f, 0.f, 15.f), FVector2D(180.f, 70.f), 0.6f, 24.f);

	// Webs in the four upper corners, under the cornice: years of them.
	const FVector2D Corners[4] = { FVector2D(EastX(), NorthY()), FVector2D(EastX(), SouthY()), FVector2D(WestX(), NorthY()), FVector2D(WestX(), SouthY()) };
	for (const FVector2D& Corner : Corners)
	{
		const float SX = Corner.X < 0.f ? 1.f : -1.f;
		const float SY = Corner.Y < 0.f ? 1.f : -1.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float In = 24.f + i * 18.f;
			UStaticMeshComponent* Web = Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SX * In, Corner.Y + SY * In, C - CorniceDrop - 8.f - i * 6.f),
				FRotator(0.f, SX * SY > 0.f ? 45.f : -45.f, 180.f), FVector(In * 1.5f, In * 1.5f, 1.f), MatWeb, false);
			BedroomNoDecals(Web);
		}
	}
}

void AMasterBedroomActor::BuildHook(FRoomBuilder& Build)
{
	// The hook, where it has always been: iron screwed up through the new ceiling into a joist, in
	// the middle of the room and of the view the detective wakes to. It does not belong in a room
	// done over in white, and that is the point of it; the plaster round it has cracked where it
	// was turned in. Built as the old one was (ARoomDressingActor): a shank with a thread, a bend
	// swept round fifteen segments in the room's YZ plane, drawn to a point.
	const FVector Anchor = HookAnchor();
	const float ShankLength = 8.5f;
	Build.Cyl(Anchor + FVector(0.f, 0.f, -ShankLength * 0.5f), FRotator::ZeroRotator, FVector(2.2f, 2.2f, ShankLength), MatIron, false);
	for (int32 Thread = 0; Thread < 4; ++Thread)
	{
		Build.Cyl(Anchor + FVector(0.f, 0.f, -1.6f - Thread * 1.5f), FRotator(0.f, 0.f, 4.f), FVector(3.1f, 3.1f, 0.55f), MatIron, false);
	}
	// A plate where it meets the plaster.
	Build.Cyl(Anchor + FVector(0.f, 0.f, -0.6f), FRotator::ZeroRotator, FVector(6.f, 6.f, 1.2f), MatIron, false);

	const float HookRadius = 5.4f;
	const FVector BendCentre = Anchor + FVector(0.f, HookRadius, -ShankLength);
	const int32 BendSegments = 15;
	const float BendSweep = 214.f;
	FVector Previous = Anchor + FVector(0.f, 0.f, -ShankLength);
	FVector Heading(0.f, 0.f, -1.f);
	float Thickness = 2.2f;
	for (int32 Step = 1; Step <= BendSegments; ++Step)
	{
		const float Along = Step / static_cast<float>(BendSegments);
		const float Sweep = FMath::DegreesToRadians(BendSweep * Along);
		const FVector Point = BendCentre + FVector(0.f, -FMath::Cos(Sweep), -FMath::Sin(Sweep)) * HookRadius;
		Heading = Point - Previous;
		Thickness = FMath::Lerp(2.2f, 1.f, Along);
		Build.Cyl(Previous + Heading * 0.5f, FRotationMatrix::MakeFromZ(Heading).Rotator(), FVector(Thickness, Thickness, Heading.Size() + 0.5f), MatIron, false);
		Build.Sph(Point, Thickness, MatIron);
		Previous = Point;
	}
	Build.Add(FRoomShapes::Cone(), Previous + Heading.GetSafeNormal() * 1.6f, FRotationMatrix::MakeFromZ(Heading).Rotator(),
		FVector(Thickness, Thickness, 3.4f), MatIron, false);

	// The plaster cracked round it, and rust bled into the paint.
	Build.Crack(Anchor + FVector(0.f, 0.f, -2.f), FRotator(90.f, 0.f, 30.f), FVector2D(46.f, 38.f), 0.8f, 20.f);
	Build.Stain(RoomSurfaces::Damp, Anchor + FVector(0.f, 0.f, -2.f), FRotator(90.f, 0.f, 24.f), FVector2D(14.f, 12.f),
		FLinearColor(0.150f, 0.064f, 0.030f), 0.6f, 1.f);
}

// ---------------------------------------------------------------------------------------------
// The bed, and what stands round it.
// ---------------------------------------------------------------------------------------------

void AMasterBedroomActor::BuildBed(FRoomBuilder& Build)
{
	const FVector Seat = BedSeat();
	Place(Build, BedroomProps::Bed, Seat, FRotator::ZeroRotator,
		{ { TEXT("Paint"), MatPaint }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Upholstery"), MatUpholstery },
		  { TEXT("Linen"), MatLinen }, { TEXT("Mattress"), MatMattress }, { TEXT("Throw"), MatThrow } });
	Blocker(Build, Seat + FVector(0.f, 4.f, 30.f), FVector(BedWidth, BedLength - 8.f, 60.f), 0.f);
	Blocker(Build, FVector(BedX, NorthY() + 5.f, 75.f), FVector(BedWidth + 22.f, 12.f, 150.f), 0.f);

	// The dust over the duvet, and the bedding gone the yellow of linen that has not been washed or
	// aired in years: two faint blooms where the light from the window fell on it.
	Dust(Build, FVector(BedX, Seat.Y + 30.f, MattressTop + 3.f), FVector2D(190.f, 150.f), 0.f, 0.32f);
	Build.Stain(RoomSurfaces::Damp, FVector(BedX + 40.f, Seat.Y + 50.f, MattressTop + 8.f), FRotator(-90.f, 0.f, 30.f), FVector2D(60.f, 80.f),
		FLinearColor(0.55f, 0.45f, 0.30f), 0.18f, 1.3f);

	// The artwork over the headboard.
	if (UStaticMeshComponent* Art = Place(Build, BedroomProps::Artwork, WallPoint(EWall::North, BedX, 210.f, 0.f), FRotator(0.f, FacingYaw(EWall::North), 0.f),
		{ { TEXT("Pictures"), MatPictures }, { TEXT("Oak"), MatOak }, { TEXT("Shadow"), F(TEXT("Shadow")) } }))
	{
		BedroomNoDecals(Art);
	}

	// The two nightstands. Hers, on the window side, carries the clue things (BuildClues); his has
	// the books he was reading.
	for (const int32 Side : { -1, 1 })
	{
		const FVector Stand = NightstandSeat(Side);
		Place(Build, BedroomProps::Nightstand, Stand, FRotator::ZeroRotator,
			{ { TEXT("Paint"), MatPaint }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Oak"), MatOak }, { TEXT("Metal"), F(TEXT("Metal")) } });
		Blocker(Build, Stand + FVector(0.f, 0.f, 29.f), FVector(52.f, 40.f, 58.f), 0.f);
		Dust(Build, Stand + FVector(0.f, 0.f, NightstandTop), FVector2D(52.f, 40.f), 0.f, 0.38f);
	}
	{
		const FVector Top = NightstandSeat(-1) + FVector(0.f, 0.f, NightstandTop);
		Place(Build, BedroomProps::Book, Top + FVector(-4.f, -2.f, 0.f), FRotator(0.f, 6.f, 0.f), { { TEXT("Cover"), F(TEXT("CoverA")) }, { TEXT("Pages"), F(TEXT("Pages")) } });
		Place(Build, BedroomProps::Book, Top + FVector(-3.f, -1.f, 3.f), FRotator(0.f, -9.f, 0.f), { { TEXT("Cover"), F(TEXT("CoverB")) }, { TEXT("Pages"), F(TEXT("Pages")) } });
	}

	// The bench at the foot, the clothes on it that were ironed and never put away.
	const FVector BenchAt = BenchSeat();
	Place(Build, BedroomProps::Bench, BenchAt, FRotator::ZeroRotator,
		{ { TEXT("Paint"), MatPaint }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Upholstery"), MatUpholstery } });
	Blocker(Build, BenchAt + FVector(0.f, 0.f, 23.f), FVector(140.f, 44.f, 46.f), 0.f);
	Place(Build, BedroomProps::FoldedClothes, BenchAt + FVector(-34.f, 1.f, BenchTop - 1.f), FRotator(0.f, 4.f, 0.f),
		{ { TEXT("Cloth1"), MatShirtBlue }, { TEXT("Cloth2"), MatShirt }, { TEXT("Cloth3"), MatKnit } });
	Dust(Build, BenchAt + FVector(0.f, 0.f, BenchTop), FVector2D(140.f, 44.f), 0.f, 0.3f);

	// The rug, under the lower two thirds of the bed and out past the bench.
	Build.Cloth(FVector(BedX, NorthY() + 175.f, 0.8f), FRotator::ZeroRotator, FVector2D(330.f, 250.f), 0.35f, 0.f, 4401, MatRug, 60.f, /*bWorn*/ false);
}

void AMasterBedroomActor::BuildWardrobes(FRoomBuilder& Build)
{
	// Four built-in modules along the west wall, floor to a bulkhead under the ceiling, their doors
	// facing the room. One has its right-hand door standing open on what is hanging inside.
	const FRotator Turn(0.f, FacingYaw(EWall::West), 0.f);
	for (int32 Module = 0; Module < 4; ++Module)
	{
		const bool bOpen = Module == OpenWardrobe;
		Place(Build, bOpen ? BedroomProps::WardrobeOpen : BedroomProps::Wardrobe, WardrobeSeat(Module), Turn,
			{ { TEXT("Paint"), MatPaint }, { TEXT("Inside"), MatInside }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Metal"), F(TEXT("Metal")) } });
	}
	const float RunMidY = (WardrobeStartY() + SouthY()) * 0.5f;
	const float RunLength = SouthY() - WardrobeStartY();
	Build.Box(FVector(WestX() + WardrobeDepth * 0.5f, RunMidY, (WardrobeHeight + Setup.Height) * 0.5f), FRotator::ZeroRotator,
		FVector(WardrobeDepth, RunLength, Setup.Height - WardrobeHeight), MatPaintBox, false);
	Blocker(Build, FVector(WestX() + WardrobeDepth * 0.5f, RunMidY, WardrobeHeight * 0.5f), FVector(WardrobeDepth + 4.f, RunLength, WardrobeHeight), 0.f);

	// The open module, worked in its own frame: X along the run (+X is world -Y), the front at +Y.
	USceneComponent* Inside = BedroomPivot(this, RoomRoot, WardrobeSeat(OpenWardrobe), Turn, TEXT("WardrobeOpen"));
	FRoomBuilder W(this, Inside);
	// The right-hand door, on its hinge at the module's right edge at the back of the leaf, opened
	// by a negative yaw (make_bedroom.py: wardrobe_door).
	const float OpenYaw = -64.f;
	if (UStaticMeshComponent* Door = W.Prop(BedroomProps::WardrobeDoor, FVector(WardrobeModule * 0.5f - 0.2f, WardrobeDepth * 0.5f - 2.2f, 0.f),
		FRotator(0.f, OpenYaw, 0.f), 0.f, false))
	{
		Dress(Door, { { TEXT("Paint"), MatPaint }, { TEXT("Metal"), F(TEXT("Metal")) } });
	}
	// The open leaf stands out into the room: a blocker along it.
	{
		const FVector Hinge(WardrobeModule * 0.5f - 0.2f, WardrobeDepth * 0.5f, 0.f);
		const FVector Leaf = FRotator(0.f, OpenYaw, 0.f).RotateVector(FVector(-WardrobeModule * 0.5f, 0.f, 0.f));
		const FVector Centre = Inside->GetRelativeTransform().TransformPosition(Hinge + Leaf * 0.5f) + FVector(0.f, 0.f, WardrobeHeight * 0.5f);
		Blocker(Build, Centre, FVector(WardrobeModule * 0.5f, 4.f, WardrobeHeight), Turn.Yaw + OpenYaw);
	}

	// What hangs behind it: two of his shirts and one of her dresses, fanned on the rail as clothes
	// settle, so the doorway shows their shape rather than their edges (the nursery's wardrobe).
	const float ShirtZ = BedroomRailZ + 1.2f - (BedroomShirtHook + 0.18f);
	Dress(W.Prop(BedroomProps::Shirt, FVector(10.f, BedroomRailY, ShirtZ), FRotator(0.f, 24.f, 0.f), 0.f, false),
		{ { TEXT("Wood"), F(TEXT("Hanger")) }, { TEXT("Iron"), F(TEXT("Metal")) }, { TEXT("Cloth"), MatShirt }, { TEXT("ClothC"), MatShirt } });
	Dress(W.Prop(BedroomProps::Shirt, FVector(22.f, BedroomRailY, ShirtZ), FRotator(0.f, 30.f, 0.f), 0.f, false),
		{ { TEXT("Wood"), F(TEXT("Hanger")) }, { TEXT("Iron"), F(TEXT("Metal")) }, { TEXT("Cloth"), MatShirtBlue }, { TEXT("ClothC"), MatShirtBlue } });
	if (UStaticMeshComponent* Gown = W.Prop(BedroomProps::Dress, FVector(36.f, BedroomRailY, BedroomRailZ + 1.2f), FRotator(0.f, 34.f, 0.f), 0.f, false))
	{
		// The girl's summer dress (make_nursery.py) at a woman's length.
		Gown->SetRelativeScale3D(FVector(1.22f, 1.22f, 1.3f));
		Dress(Gown, { { TEXT("Fabric"), MatDress }, { TEXT("Wood"), F(TEXT("Hanger")) }, { TEXT("Brass"), F(TEXT("Metal")) } });
	}
	// Two boxes of shoes on the low shelf, and dust along the open shelf's front.
	W.Box(FVector(18.f, 4.f, 36.f + 6.f), FRotator(0.f, 3.f, 0.f), FVector(30.f, 18.f, 12.f), F(TEXT("CoverC")), false);
	W.Box(FVector(34.f, 3.f, 36.f + 5.f), FRotator(0.f, -5.f, 0.f), FVector(26.f, 16.f, 10.f), F(TEXT("Black")), false);
}

void AMasterBedroomActor::BuildDresser(FRoomBuilder& Build)
{
	const FVector Seat = DresserSeat();
	Place(Build, BedroomProps::Dresser, Seat, FRotator::ZeroRotator,
		{ { TEXT("Paint"), MatPaint }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Metal"), F(TEXT("Metal")) } });
	Blocker(Build, Seat + FVector(0.f, 0.f, DresserTop * 0.5f), FVector(160.f, 48.f, DresserTop), 0.f);

	// On it, her things: a vase with nothing in it, the tray with her perfume, the jewellery box, his
	// watch where he took it off, her hairbrush, and a pile of ironing waiting to go in a drawer.
	const FVector Top(Seat.X, Seat.Y, DresserTop);
	if (UStaticMeshComponent* Vase = Build.PropSeated(RoomProps::Vase, Top + FVector(-62.f, -6.f, 0.f), FRotator(0.f, 40.f, 0.f), 30.f, false))
	{
		FRoomShapes::TintSlots(Vase, FLinearColor(0.55f, 0.55f, 0.55f));
	}
	const FVector TrayAt = Top + FVector(-25.f, -2.f, 0.f);
	Place(Build, BedroomProps::Tray, TrayAt, FRotator(0.f, 3.f, 0.f), { { TEXT("Brass"), F(TEXT("Brass")) }, { TEXT("Ceramic"), F(TEXT("Ceramic")) } });
	Place(Build, BedroomProps::Perfume, TrayAt + FVector(-2.f, 0.f, BedroomTrayBase), FRotator(0.f, 8.f, 0.f),
		{ { TEXT("Glass"), MatGlass }, { TEXT("Liquid"), F(TEXT("Liquid")) }, { TEXT("Metal"), F(TEXT("Metal")) }, { TEXT("Black"), F(TEXT("Black")) } });
	Place(Build, BedroomProps::JewelryBox, Top + FVector(13.f, -7.f, 0.f), FRotator(0.f, -8.f, 0.f),
		{ { TEXT("Leather"), F(TEXT("Leather")) }, { TEXT("Velvet"), F(TEXT("Velvet")) }, { TEXT("Brass"), F(TEXT("Brass")) } });
	Place(Build, BedroomProps::Watch, Top + FVector(8.f, 13.f, 0.f), FRotator(0.f, 70.f, 0.f),
		{ { TEXT("Metal"), F(TEXT("Metal")) }, { TEXT("Face"), F(TEXT("Face")) }, { TEXT("Leather"), F(TEXT("Leather")) } });
	Place(Build, BedroomProps::Hairbrush, Top + FVector(37.f, 8.f, 0.f), FRotator(0.f, 64.f, 0.f), { { TEXT("Black"), F(TEXT("Black")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } });
	Place(Build, BedroomProps::FoldedClothes, Top + FVector(61.f, -4.f, 0.f), FRotator(0.f, -3.f, 0.f),
		{ { TEXT("Cloth1"), MatKnit }, { TEXT("Cloth2"), MatShirt }, { TEXT("Cloth3"), MatThrow } });
	Dust(Build, Top, FVector2D(160.f, 48.f), 0.f, 0.42f);

	// The clock over it, hung a little crooked: pitch turns a thing in the plane of this wall.
	if (UStaticMeshComponent* Clock = Place(Build, BedroomProps::Clock, WallPoint(EWall::North, Seat.X, 186.f, 0.f), FRotator(4.f, FacingYaw(EWall::North), 0.f),
		{ { TEXT("Brass"), F(TEXT("Brass")) }, { TEXT("Face"), F(TEXT("Face")) } }))
	{
		BedroomNoDecals(Clock);
	}
}

void AMasterBedroomActor::BuildTelevision(FRoomBuilder& Build)
{
	// Across from the bed: the white set on the wall, the floating cabinet under it.
	const FRotator Turn(0.f, FacingYaw(EWall::South), 0.f);
	const FVector Cabinet = MediaSeat();
	Place(Build, BedroomProps::Media, Cabinet, Turn, { { TEXT("Paint"), MatPaint }, { TEXT("Shadow"), F(TEXT("Shadow")) } });
	Blocker(Build, Cabinet + FVector(0.f, 0.f, MediaHeight * 0.5f), FVector(190.f, 40.f, MediaHeight), 0.f);
	if (UStaticMeshComponent* Set = Place(Build, BedroomProps::Television, TelevisionSeat(), Turn,
		{ { TEXT("Plastic"), F(TEXT("Plastic")) }, { TEXT("Screen"), F(TEXT("Screen")) }, { TEXT("Metal"), F(TEXT("Metal")) } }))
	{
		BedroomNoDecals(Set);
	}

	// On the cabinet: the remote where it was put down, the two small sculptures, and the
	// photograph of the three of them on the beach.
	const FVector Top = Cabinet + FVector(0.f, 0.f, MediaHeight);
	Place(Build, BedroomProps::Remote, Top + FVector(46.f, -6.f, 0.f), FRotator(0.f, 168.f, 0.f), { { TEXT("Black"), F(TEXT("Black")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } });
	Place(Build, BedroomProps::Sculptures, Top + FVector(-58.f, 4.f, 0.f), Turn, { { TEXT("Ceramic"), F(TEXT("Ceramic")) }, { TEXT("Stone"), F(TEXT("Stone")) } });
	Place(Build, BedroomProps::FrameFamily, Top + FVector(78.f, 6.f, 0.f), FRotator(0.f, Turn.Yaw + 14.f, 0.f),
		{ { TEXT("Frame"), F(TEXT("Frame")) }, { TEXT("Glass"), MatGlass }, { TEXT("Pictures"), MatPictures }, { TEXT("Black"), F(TEXT("Black")) } });
	Dust(Build, Top, FVector2D(190.f, 40.f), 0.f, 0.42f);
}

void AMasterBedroomActor::BuildMirror(FRoomBuilder& Build)
{
	// The big black mirror beside the door, leaning on the wall as a floor mirror stands. Its own
	// frame puts the glass facing +Y with up +Z; here its X runs to world -X, so it faces into the
	// room, and its top leans back until it touches the plaster. The lean is the angle whose sine is
	// the foot's distance from the wall over the mirror's height.
	const float MirrorHeight = 204.f;
	const FVector Foot = MirrorSeat();
	const float Lean = FMath::Asin((SouthY() - Foot.Y - 0.6f) / MirrorHeight);
	const FVector Up(0.f, FMath::Sin(Lean), FMath::Cos(Lean));
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(FVector(-1.f, 0.f, 0.f), Up).Rotator();
	if (UStaticMeshComponent* Mesh = Place(Build, BedroomProps::Mirror, Foot, Rotation,
		{ { TEXT("Frame"), F(TEXT("Frame")) }, { TEXT("Mirror"), F(TEXT("Mirror")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } }))
	{
		// No wall decal reaches it, and the dust on it is its own (below).
		BedroomNoDecals(Mesh);
	}
	Blocker(Build, FVector(Foot.X, (Foot.Y + SouthY()) * 0.5f, 100.f), FVector(92.f, SouthY() - Foot.Y + 4.f, 200.f), 0.f);

	// A film of dust over the glass, thinner towards the top where it leans out of the fall of it.
	// Aimed into the glass along its back-facing normal.
	const FVector Front = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
	const FRotator Into = FRotationMatrix::MakeFromX(-Front).Rotator();
	for (int32 i = 0; i < 3; ++i)
	{
		const float Height = 50.f + i * 55.f;
		const FVector At = Foot + Up * Height + Front * 6.f;
		Build.Stain(RoomSurfaces::Damp, At, Into, FVector2D(78.f, 70.f), BedroomDust, 0.22f - i * 0.05f, 1.3f);
	}
}

void AMasterBedroomActor::BuildSittingCorner(FRoomBuilder& Build)
{
	// The white armchair by the window, turned to the storm, the ottoman in front of it with a
	// blanket folded on it, the side table with her coffee and two books, the lamp behind.
	const FRotator Turn(0.f, ArmchairYaw, 0.f);
	Place(Build, BedroomProps::Armchair, ArmchairSeat(), Turn, { { TEXT("Upholstery"), MatUpholstery }, { TEXT("Oak"), MatOak } });
	Blocker(Build, ArmchairSeat() + FVector(0.f, 0.f, 40.f), FVector(94.f, 90.f, 80.f), Turn.Yaw);
	Dust(Build, ArmchairSeat() + FVector(0.f, 0.f, 50.f), FVector2D(80.f, 80.f), Turn.Yaw, 0.3f);

	Place(Build, BedroomProps::Ottoman, OttomanSeat(), Turn, { { TEXT("Upholstery"), MatUpholstery }, { TEXT("Oak"), MatOak } });
	Blocker(Build, OttomanSeat() + FVector(0.f, 0.f, 21.f), FVector(64.f, 48.f, 42.f), Turn.Yaw);
	Place(Build, BedroomProps::FoldedBlanket, OttomanSeat() + FVector(0.f, 0.f, 42.5f), FRotator(0.f, Turn.Yaw + 6.f, 0.f), { { TEXT("Throw"), MatThrow } });

	const FVector Table = SideTableSeat();
	Place(Build, BedroomProps::SideTable, Table, FRotator(0.f, 10.f, 0.f), { { TEXT("Oak"), MatOak } });
	Blocker(Build, Table + FVector(0.f, 0.f, SideTableTop * 0.5f), FVector(40.f, 40.f, SideTableTop), 0.f);
	const FVector Top = Table + FVector(0.f, 0.f, SideTableTop);
	Place(Build, BedroomProps::Mug, Top + FVector(-8.f, -7.f, 0.f), FRotator(0.f, 210.f, 0.f), { { TEXT("Ceramic"), F(TEXT("Ceramic")) }, { TEXT("Soil"), F(TEXT("Soil")) } });
	Place(Build, BedroomProps::Book, Top + FVector(5.f, 5.f, 0.f), FRotator(0.f, 28.f, 0.f), { { TEXT("Cover"), F(TEXT("CoverC")) }, { TEXT("Pages"), F(TEXT("Pages")) } });
	Place(Build, BedroomProps::Book, Top + FVector(6.f, 4.f, 3.f), FRotator(0.f, 41.f, 0.f), { { TEXT("Cover"), F(TEXT("CoverD")) }, { TEXT("Pages"), F(TEXT("Pages")) } });
	Dust(Build, Top, FVector2D(46.f, 46.f), 0.f, 0.4f);

	Place(Build, BedroomProps::FloorLamp, FloorLampSeat(), FRotator(0.f, 20.f, 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Brass"), F(TEXT("Brass")) }, { TEXT("Shade"), MatShade } });
	Blocker(Build, FloorLampSeat() + FVector(0.f, 0.f, 80.f), FVector(36.f, 36.f, 160.f), 0.f);
}

void AMasterBedroomActor::BuildFloorThings(FRoomBuilder& Build)
{
	// The plant that died by the window, its dropped leaves round it.
	Place(Build, BedroomProps::Plant, PlantSeat(), FRotator(0.f, 25.f, 0.f),
		{ { TEXT("Ceramic"), F(TEXT("Ceramic")) }, { TEXT("Soil"), F(TEXT("Soil")) }, { TEXT("Stem"), F(TEXT("Stem")) }, { TEXT("Leaf"), F(TEXT("Leaf")) } });
	Blocker(Build, PlantSeat() + FVector(0.f, 0.f, 22.f), FVector(42.f, 42.f, 44.f), 0.f);

	// Books by his side of the bed, on the floor, the way a pile grows next to a bed.
	const FVector Pile = NightstandSeat(-1) + FVector(-34.f, 52.f, 0.f);
	const TCHAR* Covers[3] = { TEXT("CoverD"), TEXT("CoverB"), TEXT("CoverC") };
	const float Turns[3] = { 12.f, -6.f, 21.f };
	for (int32 i = 0; i < 3; ++i)
	{
		Place(Build, BedroomProps::Book, Pile + FVector(i * 0.8f, -i * 0.6f, i * 3.f), FRotator(0.f, Turns[i], 0.f),
			{ { TEXT("Cover"), F(Covers[i]) }, { TEXT("Pages"), F(TEXT("Pages")) } });
	}
}

void AMasterBedroomActor::BuildDamage(FRoomBuilder& Build)
{
	// Damage kept small, as the brief has it: abandoned, not destroyed. Mould round the window where
	// the storm has been coming in through the broken panes, a few places the paint has blistered
	// off the plaster, and grime on the boards along the walls. Every number is drawn into locals
	// in order before anything is laid (09-29).

	// Mould: dark blooms at the window's corners and under the sill, on the east wall, aimed into it.
	const float Half = Setup.WindowWidth * 0.5f;
	const FVector2D Mould[] = {
		FVector2D(-Half - 26.f, Setup.WindowTop + 12.f), FVector2D(Half + 30.f, Setup.WindowTop + 8.f),
		FVector2D(-Half - 20.f, Setup.WindowSill - 18.f), FVector2D(Half + 18.f, Setup.WindowSill - 30.f),
		FVector2D(-Half + 30.f, Setup.WindowSill - 40.f),
	};
	for (const FVector2D& Spot : Mould)
	{
		const float Size = Random.FRandRange(40.f, 75.f);
		const float Aspect = Random.FRandRange(0.6f, 1.2f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		// Faint: at 0.45-0.7 and near black they read as black camouflage patches on the white.
		const float Opacity = Random.FRandRange(0.18f, 0.3f);
		Build.Stain(RoomSurfaces::Damp, WallPoint(EWall::East, Spot.X, Spot.Y, 2.f), FRotator(0.f, 0.f, Roll),
			FVector2D(Size, Size * Aspect), FLinearColor(0.16f, 0.17f, 0.14f), Opacity, 1.3f);
	}

	// Paint blistered off the plaster: the brick-and-render photograph under a torn edge.
	struct FPeel { EWall Wall; float U; float Z; float Size; };
	const FPeel Peels[] = {
		{ EWall::East, -Half - 60.f, 205.f, 34.f },
		{ EWall::East, Half + 52.f, 150.f, 26.f },
		{ EWall::North, 345.f, 262.f, 30.f },
	};
	for (const FPeel& Peel : Peels)
	{
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float Aspect = Random.FRandRange(0.7f, 1.3f);
		const FRotator Into = Peel.Wall == EWall::East ? FRotator(0.f, 0.f, Roll) : FRotator(0.f, -90.f, Roll);
		Build.Stain(RoomSurfaces::Substrate, WallPoint(Peel.Wall, Peel.U, Peel.Z, 2.f), Into, FVector2D(Peel.Size, Peel.Size * Aspect),
			// The plaster under paint is a mid grey, not a hole: dark, the patches read as burns.
			FLinearColor(0.50f, 0.47f, 0.43f), 0.6f, 1.6f);
	}
	// And the hairline cracks a settling house puts in new plaster, from the window's head.
	{
		const float Roll = Random.FRandRange(-20.f, 20.f);
		Build.Crack(WallPoint(EWall::East, Half + 20.f, Setup.WindowTop + 14.f, 2.f), FRotator(0.f, 0.f, 60.f + Roll), FVector2D(70.f, 30.f), 0.8f, 24.f);
	}

	// Grime: years of shut-up air on the paint, a grey-brown wash heavier high up and in the corners.
	// Every patch is on a stretch of wall nothing stands against within a decal's reach (nine either
	// way, from two off the face): above the furniture, beside the window, east of the door with the
	// doorway and its architrave kept fifteen clear. What hangs over them (the canvas, the clock,
	// the television, the mirror) takes no decals. Numbers drawn into locals first.
	struct FWash { EWall Wall; float U; float Z; float Size; float Opacity; };
	const float DoorEast = Setup.DoorCenterX + Setup.DoorWidth * 0.5f + 9.f + 15.f;
	const FWash Washes[] = {
		{ EWall::North, -330.f, 250.f, 110.f, 0.30f }, { EWall::North, -90.f, 275.f, 150.f, 0.22f },
		{ EWall::North, 120.f, 250.f, 120.f, 0.26f }, { EWall::North, 330.f, 200.f, 140.f, 0.34f },
		{ EWall::North, 215.f, 150.f, 90.f, 0.18f }, { EWall::North, 50.f, 125.f, 60.f, 0.2f },
		{ EWall::East, -250.f, 150.f, 130.f, 0.3f }, { EWall::East, -200.f, 270.f, 110.f, 0.32f },
		{ EWall::East, 250.f, 260.f, 120.f, 0.3f }, { EWall::East, 230.f, 140.f, 100.f, 0.22f },
		{ EWall::South, DoorEast + 52.f, 150.f, 100.f, 0.28f }, { EWall::South, DoorEast + 62.f, 260.f, 90.f, 0.3f },
		{ EWall::South, -90.f, 255.f, 160.f, 0.22f }, { EWall::South, -250.f, 250.f, 100.f, 0.28f },
		{ EWall::West, -230.f, 240.f, 120.f, 0.3f }, { EWall::West, -200.f, 130.f, 90.f, 0.22f },
	};
	for (const FWash& Wash : Washes)
	{
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float Aspect = Random.FRandRange(0.6f, 1.1f);
		// A decal projects along its +X: into the wall is against the wall's normal.
		const FRotator Into(0.f, FacingYaw(Wash.Wall) - 90.f, Roll);
		Build.Stain(RoomSurfaces::Damp, WallPoint(Wash.Wall, Wash.U, Wash.Z, 2.f), Into, FVector2D(Wash.Size, Wash.Size * Aspect),
			FLinearColor(0.30f, 0.27f, 0.21f), Wash.Opacity, 1.3f);
	}
	// Hairline cracks in the plaster, from the corners and the cornice down.
	struct FHairline { EWall Wall; float U; float Z; float Length; };
	const FHairline Hairlines[] = {
		{ EWall::North, -370.f, 230.f, 90.f }, { EWall::North, 365.f, 120.f, 70.f },
		{ EWall::South, 360.f, 220.f, 80.f }, { EWall::West, -290.f, 200.f, 70.f },
		{ EWall::East, -290.f, 220.f, 90.f },
	};
	for (const FHairline& Hairline : Hairlines)
	{
		const float Roll = Random.FRandRange(50.f, 130.f);
		Build.Crack(WallPoint(Hairline.Wall, Hairline.U, Hairline.Z, 2.f), FRotator(0.f, FacingYaw(Hairline.Wall) - 90.f, Roll),
			FVector2D(Hairline.Length, Hairline.Length * 0.4f), 0.7f, 26.f);
	}

	// Grime along the walls where it collects, and the stain of rainwater under the window, on the
	// boards. Aimed down from four over them (the 09-29 rule). None near the door: its leaf sweeps
	// the boards on the hinge side of the doorway.
	struct FDrift { FVector2D At; FVector2D Size; float Roll; float Opacity; };
	const FDrift Drifts[] = {
		{ FVector2D(EastX() - 30.f, -40.f), FVector2D(110.f, 60.f), 0.f, 0.5f },
		{ FVector2D(EastX() - 26.f, -230.f), FVector2D(90.f, 40.f), 0.f, 0.35f },
		{ FVector2D(-10.f, SouthY() - 22.f), FVector2D(40.f, 120.f), 0.f, 0.3f },
		{ FVector2D(330.f, SouthY() - 26.f), FVector2D(40.f, 80.f), 0.f, 0.3f },
		{ FVector2D(-150.f, -40.f), FVector2D(160.f, 120.f), 0.f, 0.18f },
		// A film over the open floor: the oak has not been swept or walked on since.
		{ FVector2D(60.f, -10.f), FVector2D(220.f, 180.f), 40.f, 0.22f },
		{ FVector2D(150.f, -200.f), FVector2D(180.f, 140.f), 10.f, 0.2f },
		{ FVector2D(-120.f, 190.f), FVector2D(200.f, 150.f), 70.f, 0.2f },
		{ FVector2D(-330.f, -200.f), FVector2D(110.f, 60.f), 0.f, 0.32f },
		{ FVector2D(-260.f, 280.f), FVector2D(60.f, 140.f), 0.f, 0.3f },
	};
	for (const FDrift& Drift : Drifts)
	{
		const float Roll = Drift.Roll + Random.FRandRange(-10.f, 10.f);
		Build.Stain(RoomSurfaces::Damp, FVector(Drift.At.X, Drift.At.Y, BedroomFloorDecalZ), FRotator(-90.f, 0.f, Roll), Drift.Size,
			BedroomDust, Drift.Opacity, 1.35f);
	}
	// The water that came in through the broken panes and dried, a tide-mark on the oak.
	Build.Stain(RoomSurfaces::Damp, FVector(EastX() - 40.f, -55.f, BedroomFloorDecalZ), FRotator(-90.f, 0.f, 15.f), FVector2D(70.f, 50.f),
		FLinearColor(0.20f, 0.18f, 0.15f), 0.45f, 1.2f);
}

// ---------------------------------------------------------------------------------------------
// Where the clues will go.
// ---------------------------------------------------------------------------------------------

AClueActor* AMasterBedroomActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
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

void AMasterBedroomActor::BuildClues()
{
	// Set dressing only, as everywhere in the house until the game is built end to end: no text, no
	// prompt (the 09-30 decision). What says the owners left without meaning to: her phone on her
	// nightstand, the novel open face down where she stopped, the wedding photograph beside it, and
	// the suitcase packed and left open on the floor.
	const FVector Stand = NightstandSeat(1) + FVector(0.f, 0.f, NightstandTop);
	if (AClueActor* Clue = SpawnClue(Stand + FVector(-14.f, 8.f, 0.f), FRotator(0.f, 12.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(BedroomProps::Phone, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false),
			{ { TEXT("Metal"), F(TEXT("Black")) }, { TEXT("Screen"), F(TEXT("Screen")) } });
	}
	if (AClueActor* Clue = SpawnClue(Stand + FVector(9.f, 6.f, 0.f), FRotator(0.f, -14.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(BedroomProps::Novel, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false),
			{ { TEXT("Cover"), F(TEXT("CoverC")) }, { TEXT("Pages"), F(TEXT("Pages")) } });
		// Her reading glasses folded on top of it.
		Dress(B.Prop(BedroomProps::Glasses, FVector(1.f, -1.f, 3.3f), FRotator(0.f, 30.f, 0.f), 0.f, false),
			{ { TEXT("Black"), F(TEXT("Black")) }, { TEXT("Glass"), MatGlass } });
	}
	if (AClueActor* Clue = SpawnClue(Stand + FVector(-6.f, -12.f, 0.f), FRotator(0.f, -16.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(BedroomProps::FrameWedding, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false),
			{ { TEXT("Metal"), F(TEXT("Metal")) }, { TEXT("Glass"), MatGlass }, { TEXT("Pictures"), MatPictures }, { TEXT("Black"), F(TEXT("Black")) } });
	}

	// The suitcase, in front of the wardrobes, its lid back.
	const FVector CaseAt(-262.f, 32.f, 0.f);
	const FRotator CaseTurn(0.f, 78.f, 0.f);
	if (AClueActor* Clue = SpawnClue(CaseAt, CaseTurn))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(BedroomProps::Suitcase, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false),
			{ { TEXT("Black"), F(TEXT("Black")) }, { TEXT("Lining"), MatLining }, { TEXT("Metal"), F(TEXT("Metal")) },
			  { TEXT("Cloth1"), MatShirtBlue }, { TEXT("Cloth2"), MatShirt }, { TEXT("Cloth3"), MatKnit } });
	}
	FRoomBuilder Build(this, RoomRoot);
	Blocker(Build, CaseAt + FVector(0.f, 0.f, 12.f), FVector(72.f, 94.f, 24.f), CaseTurn.Yaw);
}
