#include "LibraryActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "StormWindowActor.h"
#include "DustMotesComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Algo/Find.h"

namespace
{
	/** The generated models (Tools/make_library.py), /Game/Meshes/<name>. */
	namespace LibraryProps
	{
		const TCHAR* Bookcase = TEXT("library_bookcase");
		const TCHAR* BookRows[10] = {
			TEXT("library_books_a"), TEXT("library_books_b"), TEXT("library_books_c"), TEXT("library_books_d"), TEXT("library_books_e"),
			TEXT("library_books_f"), TEXT("library_books_g"), TEXT("library_books_h"), TEXT("library_books_i"), TEXT("library_books_j") };
		const TCHAR* BooksLeaning = TEXT("library_books_leaning");
		const TCHAR* BookPiles[2] = { TEXT("library_books_pile_a"), TEXT("library_books_pile_b") };
		const TCHAR* Arch = TEXT("library_arch");
		const TCHAR* WindowArch = TEXT("library_window_arch");
		const TCHAR* Cornice = TEXT("library_cornice");
		const TCHAR* Fireplace = TEXT("library_fireplace");
		const TCHAR* FireTools = TEXT("library_fire_tools");
		const TCHAR* Desk = TEXT("library_desk");
		const TCHAR* DeskDrawer = TEXT("library_desk_drawer");
		const TCHAR* DeskChair = TEXT("library_desk_chair");
		const TCHAR* BankersLamp = TEXT("library_bankers_lamp");
		const TCHAR* InkBottle = TEXT("library_ink_bottle");
		const TCHAR* Pen = TEXT("library_pen");
		const TCHAR* Magnifier = TEXT("library_magnifier");
		const TCHAR* Glasses = TEXT("library_glasses");
		const TCHAR* Blotter = TEXT("library_blotter");
		const TCHAR* Seal = TEXT("library_seal");
		const TCHAR* Laptop = TEXT("library_laptop");
		const TCHAR* Key = TEXT("library_key");
		const TCHAR* Mug = TEXT("library_mug");
		const TCHAR* OpenBook = TEXT("library_open_book");
		const TCHAR* Notebook = TEXT("library_notebook");
		const TCHAR* Letter = TEXT("library_letter");
		const TCHAR* Newspaper = TEXT("library_newspaper");
		const TCHAR* SideTable = TEXT("library_side_table");
		const TCHAR* FloorLamp = TEXT("library_floor_lamp");
		const TCHAR* Globe = TEXT("library_globe");
		const TCHAR* Gramophone = TEXT("library_gramophone");
		const TCHAR* RecordCabinet = TEXT("library_record_cabinet");
		const TCHAR* ChessTable = TEXT("library_chess_table");
		const TCHAR* SideChair = TEXT("library_side_chair");
		const TCHAR* Pedestal = TEXT("library_pedestal");
		const TCHAR* FilingCabinet = TEXT("library_filing_cabinet");
		const TCHAR* FilingDrawer = TEXT("library_filing_drawer");
		const TCHAR* Vitrine = TEXT("library_vitrine");
		const TCHAR* Ladder = TEXT("library_ladder");
		const TCHAR* FrameMap = TEXT("library_frame_map");
		const TCHAR* FramePortrait = TEXT("library_frame_portrait");
		const TCHAR* FrameSmall = TEXT("library_frame_small");
		const TCHAR* Rug = TEXT("library_rug");
		const TCHAR* Valance = TEXT("library_valance");
	}

	/**
	 * The drapes' crimson velvet (Tools/make_library_art.py), tiling at the drapes' 34cm like the
	 * girl's rose print, so the generated curtains' UVs fit it as they are. Measured at linear
	 * (0.137, 0.004, 0.005).
	 */
	const FRoomSurface LibraryVelvet{ TEXT("library_velvet"), 34.f };
	const FLinearColor LibraryVelvetTint(0.55f, 2.0f, 1.7f);

	/** The rectangles of the library_paper atlas (Tools/make_library_art.py: PAPER), in pixels. */
	struct FLibraryRect
	{
		const TCHAR* Name;
		float X, Y, W, H;
	};
	const FLibraryRect LibraryPaperRects[] = {
		{ TEXT("notes_a"), 0.f, 512.f, 362.f, 512.f },
		{ TEXT("notes_b"), 512.f, 512.f, 362.f, 512.f },
		{ TEXT("letter"), 1024.f, 512.f, 362.f, 512.f },
		{ TEXT("envelope"), 1536.f, 512.f, 512.f, 300.f },
		{ TEXT("calendar"), 1024.f, 1024.f, 366.f, 512.f },
		{ TEXT("card"), 1536.f, 1024.f, 512.f, 320.f },
	};

	/** The pictures: the two maps on library_maps (2048), the two portraits on library_paintings (1024). */
	const FVector4f LibraryWorldMap(0.f, 0.f, 1024.f, 1024.f);
	const FVector4f LibraryParishMap(1024.f, 0.f, 1024.f, 1024.f);
	const FVector4f LibraryMotherAndDaughter(0.f, 0.f, 512.f, 640.f);
	const FVector4f LibraryOldMan(512.f, 0.f, 512.f, 640.f);

	/** The house's dust, the colour it is everywhere. */
	const FLinearColor LibraryDust(0.42f, 0.40f, 0.36f);

	/** For a model's own UVs, already in repeats of the slot's texel size: tiling at one. */
	void LibrarySheet(UMaterialInstanceDynamic* Mat)
	{
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}

	/** Keeps the walls' decals off things pinned or hung on them (the kitchen calendar's note). */
	void LibraryNoDecals(UPrimitiveComponent* Part)
	{
		if (Part)
		{
			Part->SetReceivesDecals(false);
		}
	}
}

ALibraryActor::ALibraryActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);
	RoomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void ALibraryActor::Configure(const FLibrarySetup& InSetup, AStormWindowActor* InLeadStorm)
{
	Setup = InSetup;
	LeadStorm = InLeadStorm;

	// Here and not in BeginPlay: the motes are scattered in the component's own BeginPlay, which runs
	// before the owner's (see ARoomDressingActor::Configure).
	DustMotes->ConfigureVolume(
		FVector((EastX() - WestX()) * 0.47f, (SouthY() - NorthY()) * 0.47f, RoomHeight * 0.46f),
		FVector(MidX(), MidY(), RoomHeight * 0.5f));

	Openings = { { EWall::North, Setup.ArchX, ArchHalf, 0.f, ArchCrown } };
	for (const float X : WindowXs)
	{
		Openings.Add({ EWall::South, X, WindowHalf, WindowSill, WindowTop });
	}

	// The bookcases: three walls of them, each corner taken by one run and the run on the other wall
	// starting at its front. The north wall is cut by the arch, the east by the chimney breast; the
	// last stretch of the east wall, by the filing cabinet and the calendar, is left panelled.
	const float ArchClear = ArchHalf + ArchArchitrave + 4.f;
	ShelfRuns = {
		{ EWall::North, WestX(), Setup.ArchX - ArchClear },
		{ EWall::North, Setup.ArchX + ArchClear, EastX() },
		{ EWall::West, NorthY() + LowerDepth, SouthY() },
		{ EWall::East, NorthY() + LowerDepth, BreastY - BreastHalf - 4.f },
		{ EWall::East, BreastY + BreastHalf + 4.f, 1565.f },
	};
}

void ALibraryActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning this room never relays the corridor's or the girl's room's.
	Random.Initialize(19590311);

	FRoomBuilder Build(this, RoomRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildFloor(Build);
	BuildWalls(Build);
	BuildWindows(Build);
	BuildShelves(Build);
	BuildFireplace(Build);
	BuildDesk(Build);
	BuildReadingCorner(Build);
	BuildDecor(Build);
	BuildCeiling(Build);
	BuildDamage(Build);

	SpawnWindows();
	BuildClues();
}

// ---------------------------------------------------------------------------------------------
// Materials. The house's rule holds: nothing much above a tenth, and warm, because the only fill in
// here is the cold light of three windows. What changes is the wood: this is the one room in the
// house that was furnished to be looked at, and it is dark polished oak and mahogany, not paint.
// ---------------------------------------------------------------------------------------------

void ALibraryActor::CacheMaterials(FRoomBuilder& Build)
{
	// A sheet instance per surface for the generated models' own UVs, a hair off any tint the
	// builder's boxes use so the cache hands back a separate instance (the living room's rule).
	auto Sheet = [&](const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale = 1.f)
	{
		UMaterialInstanceDynamic* Mat = Build.Surface(Set, Tint + FLinearColor(0.0007f, 0.f, 0.f, 0.f), RoughnessScale);
		LibrarySheet(Mat);
		return Mat;
	};
	auto Atlas = [&](const TCHAR* Set, const FLinearColor& Tint, float RoughnessScale)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), Set, Set));
		UMaterialInstanceDynamic* Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("Tint"), Tint);
			Mat->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
			LibrarySheet(Mat);
		}
		return Mat;
	};

	// dark_paneled_wood at the hall's and the living room's values: one joiner panelled the house.
	MatPanel = Build.Surface(RoomSurfaces::Wainscot, FLinearColor(2.1f, 3.3f, 3.5f));
	MatPanelSheet = Sheet(RoomSurfaces::Wainscot, FLinearColor(2.1f, 3.3f, 3.5f));
	MatPlaster = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f));
	// The corridor's own plaster and trim, on the corridor face of the arch.
	MatCorridorPlaster = Sheet(RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f));
	// A tall ceiling a long way from three windows: darker than the living room's.
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.17f, 0.16f, 0.15f));
	// herringbone_parquet (0.294, 0.163, 0.075) brought down past the living room's to a dark oak
	// floor, and rough with sixty years of dust: it was shot freshly lacquered (the 09-27 note).
	MatParquet = Build.Surface(RoomSurfaces::Parquet, FLinearColor(0.13f, 0.135f, 0.16f), 6.4f);
	// black_oak_veneer is polished pale veneer (the wine cellar's racks): taken down to a warm dark oak
	// and roughened, or every upright of every bookcase mirrors the lantern.
	MatOak = Sheet(RoomSurfaces::DarkOak, FLinearColor(0.16f, 0.12f, 0.095f), 2.2f);
	MatOakBox = Build.Surface(RoomSurfaces::DarkOak, FLinearColor(0.16f, 0.12f, 0.095f), 2.2f);
	// lacquered_cherry_wood (0.029, 0.012, 0.005) for the desk, the chairs and the tables: pulled a
	// little lighter and redder, a mahogany under dust, and duller than the piano's.
	MatMahogany = Sheet(RoomSurfaces::PianoWood, FLinearColor(1.55f, 1.35f, 1.45f), 1.8f);
	// brown_leather (0.084, 0.028, 0.006) to the wine cellar's old dark brown; and a green one for
	// the desk's inset, the blotter's corners and the chairs' seats.
	MatLeather = Sheet(RoomSurfaces::Leather, FLinearColor(0.36f, 0.50f, 0.78f), 1.5f);
	MatLeatherGreen = Sheet(RoomSurfaces::Leather, FLinearColor(0.26f, 1.35f, 3.6f), 1.4f);
	// A grey veined stone under soot and dust; at the living room's figure the chimneypiece was the
	// palest thing in the room at arm's length from the lantern.
	MatMarble = Sheet(RoomSurfaces::Marble, FLinearColor(0.21f, 0.20f, 0.19f), 0.9f);
	MatSootBrick = Sheet(RoomSurfaces::CellarBrick, FLinearColor(0.10f, 0.09f, 0.085f), 1.2f);
	// A silk lampshade gone the colour of weak tea.
	MatFabric = Sheet(RoomSurfaces::Drapery, FLinearColor(0.42f, 0.26f, 0.15f), 1.2f);
	// The reading corner's rug: floral_jacquard in a faded green (the living room's carpet arithmetic).
	MatGreenRug = Build.Surface(RoomSurfaces::Carpet, FLinearColor(0.58f, 1.0f, 0.62f), 1.3f);
	MatBrass = Sheet(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatPaper = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.464f, 0.245f, 0.108f));

	// The baked atlases, straight from their assets: the models carry UVs into them.
	// The spines at about a third of the bake: at nearly a half the vellum and the cream cloth came
	// out white a metre from the lantern.
	MatBooks = Atlas(TEXT("library_books"), FLinearColor(0.36f, 0.34f, 0.32f), 1.f);
	MatPaperSheet = Atlas(TEXT("library_paper"), FLinearColor(0.26f, 0.24f, 0.21f), 1.f);
	MatGlobe = Atlas(TEXT("library_globe"), FLinearColor(0.34f, 0.32f, 0.29f), 1.f);
	MatRug = Atlas(TEXT("library_rug"), FLinearColor(0.40f, 0.38f, 0.37f), 1.f);

	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.06f, 0.035f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);

	struct FFlat { const TCHAR* Name; FLinearColor Color; float Roughness; float Metallic; };
	const FFlat FlatList[] = {
		{ TEXT("Shadow"), FLinearColor(0.002f, 0.002f, 0.002f), 1.f, 0.f },
		// Gold leaf and gilt mouldings under dust: a dull old gold that still takes a highlight.
		{ TEXT("Gilt"), FLinearColor(0.20f, 0.14f, 0.055f), 0.42f, 1.f },
		{ TEXT("Ink"), FLinearColor(0.006f, 0.006f, 0.012f), 0.4f, 0.f },
		// Cased glass, green outside and opal inside: the one coloured thing on the desk, and off.
		{ TEXT("Shade"), FLinearColor(0.010f, 0.055f, 0.024f), 0.18f, 0.f },
		{ TEXT("ShadeInside"), FLinearColor(0.22f, 0.22f, 0.20f), 0.35f, 0.f },
		{ TEXT("Felt"), FLinearColor(0.012f, 0.028f, 0.017f), 1.f, 0.f },
		{ TEXT("Record"), FLinearColor(0.004f, 0.004f, 0.004f), 0.3f, 0.f },
		{ TEXT("Bone"), FLinearColor(0.20f, 0.17f, 0.12f), 0.45f, 0.f },
		{ TEXT("Ebony"), FLinearColor(0.006f, 0.0055f, 0.005f), 0.35f, 0.f },
		{ TEXT("Paint"), RoomPalette::PaintedTrim, 0.62f, 0.f },
		{ TEXT("Ash"), FLinearColor(0.060f, 0.058f, 0.055f), 1.f, 0.f },
		{ TEXT("Charcoal"), FLinearColor(0.008f, 0.007f, 0.007f), 1.f, 0.f },
		{ TEXT("Wax"), FLinearColor(0.11f, 0.018f, 0.014f), 0.5f, 0.f },
		{ TEXT("Candle"), FLinearColor(0.34f, 0.30f, 0.22f), 0.55f, 0.f },
		{ TEXT("Ceramic"), FLinearColor(0.16f, 0.15f, 0.13f), 0.3f, 0.f },
		{ TEXT("Aluminium"), FLinearColor(0.045f, 0.045f, 0.05f), 0.42f, 1.f },
		{ TEXT("Velvet"), FLinearColor(0.045f, 0.007f, 0.011f), 0.95f, 0.f },
		{ TEXT("Card"), FLinearColor(0.24f, 0.19f, 0.12f), 0.9f, 0.f },
		{ TEXT("Bristle"), FLinearColor(0.05f, 0.035f, 0.02f), 1.f, 0.f },
		{ TEXT("Fringe"), FLinearColor(0.20f, 0.18f, 0.14f), 1.f, 0.f },
		// The curtains' fringe, cord and tassels: gold silk thread gone dull, a little sheen left.
		// At (0.20, 0.135, 0.05) the tassels came out cream a metre from the lantern.
		{ TEXT("GoldThread"), FLinearColor(0.12f, 0.078f, 0.026f), 0.55f, 0.35f },
	};
	for (const FFlat& Flat : FlatList)
	{
		Flats.Add(FName(Flat.Name), Build.Flat(Flat.Color, Flat.Roughness, Flat.Metallic));
	}
}

UMaterialInterface* ALibraryActor::F(const TCHAR* Name) const
{
	const TObjectPtr<UMaterialInstanceDynamic>* Found = Flats.Find(FName(Name));
	return Found ? Found->Get() : MatShell.Get();
}

// ---------------------------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------------------------

void ALibraryActor::Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
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

UStaticMeshComponent* ALibraryActor::Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
	std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	Dress(Mesh, Slots);
	return Mesh;
}

void ALibraryActor::Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw)
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

USceneComponent* ALibraryActor::Pivot(const FVector& Location, const FRotator& Rotation, const TCHAR* Name)
{
	USceneComponent* Scene = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
	Scene->SetMobility(EComponentMobility::Movable);
	Scene->AttachToComponent(RoomRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Scene->SetRelativeLocationAndRotation(Location, Rotation);
	Scene->RegisterComponent();
	AddInstanceComponent(Scene);
	return Scene;
}

UStaticMeshComponent* ALibraryActor::Paper(FRoomBuilder& Build, const TCHAR* Rect, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H)
{
	// One instance per rectangle, made straight from the asset and never registered with the builder,
	// so Add() does not re-tile it to the plane's size (the wine cellar's and the laundry's paper).
	const FName Key(Rect);
	UMaterialInstanceDynamic* Mat = nullptr;
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = SheetMats.Find(Key))
	{
		Mat = Found->Get();
	}
	else
	{
		const FLibraryRect* R = Algo::FindByPredicate(LibraryPaperRects, [&](const FLibraryRect& Each) { return FCString::Strcmp(Each.Name, Rect) == 0; });
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_library_paper.MI_library_paper"));
		Mat = (Parent && R) ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Mat)
		{
			// Paper at about a fifth of the bake: anything paler burns out under the lantern.
			Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.24f, 0.22f, 0.19f));
			Mat->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(R->W / 2048.f, R->H / 2048.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(R->X / 2048.f, R->Y / 2048.f, 0.f, 1.f));
		}
		SheetMats.Add(Key, Mat);
	}
	// Local Z out of the sheet, local X to the viewer's right, so local -Y is the top of the page.
	const FVector Right = FVector::CrossProduct(Normal, Up).GetSafeNormal();
	const FRotator Facing = FRotationMatrix::MakeFromZX(Normal, Right).Rotator();
	UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Facing, FVector(W, H, 1.f), Mat ? Mat : MatPaper.Get(), false);
	if (Sheet)
	{
		Sheet->SetCastShadow(false);
		Sheet->SetReceivesDecals(false);
	}
	return Sheet;
}

UStaticMeshComponent* ALibraryActor::Picture(FRoomBuilder& Build, const TCHAR* Set, const FVector4f& Rect, float SheetSize, const FVector& Centre,
	const FVector& Normal, float W, float H, const FLinearColor& Tint, float RoughnessScale)
{
	const FName Key(*FString::Printf(TEXT("%s_%.0f_%.0f"), Set, Rect.X, Rect.Y));
	UMaterialInstanceDynamic* Mat = nullptr;
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = SheetMats.Find(Key))
	{
		Mat = Found->Get();
	}
	else
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), Set, Set));
		Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("Tint"), Tint);
			Mat->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(Rect.Z / SheetSize, Rect.W / SheetSize, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(Rect.X / SheetSize, Rect.Y / SheetSize, 0.f, 1.f));
		}
		SheetMats.Add(Key, Mat);
	}
	const FVector Right = FVector::CrossProduct(Normal, FVector::UpVector).GetSafeNormal();
	const FRotator Facing = FRotationMatrix::MakeFromZX(Normal, Right).Rotator();
	UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Facing, FVector(W, H, 1.f), Mat ? Mat : MatShell.Get(), false);
	if (Sheet)
	{
		Sheet->SetCastShadow(false);
		Sheet->SetReceivesDecals(false);
	}
	return Sheet;
}

void ALibraryActor::Dust(FRoomBuilder& Build, const FVector& Centre, const FVector2D& Size, float Opacity) const
{
	// From four centimetres over the top, aimed down: a decal reaches nine either way (09-29).
	Build.Stain(RoomSurfaces::Damp, Centre + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, 0.f), Size, LibraryDust, Opacity, 1.35f);
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers, the girl's room's: U runs along a wall (X on north/south, Y on east/west), Z is
// height, and a wall's normal points into the room.
// ---------------------------------------------------------------------------------------------

float ALibraryActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector ALibraryActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector ALibraryActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

float ALibraryActor::FacingYaw(EWall Wall)
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

TArray<FBox2D> ALibraryActor::CutAround(EWall Wall, float U0, float U1, float Z0, float Z1) const
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

void ALibraryActor::WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud)
{
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

void ALibraryActor::PanelPiece(FRoomBuilder& Build, const FVector& Centre, float Yaw, float W, float H)
{
	if (W < 0.5f || H < 0.5f)
	{
		return;
	}
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr,
		*FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), RoomSurfaces::Wainscot.Set, RoomSurfaces::Wainscot.Set));
	UMaterialInstanceDynamic* Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
	if (Mat)
	{
		// The hall's and the living room's panelling tint; each piece cut from its own part of the
		// photograph. At the photograph's own 140cm a repeat, five metres of it was a grid of
		// hand-sized squares, a tiled wall: the hall only panels to the dado. A room panelled to the
		// cornice was panelled in bigger fields, so the repeat is taken up nearly double.
		const float Texel = RoomSurfaces::Wainscot.TexelSizeCm * 1.85f;
		const int32 Index = PanelMats.Num();
		Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(2.1f, 3.3f, 3.5f));
		Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(W / Texel, H / Texel, 0.f, 1.f));
		Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(FMath::Frac(Index * 0.317f), FMath::Frac(Index * 0.533f), 0.f, 1.f));
		PanelMats.Add(Mat);
	}
	Build.Box(Centre, FRotator(0.f, Yaw, 0.f), FVector(W, 0.6f, H), Mat ? Mat : MatPanel.Get(), false);
}

void ALibraryActor::AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const
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

bool ALibraryActor::WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfU, float HalfZ) const
{
	// A wall decal projects onto everything within reach of the wall (the wine cellar's 10-08 note):
	// the bookcases stand against it from the floor to 470, the arch and the windows have their
	// architraves round them, and the breast stands out of the east wall. A decal near any of those
	// goes elsewhere.
	const float Margin = 15.f;
	for (const FShelfRun& Run : ShelfRuns)
	{
		if (Run.Wall == Wall && U + HalfU > Run.U0 - Margin && U - HalfU < Run.U1 + Margin && Z - HalfZ < CaseTop + Margin)
		{
			return true;
		}
	}
	for (const FOpening& Opening : Openings)
	{
		const float Surround = Opening.Wall == EWall::North ? ArchArchitrave : 14.f;
		if (Opening.Wall == Wall && FMath::Abs(U - Opening.CenterU) < Opening.HalfU + Surround + Margin + HalfU
			&& Z + HalfZ > Opening.BottomZ - Margin && Z - HalfZ < Opening.TopZ + Surround + Margin)
		{
			return true;
		}
	}
	if (Wall == EWall::East && FMath::Abs(U - BreastY) < BreastHalf + Margin + HalfU)
	{
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------

void ALibraryActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float Z0 = -20.f;
	const float Z1 = RoomHeight + 10.f;

	// Solid, colliding and never seen. The north wall up to the corridor's ceiling is the corridor's
	// south wall; the west wall below the hall's ceiling is the hall's east wall and the living
	// room's; the east wall up to the girl's ceiling is her west wall. Everything over those is ours.
	Build.Box(FVector(WestX() - T * 0.5f, (NorthY() + SouthY() + T) * 0.5f, (Z0 + Z1) * 0.5f), FRotator::ZeroRotator,
		FVector(T, SouthY() + T - NorthY(), Z1 - Z0), MatShell);
	const float NurserySouthOuter = NorthY() + 500.f + T;
	Build.Box(FVector(EastX() + T * 0.5f, (NorthY() + NurserySouthOuter) * 0.5f, (325.f + Z1) * 0.5f), FRotator::ZeroRotator,
		FVector(T, NurserySouthOuter - NorthY(), Z1 - 325.f), MatShell);
	Build.Box(FVector(EastX() + T * 0.5f, (NurserySouthOuter + SouthY() + T) * 0.5f, (Z0 + Z1) * 0.5f), FRotator::ZeroRotator,
		FVector(T, SouthY() + T - NurserySouthOuter, Z1 - Z0), MatShell);
	Build.Box(FVector(MidX(), NorthY() - T * 0.5f, (Setup.CorridorHeight - 5.f + Z1) * 0.5f), FRotator::ZeroRotator,
		FVector(EastX() - WestX() + T * 2.f, T, Z1 - (Setup.CorridorHeight - 5.f)), MatShell);
	for (const FBox2D& Piece : CutAround(EWall::South, WestX() - T, EastX() + T, Z0, Z1))
	{
		const FVector2D C = Piece.GetCenter();
		const FVector2D S = Piece.GetSize();
		Build.Box(FVector(C.X, SouthY() + T * 0.5f, C.Y), FRotator::ZeroRotator, FVector(S.X, T, S.Y), MatShell);
	}

	// The ceiling, and a dark slab under the floor.
	Build.Box(FVector(MidX(), (NorthY() - T + SouthY() + T) * 0.5f, RoomHeight + 5.f), FRotator::ZeroRotator,
		FVector(EastX() - WestX() + T * 2.f, SouthY() - NorthY() + T * 2.f, 10.f), MatShell);
	Build.Box(FVector(MidX(), MidY(), -9.f), FRotator::ZeroRotator, FVector(EastX() - WestX(), SouthY() - NorthY(), 10.f), MatShell);
}

void ALibraryActor::BuildFloor(FRoomBuilder& Build)
{
	// Herringbone, wall to wall, as the living room's below: the floor that was laid to be shown. One
	// box, turned so its long side is its local X (the builder's tiling rule); it collides.
	Build.Box(FVector(MidX(), MidY(), -2.f), FRotator(0.f, 90.f, 0.f), FVector(SouthY() - NorthY(), EastX() - WestX(), 4.f), MatParquet);

	// Dust along the feet of the bookcases and the walls, where nobody's feet went. Aimed down, the
	// first size runs along world Y: roll 90 along the north and south walls, none along the east
	// and west (the 09-27 note). Kept off the arch's sill (Y over 610).
	for (int32 i = 0; i < 18; ++i)
	{
		const int32 Side = i % 4;
		const float Along = Random.FRandRange(0.f, 1.f);
		const float Out = Random.FRandRange(10.f, 26.f);
		const float Length = Random.FRandRange(70.f, 150.f);
		const float Width = Random.FRandRange(16.f, 28.f);
		const float Opacity = Random.FRandRange(0.22f, 0.36f);
		const float Turn = Random.FRandRange(-6.f, 6.f);
		FVector2D Spot;
		float Roll = 0.f;
		switch (Side)
		{
		case 0: Spot = FVector2D(FMath::Lerp(WestX() + 60.f, EastX() - 60.f, Along), NorthY() + LowerDepth + Out); Roll = 90.f; break;
		case 1: Spot = FVector2D(FMath::Lerp(WestX() + 60.f, EastX() - 60.f, Along), SouthY() - Out); Roll = 90.f; break;
		case 2: Spot = FVector2D(EastX() - LowerDepth - Out, FMath::Lerp(NorthY() + 80.f, SouthY() - 60.f, Along)); break;
		default: Spot = FVector2D(WestX() + LowerDepth + Out, FMath::Lerp(NorthY() + 80.f, SouthY() - 60.f, Along)); break;
		}
		if (Side == 0 && FMath::Abs(Spot.X - Setup.ArchX) < ArchHalf + 60.f)
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(Spot.X, Spot.Y, 4.f), FRotator(-90.f, 0.f, Roll + Turn), FVector2D(Length, Width), LibraryDust, Opacity, 1.35f);
	}
	// A thin film over the whole floor, and the way he walked every evening from the arch to the desk
	// and from the desk to the fire: darker, where the dust never settled.
	for (int32 i = 0; i < 7; ++i)
	{
		const float X = Random.FRandRange(WestX() + 140.f, EastX() - 140.f);
		const float Y = Random.FRandRange(NorthY() + 160.f, SouthY() - 160.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float SizeV = Random.FRandRange(130.f, 220.f);
		const float SizeU = Random.FRandRange(170.f, 260.f);
		const FVector2D Size(SizeU, SizeV);
		const float Opacity = Random.FRandRange(0.14f, 0.22f);
		Build.Stain(RoomSurfaces::Damp, FVector(X, Y, 4.f), FRotator(-90.f, 0.f, Roll), Size, LibraryDust, Opacity, 1.4f);
	}
	const FVector2D Path[] = { { Setup.ArchX, 690.f }, { -840.f, 900.f }, { -800.f, 1120.f }, { -640.f, 1140.f } };
	for (const FVector2D& P : Path)
	{
		Build.Stain(RoomSurfaces::Damp, FVector(P.X, P.Y, 4.f), FRotator(-90.f, 0.f, 0.f), FVector2D(150.f, 80.f),
			FLinearColor(0.10f, 0.085f, 0.07f), 0.28f, 1.3f, 0.8f);
	}
	// Under the windows the rain has been in at the sills for years.
	for (const float X : WindowXs)
	{
		Build.Stain(RoomSurfaces::Damp, FVector(X, SouthY() - 38.f, 4.f), FRotator(-90.f, 0.f, 90.f), FVector2D(70.f, 140.f),
			FLinearColor(0.16f, 0.12f, 0.09f), 0.6f, 1.25f, 0.6f);
	}
}

void ALibraryActor::BuildWalls(FRoomBuilder& Build)
{
	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Walls[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), SouthY() },
	};
	for (const FRun& R : Walls)
	{
		// Panelled from the floor to the bookcases' cornice wherever the bookcases stop, plaster over
		// it to the ceiling. Not behind the bookcases: there the panelling stood in front of their
		// back boards and showed between the books.
		TArray<FBox2D> Holes;
		for (const FOpening& Opening : Openings)
		{
			if (Opening.Wall == R.Wall)
			{
				Holes.Add(FBox2D(FVector2D(Opening.CenterU - Opening.HalfU, Opening.BottomZ), FVector2D(Opening.CenterU + Opening.HalfU, Opening.TopZ)));
			}
		}
		for (const FShelfRun& Run : ShelfRuns)
		{
			if (Run.Wall == R.Wall)
			{
				Holes.Add(FBox2D(FVector2D(Run.U0, 0.f), FVector2D(Run.U1, CaseTop)));
			}
		}
		if (R.Wall == EWall::East)
		{
			Holes.Add(FBox2D(FVector2D(BreastY - BreastHalf, 0.f), FVector2D(BreastY + BreastHalf, RoomHeight)));
		}
		for (const FBox2D& Piece : RoomWalls::CutAround(Holes, R.U0, R.U1, 0.f, CaseTop))
		{
			const FVector2D C = Piece.GetCenter();
			const FVector2D S = Piece.GetSize();
			PanelPiece(Build, WallPoint(R.Wall, C.X, C.Y, 0.6f), FacingYaw(R.Wall), S.X, S.Y);
		}
		WallFill(Build, R.Wall, R.U0, R.U1, CaseTop, RoomHeight, MatPlaster, 0.3f);
		// A plain oak skirting along the panelled stretches; behind a bookcase it is only hidden.
		for (const FBox2D& Piece : CutAround(R.Wall, R.U0, R.U1, 0.f, 18.f))
		{
			const FVector2D C = Piece.GetCenter();
			const FVector2D S = Piece.GetSize();
			const bool bAlongX = R.Wall == EWall::North || R.Wall == EWall::South;
			const FVector At = WallPoint(R.Wall, C.X, 9.f, 2.f);
			Build.Box(At, bAlongX ? FRotator::ZeroRotator : FRotator(0.f, 90.f, 0.f), FVector(S.X, 2.8f, 18.f), MatOakBox, false);
		}
	}

	// The arch from the corridor: one model through the wall, its corridor face painted and its
	// library face oak, both architraves and the lining in it. The corridor has cut its wall to the
	// arch's rectangle; this room's panelling is cut to it above.
	// The corridor's trim is RoughWood at (0.370, 0.370, 0.360); this is its own instance, a hair off,
	// with the tiling at one for the model's UVs.
	UMaterialInstanceDynamic* CorridorTrim = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.3707f, 0.370f, 0.360f));
	LibrarySheet(CorridorTrim);
	Place(Build, LibraryProps::Arch, FVector(Setup.ArchX, NorthY() - Setup.WallThickness * 0.5f, 0.f), FRotator::ZeroRotator,
		{ { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("CorridorWall"), MatCorridorPlaster }, { TEXT("LibraryWall"), MatPanelSheet },
		  { TEXT("Oak"), MatOak }, { TEXT("Trim"), CorridorTrim } });

	// The chimney breast in the east wall: a shell round the firebox (it has to stay hollow, or its
	// front face covers the fire), and the panelling laid on its three faces with the opening cut.
	const float FaceX = BreastFaceX();
	const float Y0 = BreastY - BreastHalf;
	const float Y1 = BreastY + BreastHalf;
	const float OpenHalf = 48.f;
	const float OpenTop = 94.f;
	auto Solid = [&](float YA, float YB, float ZA, float ZB)
	{
		Build.Box(FVector((FaceX + EastX()) * 0.5f, (YA + YB) * 0.5f, (ZA + ZB) * 0.5f), FRotator::ZeroRotator, FVector(EastX() - FaceX, YB - YA, ZB - ZA), MatShell);
	};
	Solid(Y0, BreastY - OpenHalf, 0.f, RoomHeight);
	Solid(BreastY + OpenHalf, Y1, 0.f, RoomHeight);
	Solid(BreastY - OpenHalf, BreastY + OpenHalf, OpenTop, RoomHeight);
	TArray<FBox2D> FaceHoles = { FBox2D(FVector2D(BreastY - OpenHalf, 0.f), FVector2D(BreastY + OpenHalf, OpenTop)) };
	for (const FBox2D& Piece : RoomWalls::CutAround(FaceHoles, Y0, Y1, 0.f, CaseTop))
	{
		PanelPiece(Build, FVector(FaceX - 0.3f, Piece.GetCenter().X, Piece.GetCenter().Y), FacingYaw(EWall::East), Piece.GetSize().X, Piece.GetSize().Y);
	}
	Build.Mark(FVector(FaceX - 0.3f, BreastY, (CaseTop + RoomHeight) * 0.5f), FRotator(0.f, 90.f, 90.f), FVector2D(Y1 - Y0, RoomHeight - CaseTop), MatPlaster);
	for (const float Side : { -1.f, 1.f })
	{
		const float Y = BreastY + Side * BreastHalf;
		// The breast's south side faces +Y (a yaw of 0 turns a piece's face there), its north side -Y.
		PanelPiece(Build, FVector((FaceX + EastX()) * 0.5f, Y + Side * 0.3f, CaseTop * 0.5f), Side > 0.f ? 0.f : 180.f, EastX() - FaceX, CaseTop);
		// A Mark stands out along its +Z, and a roll of +90 turns that to +Y (the corridor's FacePanel note).
		Build.Mark(FVector((FaceX + EastX()) * 0.5f, Y + Side * 0.3f, (CaseTop + RoomHeight) * 0.5f), FRotator(0.f, 0.f, Side > 0.f ? 90.f : -90.f),
			FVector2D(EastX() - FaceX, RoomHeight - CaseTop), MatPlaster);
	}

	// The crown moulding where the walls meet the ceiling, round the room and round the breast:
	// library_cornice laid end to end along each stretch and scaled to fit it. At an inside corner
	// the two runs go through each other, which is what a mitre looks like from below.
	// Round the breast's two outside corners the runs are carried on past the corner by the
	// moulding's own depth, so they meet in a block instead of leaving a notch.
	const float Depth = 30.f;
	struct FCorniceRun { FVector Start; FVector Dir; float Length; float Yaw; };
	const FCorniceRun Cornices[] = {
		{ FVector(WestX(), NorthY(), RoomHeight), FVector(1.f, 0.f, 0.f), EastX() - WestX(), 0.f },
		{ FVector(EastX(), SouthY(), RoomHeight), FVector(-1.f, 0.f, 0.f), EastX() - WestX(), 180.f },
		{ FVector(WestX(), SouthY(), RoomHeight), FVector(0.f, -1.f, 0.f), SouthY() - NorthY(), -90.f },
		{ FVector(EastX(), NorthY(), RoomHeight), FVector(0.f, 1.f, 0.f), Y0 - NorthY(), 90.f },
		{ FVector(EastX(), Y1, RoomHeight), FVector(0.f, 1.f, 0.f), SouthY() - Y1, 90.f },
		{ FVector(FaceX, Y0 - Depth, RoomHeight), FVector(0.f, 1.f, 0.f), Y1 - Y0 + Depth * 2.f, 90.f },
		{ FVector(EastX(), Y0, RoomHeight), FVector(-1.f, 0.f, 0.f), EastX() - FaceX + Depth, 180.f },
		{ FVector(EastX(), Y1, RoomHeight), FVector(-1.f, 0.f, 0.f), EastX() - FaceX + Depth, 0.f },
	};
	for (const FCorniceRun& Run : Cornices)
	{
		const int32 Pieces = FMath::Max(1, FMath::CeilToInt(Run.Length / 100.f));
		const float Piece = Run.Length / Pieces;
		for (int32 i = 0; i < Pieces; ++i)
		{
			if (UStaticMeshComponent* Mesh = Place(Build, LibraryProps::Cornice, Run.Start + Run.Dir * (Piece * (i + 0.5f)), FRotator(0.f, Run.Yaw, 0.f),
				{ { TEXT("Trim"), MatOak } }))
			{
				Mesh->SetRelativeScale3D(FVector(Piece / 100.f, 1.f, 1.f));
				Mesh->SetReceivesDecals(false);
			}
		}
	}
}

void ALibraryActor::BuildWindows(FRoomBuilder& Build)
{
	// The three tall windows: oak architraves up the jambs, and the round heads (library_window_arch)
	// laid over the tops of the rectangular openings, so from inside every one is arched. The head's
	// fill and soffit are panelled like the wall round them; a painted triangle between the
	// architrave and the panelling read as a patch.
	const float Spring = WindowTop - WindowHalf;
	for (const float X : WindowXs)
	{
		for (const float Side : { -1.f, 1.f })
		{
			Build.Box(WallPoint(EWall::South, X + Side * (WindowHalf + 6.f), (WindowSill - 4.f + Spring) * 0.5f, 1.7f), FRotator::ZeroRotator,
				FVector(12.f, 2.8f, Spring - WindowSill + 4.f), MatOakBox, false);
			// The reveal's lining from the sill to the springing, in the head's panelling: a piece turned
			// to face across the opening.
			PanelPiece(Build, FVector(X + Side * (WindowHalf - 0.3f), SouthY() + 4.5f, (WindowSill + Spring) * 0.5f), Side > 0.f ? 90.f : -90.f,
				9.f, Spring - WindowSill);
		}
		if (UStaticMeshComponent* Head = Place(Build, LibraryProps::WindowArch, FVector(X, SouthY(), WindowTop), FRotator(0.f, 180.f, 0.f),
			{ { TEXT("Trim"), MatPanelSheet }, { TEXT("Oak"), MatOak } }))
		{
			Head->SetReceivesDecals(false);
		}
	}

	// Over each window, the velvet valance with its swags, fringe and tassels (the user's theatre
	// reference), hiding the pole the drapes hang from. Its board's top is a little over the pole and
	// its swags come down over the crown of the arch, as a pelmet does.
	UMaterialInstanceDynamic* Valance = Build.Surface(LibraryVelvet, LibraryVelvetTint + FLinearColor(0.0007f, 0.f, 0.f, 0.f), 1.15f);
	LibrarySheet(Valance);
	for (const float X : WindowXs)
	{
		if (UStaticMeshComponent* Mesh = Place(Build, LibraryProps::Valance, WallPoint(EWall::South, X, WindowTop + 34.f, 0.f), FRotator(0.f, 180.f, 0.f),
			{ { TEXT("CurtainVelvet"), Valance }, { TEXT("Gold"), F(TEXT("GoldThread")) } }))
		{
			Mesh->SetReceivesDecals(false);
		}
	}
}

void ALibraryActor::FillBay(const FTransform& Bay, float ScaleX, FRandomStream& Stream)
{
	// A bay's shelves, in its own frame: X along the shelf, Y out of it, Z up. The interior is the
	// bay's width less its two uprights, scaled with the bay; the books are not scaled.
	const float Interior = (BayWidth - 2.f * BayUpright) * ScaleX - 0.8f;
	const float X0 = -Interior * 0.5f;
	const float Front = UpperDepth - 1.4f;
	for (int32 Level = 0; Level < ShelfLevels; ++Level)
	{
		const float Z = CounterZ + Level * ShelfPitch;
		float Cursor = X0 + Stream.FRandRange(0.f, 1.2f);
		bool bAfterRow = false;
		while (Cursor < X0 + Interior - 2.f)
		{
			const float Remaining = X0 + Interior - Cursor;
			const float Roll = Stream.FRand();
			const float Gap = Stream.FRandRange(5.f, 16.f);
			const float Turn = Stream.FRandRange(-7.f, 7.f);
			const int32 Pick = Stream.RandRange(0, 9);
			const int32 PileIndex = Stream.RandRange(0, 1);
			auto Add = [&](UInstancedStaticMeshComponent* Set, const FVector& Local, float Yaw)
			{
				if (Set)
				{
					Set->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Local) * Bay);
				}
			};

			// A few gone over against the end of the row before, into the gap somebody left.
			if (Roll < 0.05f && bAfterRow && Remaining > 22.f)
			{
				Add(BooksLeaning, FVector(Cursor, Front, Z), 0.f);
				Cursor += 21.f + Gap;
				bAfterRow = false;
				continue;
			}
			// A pile laid flat in a gap.
			if (Roll < 0.10f && Remaining > 32.f)
			{
				Add(BookPiles[PileIndex], FVector(Cursor + 16.f, UpperDepth * 0.5f, Z), Turn);
				Cursor += 33.f;
				bAfterRow = false;
				continue;
			}
			// A gap where books were taken out and not put back.
			if (Roll < 0.19f)
			{
				Cursor += Gap;
				bAfterRow = false;
				continue;
			}
			// A row: the one picked if it fits, else the longest that does, else the shelf ends here.
			int32 Row = Pick;
			if (RowLengths[Row] > Remaining)
			{
				Row = INDEX_NONE;
				for (int32 k = 0; k < 10; ++k)
				{
					if (RowLengths[k] <= Remaining && (Row == INDEX_NONE || RowLengths[k] > RowLengths[Row]))
					{
						Row = k;
					}
				}
			}
			if (Row == INDEX_NONE)
			{
				break;
			}
			Add(BookRows[Row], FVector(Cursor, Front, Z), 0.f);
			Cursor += RowLengths[Row] + Stream.FRandRange(0.f, 0.5f);
			bAfterRow = true;
		}
	}
}

void ALibraryActor::BuildShelves(FRoomBuilder& Build)
{
	// The books: one instanced set per row model, Nanite, a few thousand bindings in all.
	for (const TCHAR* Name : LibraryProps::BookRows)
	{
		BookRows.Add(Build.Instances(FRoomShapes::Prop(Name), MatBooks));
	}
	BooksLeaning = Build.Instances(FRoomShapes::Prop(LibraryProps::BooksLeaning), MatBooks);
	for (const TCHAR* Name : LibraryProps::BookPiles)
	{
		BookPiles.Add(Build.Instances(FRoomShapes::Prop(Name), MatBooks));
	}

	FRandomStream Stream(4471);
	for (const FShelfRun& Run : ShelfRuns)
	{
		const float Length = Run.U1 - Run.U0;
		const int32 Bays = FMath::Max(1, FMath::RoundToInt(Length / BayWidth));
		const float Scale = Length / (Bays * BayWidth);
		const float Yaw = FacingYaw(Run.Wall);
		for (int32 i = 0; i < Bays; ++i)
		{
			const float U = Run.U0 + (i + 0.5f) * Length / Bays;
			const FVector At = WallPoint(Run.Wall, U, 0.f, 0.f);
			if (UStaticMeshComponent* Case = Place(Build, LibraryProps::Bookcase, At, FRotator(0.f, Yaw, 0.f),
				{ { TEXT("Oak"), MatOak }, { TEXT("Brass"), MatBrass }, { TEXT("Shadow"), F(TEXT("Shadow")) } }))
			{
				Case->SetRelativeScale3D(FVector(Scale, 1.f, 1.f));
			}
			FillBay(FTransform(FRotator(0.f, Yaw, 0.f), At), Scale, Stream);
		}
		// The whole run blocks a walking man at the cupboards' front.
		const bool bAlongX = Run.Wall == EWall::North || Run.Wall == EWall::South;
		const FVector Centre = WallPoint(Run.Wall, (Run.U0 + Run.U1) * 0.5f, CaseTop * 0.5f, LowerDepth * 0.5f);
		Blocker(Build, Centre, bAlongX ? FVector(Length, LowerDepth, CaseTop) : FVector(LowerDepth, Length, CaseTop));
	}

	// The ladder's rail along the west wall's bookcases, on brackets off the uprights, and the ladder
	// hung on it a little way down the room, its foot out on the boards.
	const FShelfRun& West = ShelfRuns[2];
	const float RailX = WestX() + RailProud;
	Build.Cyl(FVector(RailX, (West.U0 + West.U1) * 0.5f, RailZ), FRotator(90.f, 90.f, 0.f), FVector(2.4f, 2.4f, West.U1 - West.U0 - 6.f), MatBrass, false);
	for (float Y = West.U0 + 30.f; Y < West.U1 - 20.f; Y += 104.f)
	{
		Build.Box(FVector(WestX() + UpperDepth + 3.f, Y, RailZ), FRotator::ZeroRotator, FVector(7.f, 2.f, 3.f), MatBrass, false);
	}
	Place(Build, LibraryProps::Ladder, FVector(WestX(), LadderY(), 0.f), FRotator(0.f, FacingYaw(EWall::West), 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Brass"), MatBrass } });
	Blocker(Build, FVector(WestX() + LadderFoot - 4.f, LadderY(), 60.f), FVector(16.f, 58.f, 120.f));
}

void ALibraryActor::BuildFireplace(FRoomBuilder& Build)
{
	const float FaceX = BreastFaceX();
	const float Facing = FacingYaw(EWall::East);

	// The chimneypiece, cold: soot in the firebox, the last logs burnt through on the basket, the ash
	// never cleared. The hearth stone is a step to walk over, not a wall.
	Place(Build, LibraryProps::Fireplace, FVector(FaceX, BreastY, 0.f), FRotator(0.f, Facing, 0.f),
		{ { TEXT("Marble"), MatMarble }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Brick"), MatSootBrick }, { TEXT("Iron"), MatIron },
		  { TEXT("Charcoal"), F(TEXT("Charcoal")) }, { TEXT("Ash"), F(TEXT("Ash")) } });
	// The breast is hollow where the firebox is; nobody walks into a fireplace.
	Blocker(Build, FVector(FaceX - 6.f, BreastY, 70.f), FVector(16.f, 200.f, 140.f));
	Place(Build, LibraryProps::FireTools, FVector(FaceX - 22.f, BreastY + 92.f, 3.f), FRotator(0.f, Facing - 8.f, 0.f),
		{ { TEXT("Brass"), MatBrass }, { TEXT("Iron"), MatIron }, { TEXT("Bristle"), F(TEXT("Bristle")) } });

	// Soot licked up out of the opening onto the lintel, and ash spilled out over the hearth. Under
	// the frieze, not across it: a black shape on pale stone at mantel height read as a painted blot.
	Build.Stain(RoomSurfaces::Damp, FVector(FaceX - 6.f, BreastY, 98.f), FRotator(0.f, 0.f, 0.f), FVector2D(90.f, 22.f),
		FLinearColor(0.03f, 0.028f, 0.026f), 0.5f, 1.3f, 1.f);
	Build.Stain(RoomSurfaces::Damp, FVector(FaceX - 30.f, BreastY, 7.f), FRotator(-90.f, 0.f, 0.f), FVector2D(70.f, 40.f),
		FLinearColor(0.20f, 0.19f, 0.18f), 0.5f, 1.3f, 1.f);

	// On the mantel shelf: the clock stopped at its own hour, a candlestick each end, a pair of
	// spectacles' case and a few cards stood against the breast.
	const FVector Shelf(FaceX - 16.f, BreastY, MantelTop);
	if (UStaticMeshComponent* Clock = Build.PropSeated(RoomProps::MantelClock, Shelf + FVector(2.f, 0.f, 0.f), FRotator(0.f, Facing, 0.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Clock, FLinearColor(0.45f, 0.40f, 0.36f), 0);
		Clock->SetMaterial(1, MatGlass);
	}
	for (const float Side : { -1.f, 1.f })
	{
		Place(Build, RoomProps::CellarCandlestick, Shelf + FVector(0.f, Side * 74.f, 0.f), FRotator(0.f, Side * 20.f, 0.f),
			{ { TEXT("Brass"), MatBrass }, { TEXT("Wax"), F(TEXT("Candle")) }, { TEXT("Wick"), F(TEXT("Shadow")) } });
	}
	Dust(Build, Shelf + FVector(0.f, 0.f, 0.f), FVector2D(200.f, 26.f), 0.45f);
	for (int32 i = 0; i < 2; ++i)
	{
		// Leant back against the breast, a little apart.
		const FVector At = FVector(FaceX - 2.f - i * 0.6f, BreastY - 40.f + i * 9.f, MantelTop + 4.6f);
		Paper(Build, i ? TEXT("card") : TEXT("envelope"), At, FVector(-1.f, 0.f, 0.12f).GetSafeNormal(), FVector(0.12f, 0.f, 1.f).GetSafeNormal(), 14.f, i ? 9.f : 8.2f);
	}

	// Over it, the woman and the little girl in oils, in the big gilt frame.
	const FVector Portrait(FaceX, BreastY, 292.f);
	if (UStaticMeshComponent* Frame = Place(Build, LibraryProps::FramePortrait, Portrait, FRotator(0.f, Facing, 0.f),
		{ { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } }))
	{
		LibraryNoDecals(Frame);
	}
	Picture(Build, TEXT("library_paintings"), LibraryMotherAndDaughter, 1024.f, Portrait + FVector(-1.4f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f),
		88.f, 110.f, FLinearColor(0.42f, 0.40f, 0.38f), 1.f);
	Build.Sph(Portrait + FVector(-1.f, 0.f, 80.f), 1.6f, MatIron);
}

void ALibraryActor::BuildDesk(FRoomBuilder& Build)
{
	// The partners' desk in the middle of the carpet, its panelled front to the arch and its drawers
	// to the windows, where he sat with the storm at his back. Built in its own frame: the sitter at
	// local -Y, local +X his left hand, the top at DeskTop.
	const FVector Seat = DeskSeat();
	Place(Build, LibraryProps::Rug, Seat + FVector(0.f, -10.f, 0.f), FRotator(0.f, 0.f, 0.f),
		{ { TEXT("Rug"), MatRug }, { TEXT("Fringe"), F(TEXT("Fringe")) } });

	USceneComponent* DeskFrame = Pivot(Seat, FRotator(0.f, 180.f, 0.f), TEXT("DeskFrame"));
	FRoomBuilder D(this, DeskFrame);
	const float Top = DeskTop;
	Place(D, LibraryProps::Desk, FVector::ZeroVector, FRotator::ZeroRotator,
		{ { TEXT("Mahogany"), MatMahogany }, { TEXT("LeatherGreen"), MatLeatherGreen }, { TEXT("Gilt"), F(TEXT("Gilt")) },
		  { TEXT("Brass"), MatBrass }, { TEXT("Shadow"), F(TEXT("Shadow")) } });
	Blocker(D, FVector(0.f, 0.f, 40.f), FVector(DeskWidth + 4.f, DeskDepth + 4.f, 80.f));

	// The drawer he had open, pulled out and left: the top one of the left-hand pedestal, the family's
	// papers in it.
	const FVector DrawerFront(DeskWidth * 0.5f - 23.f, -DeskDepth * 0.5f - 30.f, 54.f);
	Place(D, LibraryProps::DeskDrawer, DrawerFront, FRotator(0.f, 180.f, 0.f),
		{ { TEXT("Mahogany"), MatMahogany }, { TEXT("Oak"), MatOak }, { TEXT("Brass"), MatBrass } });

	// His chair, pushed back from the desk and turned, as it is when somebody gets up in a hurry.
	Place(D, LibraryProps::DeskChair, FVector(-14.f, -DeskDepth * 0.5f - 62.f, 0.f), FRotator(0.f, 28.f, 0.f),
		{ { TEXT("Mahogany"), MatMahogany }, { TEXT("Leather"), MatLeather }, { TEXT("Iron"), MatIron } });
	Blocker(D, FVector(-14.f, -DeskDepth * 0.5f - 62.f, 40.f), FVector(54.f, 54.f, 80.f), 28.f);

	// The top. Centre: the blotter, the notebook open on it (a clue, BuildClues). His right hand (-X):
	// the ink, the pen laid down across a sheet of notes. His left: the lamp at the back, the big
	// book open with the reading glass on it. The far corners: the laptop shut under its dust, the
	// seal and the wax, books stacked with slips of paper in them.
	Place(D, LibraryProps::Blotter, FVector(0.f, -12.f, Top), FRotator(0.f, 2.f, 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Paper"), MatPaperSheet }, { TEXT("LeatherGreen"), MatLeatherGreen } });
	Place(D, LibraryProps::BankersLamp, FVector(48.f, 26.f, Top), FRotator(0.f, 188.f, 0.f),
		{ { TEXT("Brass"), MatBrass }, { TEXT("Shade"), F(TEXT("Shade")) }, { TEXT("ShadeInside"), F(TEXT("ShadeInside")) } });
	Place(D, LibraryProps::InkBottle, FVector(-38.f, 18.f, Top), FRotator(0.f, 30.f, 0.f),
		{ { TEXT("Glass"), MatGlass }, { TEXT("Ink"), F(TEXT("Ink")) }, { TEXT("Ebony"), F(TEXT("Ebony")) } });
	Paper(D, TEXT("notes_a"), FVector(-44.f, -16.f, Top + 0.12f), FVector::UpVector, FRotator(0.f, 102.f, 0.f).RotateVector(FVector(1.f, 0.f, 0.f)), 21.f, 29.7f);
	Paper(D, TEXT("notes_b"), FVector(-38.f, -6.f, Top + 0.22f), FVector::UpVector, FRotator(0.f, 76.f, 0.f).RotateVector(FVector(1.f, 0.f, 0.f)), 21.f, 29.7f);
	Place(D, LibraryProps::Pen, FVector(-36.f, -14.f, Top + 0.3f), FRotator(0.f, 64.f, 0.f), { { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("Gilt"), F(TEXT("Gilt")) } });
	Place(D, LibraryProps::OpenBook, FVector(46.f, -12.f, Top), FRotator(0.f, -9.f, 0.f),
		{ { TEXT("Leather"), MatLeather }, { TEXT("Card"), F(TEXT("Card")) }, { TEXT("Paper"), MatPaperSheet }, { TEXT("Velvet"), F(TEXT("Velvet")) } });
	Place(D, LibraryProps::Magnifier, FVector(54.f, -18.f, Top + 4.4f), FRotator(0.f, 150.f, 0.f),
		{ { TEXT("Brass"), MatBrass }, { TEXT("Glass"), MatGlass }, { TEXT("Ebony"), F(TEXT("Ebony")) } });
	Place(D, LibraryProps::Glasses, FVector(16.f, -30.f, Top + 0.4f), FRotator(0.f, 22.f, 0.f), { { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Glass"), MatGlass } });
	Place(D, LibraryProps::Laptop, FVector(-62.f, 20.f, Top), FRotator(0.f, -6.f, 0.f),
		{ { TEXT("Aluminium"), F(TEXT("Aluminium")) }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Ebony"), F(TEXT("Ebony")) } });
	Place(D, LibraryProps::Seal, FVector(-66.f, -26.f, Top), FRotator(0.f, 12.f, 0.f),
		{ { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("Brass"), MatBrass }, { TEXT("Wax"), F(TEXT("Wax")) } });
	if (UStaticMeshComponent* Pile = Place(D, LibraryProps::BookPiles[0], FVector(74.f, 18.f, Top), FRotator(0.f, 96.f, 0.f), { { TEXT("Books"), MatBooks } }))
	{
		// Slips of paper standing out of the pages: where he had marked his place in each.
		for (int32 i = 0; i < 3; ++i)
		{
			D.Box(FVector(74.f + i * 1.6f - 2.f, 18.f - 10.f, Top + 3.f + i * 3.8f), FRotator(0.f, 96.f + i * 6.f, 0.f), FVector(4.f, 0.15f, 3.6f), MatPaper, false);
		}
	}
	Dust(D, FVector(0.f, 0.f, Top), FVector2D(DeskDepth, DeskWidth), 0.35f);

	// The newspaper he had finished with, dropped on the carpet beside the chair.
	Place(Build, LibraryProps::Newspaper, Seat + FVector(70.f, 92.f, 1.2f), FRotator(0.f, 22.f, 0.f), { { TEXT("Paper"), MatPaperSheet }, { TEXT("Card"), F(TEXT("Card")) } });
}

void ALibraryActor::BuildReadingCorner(FRoomBuilder& Build)
{
	// The armchair by the fire, turned to it, on its own faded green rug; a small table at its elbow
	// with a cup and the books he was reading, the standard lamp behind it. Somebody sat here every
	// evening, and the seat still has the shape of him in it.
	const FVector Chair = ArmchairSeat();
	const FVector ToFire = (FVector(BreastFaceX(), BreastY, 0.f) - Chair).GetSafeNormal2D();
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(-ToFire.X, ToFire.Y));
	Build.Cloth(Chair + FVector(25.f, -30.f, 0.6f), FRotator(0.f, Yaw + 8.f, 0.f), FVector2D(210.f, 150.f), 0.5f, 0.f, 8101, MatGreenRug, 90.f, /*bWorn*/ false);
	Place(Build, RoomProps::CellarArmchair, Chair, FRotator(0.f, Yaw, 0.f),
		{ { TEXT("Leather"), MatLeather }, { TEXT("Brass"), MatBrass }, { TEXT("Wood"), MatMahogany } });
	Blocker(Build, Chair + FVector(0.f, 0.f, 45.f), FVector(88.f, 86.f, 90.f), Yaw);

	const FVector Table = Chair + FRotator(0.f, Yaw, 0.f).RotateVector(FVector(-68.f, 8.f, 0.f));
	Place(Build, LibraryProps::SideTable, Table, FRotator(0.f, 15.f, 0.f), { { TEXT("Mahogany"), MatMahogany } });
	Blocker(Build, Table + FVector(0.f, 0.f, 34.f), FVector(48.f, 48.f, 68.f));
	Place(Build, LibraryProps::Mug, Table + FVector(9.f, -6.f, 67.4f), FRotator(0.f, -120.f, 0.f), { { TEXT("Ceramic"), F(TEXT("Ceramic")) }, { TEXT("Charcoal"), F(TEXT("Charcoal")) } });
	Place(Build, LibraryProps::BookPiles[1], Table + FVector(-6.f, 6.f, 67.4f), FRotator(0.f, 40.f, 0.f), { { TEXT("Books"), MatBooks } });
	Place(Build, LibraryProps::BookPiles[0], Table + FVector(0.f, 0.f, 26.f), FRotator(0.f, -10.f, 0.f), { { TEXT("Books"), MatBooks } });
	Dust(Build, Table + FVector(0.f, 0.f, 67.4f), FVector2D(46.f, 46.f), 0.4f);

	const FVector Lamp = Chair + FRotator(0.f, Yaw, 0.f).RotateVector(FVector(-46.f, -58.f, 0.f));
	Place(Build, LibraryProps::FloorLamp, Lamp, FRotator(0.f, 0.f, 0.f), { { TEXT("Brass"), MatBrass }, { TEXT("Fabric"), MatFabric } });
	Blocker(Build, Lamp + FVector(0.f, 0.f, 60.f), FVector(30.f, 30.f, 120.f));
	Build.Add(FRoomShapes::Plane(), Lamp + FVector(0.f, 0.f, 150.f), FRotator(0.f, 40.f, 90.f), FVector(30.f, 24.f, 1.f), MatWeb, false);
}

void ALibraryActor::BuildDecor(FRoomBuilder& Build)
{
	// The globe beside the desk, turned to the Atlantic and left.
	Place(Build, LibraryProps::Globe, GlobeSeat(), FRotator(0.f, 32.f, 0.f),
		{ { TEXT("Globe"), MatGlobe }, { TEXT("Brass"), MatBrass }, { TEXT("Oak"), MatOak }, { TEXT("Card"), F(TEXT("Card")) } });
	Blocker(Build, GlobeSeat() + FVector(0.f, 0.f, 50.f), FVector(70.f, 70.f, 100.f));

	// Between the windows: the gramophone on its record cabinet, the bronze bust on its column, and a
	// map over each.
	const float PierA = (WindowXs[0] + WindowXs[1]) * 0.5f;
	const float PierB = (WindowXs[1] + WindowXs[2]) * 0.5f;
	const FVector Cabinet(PierB, SouthY() - 25.f, 0.f);
	Place(Build, LibraryProps::RecordCabinet, Cabinet, FRotator(0.f, 180.f, 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Card"), F(TEXT("Card")) }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Brass"), MatBrass } });
	Place(Build, LibraryProps::Gramophone, Cabinet + FVector(2.f, 4.f, 78.f), FRotator(0.f, 168.f, 0.f),
		{ { TEXT("Oak"), MatOak }, { TEXT("Felt"), F(TEXT("Felt")) }, { TEXT("Record"), F(TEXT("Record")) }, { TEXT("Wax"), F(TEXT("Wax")) }, { TEXT("Brass"), MatBrass } });
	Dust(Build, Cabinet + FVector(0.f, 0.f, 78.f), FVector2D(46.f, 66.f), 0.45f);
	Blocker(Build, Cabinet + FVector(0.f, 0.f, 50.f), FVector(66.f, 46.f, 100.f));

	const FVector Column(PierA, SouthY() - 28.f, 0.f);
	Place(Build, LibraryProps::Pedestal, Column, FRotator(0.f, 12.f, 0.f), { { TEXT("Marble"), MatMarble }, { TEXT("Shadow"), F(TEXT("Shadow")) } });
	if (UStaticMeshComponent* Bust = Build.PropSeated(RoomProps::Bust, Column + FVector(0.f, -2.f, 112.f), FRotator(0.f, 180.f, 0.f), 56.f, false))
	{
		// marble_bust_01 is white marble; a tint multiplies, and white takes it: a dark bronze.
		FRoomShapes::TintSlots(Bust, FLinearColor(0.15f, 0.095f, 0.05f));
	}
	Blocker(Build, Column + FVector(0.f, 0.f, 80.f), FVector(42.f, 42.f, 160.f));

	const FVector4f Maps[2] = { LibraryWorldMap, LibraryParishMap };
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector At = WallPoint(EWall::South, i == 0 ? PierA : PierB, 232.f, 0.f);
		if (UStaticMeshComponent* Frame = Place(Build, LibraryProps::FrameMap, At, FRotator(0.f, 180.f, 0.f),
			{ { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } }))
		{
			LibraryNoDecals(Frame);
		}
		Picture(Build, TEXT("library_maps"), Maps[i], 2048.f, At + FVector(0.f, -1.2f, 0.f), FVector(0.f, -1.f, 0.f), 72.f, 72.f,
			FLinearColor(0.24f, 0.22f, 0.19f), 1.f);
	}

	// Over the arch, on the library side, the old man in black who built the house.
	const FVector Old = WallPoint(EWall::North, Setup.ArchX, 352.f, 0.f);
	if (UStaticMeshComponent* Frame = Place(Build, LibraryProps::FrameSmall, Old, FRotator(0.f, 0.f, 0.f),
		{ { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } }))
	{
		LibraryNoDecals(Frame);
	}
	Picture(Build, TEXT("library_paintings"), LibraryOldMan, 1024.f, Old + FVector(0.f, 1.2f, 0.f), FVector(0.f, 1.f, 0.f), 64.f, 80.f,
		FLinearColor(0.42f, 0.40f, 0.38f), 1.f);

	// The chess table north of the fire, a game left in the middle, the two chairs pushed back from it.
	const FVector Chess = ChessSeat();
	Place(Build, LibraryProps::ChessTable, Chess, FRotator(0.f, 0.f, 0.f),
		{ { TEXT("Mahogany"), MatMahogany }, { TEXT("Bone"), F(TEXT("Bone")) }, { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("Gilt"), F(TEXT("Gilt")) } });
	Blocker(Build, Chess + FVector(0.f, 0.f, 38.f), FVector(62.f, 62.f, 76.f));
	const TPair<FVector, float> ChessChairs[] = { { Chess + FVector(-4.f, -64.f, 0.f), 6.f }, { Chess + FVector(10.f, 70.f, 0.f), 196.f } };
	for (const TPair<FVector, float>& C : ChessChairs)
	{
		Place(Build, LibraryProps::SideChair, C.Key, FRotator(0.f, C.Value, 0.f), { { TEXT("Mahogany"), MatMahogany }, { TEXT("LeatherGreen"), MatLeatherGreen } });
		Blocker(Build, C.Key + FVector(0.f, 0.f, 45.f), FVector(44.f, 44.f, 90.f), C.Value);
	}
	Dust(Build, Chess + FVector(0.f, 0.f, 72.f), FVector2D(60.f, 60.f), 0.35f);

	// The display table of the rare books, standing out from the west bookcases: two folios under
	// the glass, one open on its velvet.
	USceneComponent* VitrineFrame = Pivot(VitrineSeat(), FRotator(0.f, -90.f, 0.f), TEXT("VitrineFrame"));
	FRoomBuilder V(this, VitrineFrame);
	Place(V, LibraryProps::Vitrine, FVector::ZeroVector, FRotator::ZeroRotator,
		{ { TEXT("Mahogany"), MatMahogany }, { TEXT("Velvet"), F(TEXT("Velvet")) }, { TEXT("Glass"), MatGlass }, { TEXT("Brass"), MatBrass } });
	Place(V, LibraryProps::OpenBook, FVector(-22.f, 2.f, 82.6f), FRotator(0.f, 180.f, 0.f),
		{ { TEXT("Leather"), MatLeather }, { TEXT("Card"), F(TEXT("Card")) }, { TEXT("Paper"), MatPaperSheet }, { TEXT("Velvet"), F(TEXT("Velvet")) } });
	Place(V, LibraryProps::BookPiles[1], FVector(34.f, -2.f, 82.6f), FRotator(0.f, 14.f, 0.f), { { TEXT("Books"), MatBooks } });
	Blocker(V, FVector(0.f, 0.f, 54.f), FVector(122.f, 62.f, 108.f));

	// The filing cabinet in the corner by the windows, the second drawer pulled out on its runners;
	// the month on the wall over it.
	USceneComponent* FilingFrame = Pivot(FilingSeat(), FRotator(0.f, FacingYaw(EWall::East), 0.f), TEXT("FilingFrame"));
	FRoomBuilder Fi(this, FilingFrame);
	Place(Fi, LibraryProps::FilingCabinet, FVector::ZeroVector, FRotator::ZeroRotator,
		{ { TEXT("Oak"), MatOak }, { TEXT("Brass"), MatBrass }, { TEXT("Shadow"), F(TEXT("Shadow")) } });
	Place(Fi, LibraryProps::FilingDrawer, FVector(0.f, 33.f + 34.f, 71.f), FRotator::ZeroRotator,
		{ { TEXT("Oak"), MatOak }, { TEXT("Brass"), MatBrass }, { TEXT("Card"), F(TEXT("Card")) } });
	Blocker(Fi, FVector(0.f, 14.f, 68.f), FVector(52.f, 96.f, 136.f));
	Dust(Fi, FVector(0.f, 0.f, 135.f), FVector2D(66.f, 50.f), 0.45f);

	// The chandelier, dead, over the middle of the room between the arch and the desk.
	const FVector Hook(-785.f, 1105.f, RoomHeight - 8.f);
	Build.Cyl(Hook + FVector(0.f, 0.f, 5.f), FRotator::ZeroRotator, FVector(54.f, 54.f, 4.f), MatOakBox, false);
	const float Chain = 96.f;
	for (int32 Link = 0; Link * 6.f < Chain; ++Link)
	{
		Build.Box(Hook - FVector(0.f, 0.f, 3.f + Link * 6.f), FRotator(0.f, (Link % 2) * 90.f, 0.f), FVector(1.f, 3.2f, 7.f), MatIron, false);
	}
	if (UStaticMeshComponent* Body = Build.Prop(RoomProps::Chandelier, Hook - FVector(0.f, 0.f, Chain), FRotator(0.f, 20.f, 0.f), 118.f, false))
	{
		FRoomShapes::TintSlots(Body, FLinearColor(0.24f, 0.21f, 0.17f), 0);
		FRoomShapes::TintSlots(Body, FLinearColor(0.55f, 0.55f, 0.56f), 1);
	}
	Build.Add(FRoomShapes::Plane(), Hook - FVector(0.f, 0.f, Chain + 40.f), FRotator(0.f, 30.f, 90.f), FVector(56.f, 42.f, 1.f), MatWeb, false);
}

void ALibraryActor::BuildCeiling(FRoomBuilder& Build)
{
	const float C = RoomHeight;
	Build.Mark(FVector(MidX(), MidY(), C), FRotator(0.f, 0.f, 180.f), FVector2D(EastX() - WestX(), SouthY() - NorthY()), MatCeiling);

	// Coffered: oak beams across both ways, a hand deep, the long ones laid with their length on
	// their local X (the builder's tiling rule).
	const float BeamW = 22.f;
	const float BeamD = 24.f;
	for (int32 k = 1; k < 5; ++k)
	{
		const float X = WestX() + (EastX() - WestX()) * k / 5.f;
		Build.Box(FVector(X, MidY(), C - BeamD * 0.5f), FRotator(0.f, 90.f, 0.f), FVector(SouthY() - NorthY(), BeamW, BeamD), MatOakBox, false);
	}
	for (int32 k = 1; k < 6; ++k)
	{
		const float Y = NorthY() + (SouthY() - NorthY()) * k / 6.f;
		Build.Box(FVector(MidX(), Y, C - BeamD * 0.5f + 1.f), FRotator::ZeroRotator, FVector(EastX() - WestX(), BeamW - 2.f, BeamD - 2.f), MatOakBox, false);
	}

	// Water through the ceiling over the windows and in one corner, and a crack across a coffer.
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = Random.FRandRange(WestX() + 80.f, EastX() - 80.f);
		const float Y = (i < 4) ? SouthY() - Random.FRandRange(40.f, 220.f) : Random.FRandRange(NorthY() + 80.f, SouthY() - 80.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float SizeV = Random.FRandRange(70.f, 160.f);
		const float SizeU = Random.FRandRange(90.f, 200.f);
		const FVector2D Size(SizeU, SizeV);
		const float Opacity = Random.FRandRange(0.35f, 0.6f);
		Build.Stain(RoomSurfaces::Damp, FVector(X, Y, C - 2.f), FRotator(90.f, 0.f, Roll), Size, FLinearColor(0.32f, 0.27f, 0.21f), Opacity, 1.2f);
	}
	Build.Crack(FVector(MidX() - 120.f, MidY() + 160.f, C - 2.f), FRotator(90.f, 0.f, 20.f), FVector2D(160.f, 120.f), 0.7f, 18.f);

	// Cobwebs: in the four upper corners, and slung between the bookcases' cornices and the ceiling.
	const FVector2D Corners[4] = { { WestX(), NorthY() }, { EastX(), NorthY() }, { WestX(), SouthY() }, { EastX(), SouthY() } };
	for (const FVector2D& Corner : Corners)
	{
		const float SX = Corner.X < MidX() ? 1.f : -1.f;
		const float SY = Corner.Y < MidY() ? 1.f : -1.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Inset = 24.f + i * 20.f;
			Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SX * Inset, Corner.Y + SY * Inset, C - 40.f - i * 8.f),
				FRotator(0.f, SX * SY > 0.f ? 45.f : -45.f, 180.f), FVector(Inset * 1.6f, Inset * 1.6f, 1.f), MatWeb, false);
		}
	}
	for (const FShelfRun& Run : ShelfRuns)
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const float U = FMath::Lerp(Run.U0, Run.U1, Random.FRandRange(0.15f, 0.85f));
			const float Size = Random.FRandRange(30.f, 52.f);
			const FVector At = WallPoint(Run.Wall, U, CaseTop + 16.f, UpperDepth + 4.f);
			Build.Add(FRoomShapes::Plane(), At, FRotator(0.f, FacingYaw(Run.Wall), 90.f + Random.FRandRange(-20.f, 20.f)), FVector(Size, Size * 0.6f, 1.f), MatWeb, false);
		}
	}
	// And from the top of the ladder to the rail.
	Build.Add(FRoomShapes::Plane(), FVector(WestX() + RailProud + 4.f, LadderY() + 24.f, RailZ - 18.f), FRotator(0.f, 0.f, 90.f), FVector(26.f, 30.f, 1.f), MatWeb, false);
}

void ALibraryActor::BuildDamage(FRoomBuilder& Build)
{
	const FLinearColor DampTint(0.46f, 0.38f, 0.30f);
	// Mould is a dark warm bloom, never green (the house's rule: under a cold window a green stain
	// reads as paint).
	const FLinearColor MouldTint(0.20f, 0.19f, 0.13f);
	auto Stain = [&](EWall Wall, float U, float Z, float SU, float SZ, const FLinearColor& Tint, float Opacity, float Roll, float Edge)
	{
		// The rolled footprint, for the keep-outs: a decal is tested by its whole extent, not its middle.
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		const float HalfU = (Cos * SU + Sin * SZ) * 0.5f;
		const float HalfZ = (Sin * SU + Cos * SZ) * 0.5f;
		if (WallDecalHitsSomething(Wall, U, Z, HalfU, HalfZ))
		{
			return;
		}
		FVector Location;
		FRotator Rotation;
		AimAt(Wall, U, Z, Roll, Location, Rotation);
		Build.Stain(RoomSurfaces::Damp, Location, Rotation, FVector2D(SU, SZ), Tint, Opacity, Edge);
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
		// Water down from the ceiling line, over the bookcases' tops: stains in the frieze. Every number
		// is drawn before the test, in order (09-29, 10-05).
		for (int32 i = 0; i < 5; ++i)
		{
			const float U = Random.FRandRange(R.U0 + 40.f, R.U1 - 40.f);
			const float SU = Random.FRandRange(80.f, 180.f);
			const float Opacity = Random.FRandRange(0.4f, 0.6f);
			Stain(R.Wall, U, RoomHeight - 26.f, SU, 40.f, DampTint * 0.7f, Opacity, 0.f, 1.25f);
		}
		// Faint blooms on the panelling wherever it shows, and mould low down.
		for (int32 i = 0; i < 6; ++i)
		{
			const float U = Random.FRandRange(R.U0 + 30.f, R.U1 - 30.f);
			const float Z = Random.FRandRange(30.f, CaseTop - 40.f);
			const float SU = Random.FRandRange(60.f, 150.f);
			const float SZ = Random.FRandRange(60.f, 140.f);
			const float Opacity = Random.FRandRange(0.12f, 0.24f);
			const float Roll = Random.FRandRange(0.f, 360.f);
			Stain(R.Wall, U, Z, SU, SZ, DampTint * 0.8f, Opacity, Roll, 1.15f);
		}
	}

	// Round the windows, where the rain has come in round the frames: patches, not a border. The
	// architraves stand there, so the patches are out on the panelling beyond them.
	for (const float X : WindowXs)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const float Side = (i % 2 == 0) ? -1.f : 1.f;
			const float Out = Random.FRandRange(34.f, 46.f);
			const float Z = Random.FRandRange(WindowSill, WindowTop - 60.f);
			const float SU = Random.FRandRange(16.f, 30.f);
			const float SZ = Random.FRandRange(26.f, 60.f);
			const float Opacity = Random.FRandRange(0.5f, 0.75f);
			FVector Location;
			FRotator Rotation;
			const float U = X + Side * (WindowHalf + Out);
			if (FMath::Abs(U - (WindowXs[0] + WindowXs[1]) * 0.5f) < 50.f || FMath::Abs(U - (WindowXs[1] + WindowXs[2]) * 0.5f) < 50.f)
			{
				// The piers carry a map each; the damp keeps clear of the frames.
				continue;
			}
			AimAt(EWall::South, U, Z, 0.f, Location, Rotation);
			Build.Stain(RoomSurfaces::Damp, Location, Rotation, FVector2D(SU, SZ), MouldTint, Opacity, 1.1f);
		}
	}
}

void ALibraryActor::SpawnWindows()
{
	// Three followers of the bedroom's storm in the south wall, the living room's three below and to
	// the west looking the same way. The living room's middle window has a view of its own (sky,
	// ground, trees) out there already; this room's middle one adds its own trees and rain in front
	// of that (bOwnTrees), because from up here the living room's treeline stands well off to the
	// west. No own view: a second sky and ground on the same side of the house stand two worlds
	// through each other (the 10-02 note).
	for (int32 i = 0; i < 3; ++i)
	{
		const FTransform Transform(FRotator(0.f, 90.f, 0.f),
			GetActorTransform().TransformPosition(FVector(WindowXs[i], SouthY() + Setup.WallThickness * 0.5f, 0.f)));
		AStormWindowActor* Window = GetWorld()->SpawnActorDeferred<AStormWindowActor>(AStormWindowActor::StaticClass(), Transform, this);
		if (!Window)
		{
			continue;
		}
		FStormWindowSetup WindowSetup;
		WindowSetup.OpeningWidth = WindowHalf * 2.f;
		WindowSetup.SillHeight = WindowSill;
		WindowSetup.TopHeight = WindowTop;
		WindowSetup.WallThickness = Setup.WallThickness;
		WindowSetup.bOwnTrees = (i == 1);
		// Three tall windows are a lot of sky, and this room is meant to be mostly dark.
		WindowSetup.PortalScale = 0.38f;
		// No through-the-walls glow: the girl's room is next door and the living room below, and both
		// have windows of their own. The strike comes in through the glass, on the sky portals.
		WindowSetup.GlowScale = 0.f;
		// Theatre drapes, after the user's reference: crimson velvet, wide, in deep folds, to the
		// floor, tied back on gold cords with tassels, gold fringe on what is left of the hem. The
		// east window's right-hand cord has given way and that drape hangs loose (nothing of the cord is left). The pole stands
		// further out for the folds, and the sill is a narrow board they fall past.
		WindowSetup.CurtainSurface = &LibraryVelvet;
		WindowSetup.CurtainTint = LibraryVelvetTint;
		WindowSetup.DrapeWidth = 86.f;
		WindowSetup.DrapeFold = 1.8f;
		WindowSetup.DrapeTie = 0.6f;
		WindowSetup.DrapeTieGone = (i == 2) ? 1 : -1;
		WindowSetup.bDrapeToFloor = true;
		WindowSetup.PoleProud = 24.f;
		WindowSetup.InnerSillProud = 10.f;
		WindowSetup.CurtainTrim = F(TEXT("GoldThread"));
		WindowSetup.Seed = 19590311 + i * 101;
		Window->Configure(WindowSetup);
		Window->SetLead(LeadStorm);
		Window->FinishSpawning(Transform);
		Windows.Add(Window);
	}
}

AClueActor* ALibraryActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
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

void ALibraryActor::BuildClues()
{
	// Where this room's clues will go. Set dressing only, as everywhere in the house until the game
	// is built end to end: no text, no prompt (the 09-30 decision).
	const FTransform Desk(FRotator(0.f, 180.f, 0.f), DeskSeat());
	auto OnDesk = [&](float X, float Y, float Z) { return Desk.TransformPosition(FVector(X, Y, DeskTop + Z)); };

	// His notebook, open on the blotter: the last thing he was writing.
	if (AClueActor* Notebook = SpawnClue(OnDesk(-2.f, -14.f, 0.9f), FRotator(0.f, 180.f + 4.f, 0.f)))
	{
		FRoomBuilder B(Notebook, Notebook->GetRootScene());
		Place(B, LibraryProps::Notebook, FVector::ZeroVector, FRotator::ZeroRotator, { { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("Paper"), MatPaperSheet } });
	}

	// The letter, opened and read, its envelope under it, and under both the small brass key.
	if (AClueActor* Letter = SpawnClue(OnDesk(-58.f, -22.f, 0.f), FRotator(0.f, 180.f - 14.f, 0.f)))
	{
		FRoomBuilder B(Letter, Letter->GetRootScene());
		Place(B, LibraryProps::Key, FVector(5.f, 6.f, 0.2f), FRotator(0.f, 40.f, 0.f), { { TEXT("Brass"), MatBrass } });
		Paper(B, TEXT("envelope"), FVector(2.f, 3.f, 0.35f), FVector::UpVector, FVector(0.f, 1.f, 0.f), 22.f, 12.9f);
		Place(B, LibraryProps::Letter, FVector(0.f, 0.f, 0.4f), FRotator::ZeroRotator, { { TEXT("Paper"), MatPaperSheet } });
	}

	// The family's papers in the drawer he left open: a letter, the index cards, an envelope.
	// In the part of the tray that is out of the desk: its front is 30 out from the pedestal.
	const FVector Drawer = Desk.TransformPosition(FVector(DeskWidth * 0.5f - 23.f, -DeskDepth * 0.5f - 30.f + 15.f, 55.2f));
	if (AClueActor* Papers = SpawnClue(Drawer, FRotator(0.f, 180.f, 0.f)))
	{
		FRoomBuilder B(Papers, Papers->GetRootScene());
		Paper(B, TEXT("letter"), FVector(0.f, 4.f, 0.4f), FVector::UpVector, FVector(0.f, 1.f, 0.f), 19.f, 26.8f);
		Paper(B, TEXT("card"), FVector(4.f, -10.f, 1.1f), FVector::UpVector, FVector(0.2f, 1.f, 0.f).GetSafeNormal(), 15.f, 9.4f);
		Paper(B, TEXT("envelope"), FVector(-6.f, 14.f, 0.8f), FVector::UpVector, FVector(-0.1f, 1.f, 0.f).GetSafeNormal(), 22.f, 12.9f);
	}

	// The calendar on the wall over the filing cabinet: October.
	if (AClueActor* Calendar = SpawnClue(WallPoint(EWall::East, FilingSeat().Y, 186.f, 1.5f), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Calendar, Calendar->GetRootScene());
		Paper(B, TEXT("calendar"), FVector::ZeroVector, FVector(-1.f, 0.f, 0.f), FVector::UpVector, 30.f, 42.f);
		B.Sph(FVector(0.4f, 0.f, 22.f), 1.4f, MatIron);
	}
}
