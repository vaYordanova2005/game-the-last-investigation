#include "MusicRoomActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "StormWindowActor.h"
#include "DustMotesComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Algo/Find.h"

namespace
{
	/** The generated models (Tools/make_music_room.py), /Game/Meshes/<name>. */
	namespace MusicProps
	{
		const TCHAR* Arch = TEXT("music_arch");
		const TCHAR* Cello = TEXT("music_cello");
		const TCHAR* ViolinCase = TEXT("music_violin_case");
		const TCHAR* ViolinBow = TEXT("music_violin_bow");
		const TCHAR* CelloBow = TEXT("music_cello_bow");
		const TCHAR* Guitar = TEXT("music_guitar");
		const TCHAR* StandIron = TEXT("music_stand_iron");
		const TCHAR* StandWood = TEXT("music_stand_wood");
		const TCHAR* Metronome = TEXT("music_metronome");
		const TCHAR* Chandelier = TEXT("music_chandelier");
		const TCHAR* CeilingRose = TEXT("music_ceiling_rose");
		const TCHAR* CeilingBead = TEXT("music_ceiling_bead");
		const TCHAR* Cabinet = TEXT("music_instrument_cabinet");
		const TCHAR* CabinetDoor = TEXT("music_cabinet_door");
		const TCHAR* Shelf = TEXT("music_display_shelf");
		const TCHAR* BookRows[3] = { TEXT("music_books_a"), TEXT("music_books_b"), TEXT("music_books_c") };
		const TCHAR* BooksLeaning = TEXT("music_books_leaning");
		const TCHAR* BooksPile = TEXT("music_books_pile");
		const TCHAR* Sideboard = TEXT("music_sideboard");
		const TCHAR* SideTable = TEXT("music_side_table");
		const TCHAR* Bench = TEXT("music_window_bench");
		const TCHAR* FrameCellist = TEXT("music_frame_cellist");
		const TCHAR* FrameLandscape = TEXT("music_frame_landscape");
		const TCHAR* FrameCertLand = TEXT("music_frame_cert_land");
		const TCHAR* FrameCertPort = TEXT("music_frame_cert_port");
		const TCHAR* MedalBox = TEXT("music_medal_box");
		const TCHAR* Photos[3] = { TEXT("music_photo_a"), TEXT("music_photo_b"), TEXT("music_photo_c") };
		const TCHAR* DriedRoses = TEXT("music_dried_roses");
		const TCHAR* DriedHydrangea = TEXT("music_dried_hydrangea");
		const TCHAR* Petals = TEXT("music_petals");
		const TCHAR* OpenScore = TEXT("music_open_score");
		// Shared with the library across the stairwell: one joiner and one cabinetmaker did the house.
		const TCHAR* WindowArch = TEXT("library_window_arch");
		const TCHAR* Cornice = TEXT("library_cornice");
		const TCHAR* SideChair = TEXT("library_side_chair");
	}

	/**
	 * The drapes' emerald velvet (Tools/make_music_art.py), tiling at the drapes' 34cm as the
	 * library's crimson does. Measured at linear (0.006, 0.072, 0.026); the tint takes it down to a
	 * dark bottle green, about the library's crimson's value.
	 */
	const FRoomSurface MusicVelvet{ TEXT("music_velvet"), 34.f };
	const FLinearColor MusicVelvetTint(1.4f, 0.45f, 0.7f);
	/** The silk damask over the panelling: one repeat every 64cm. Measured at linear (0.103, 0.068, 0.023). */
	constexpr float MusicDamaskRepeat = 64.f;

	/** The rectangles of the music_paper atlas (Tools/make_music_art.py: PAPER), in pixels. */
	struct FMusicRect
	{
		const TCHAR* Name;
		float X, Y, W, H;
	};
	const FMusicRect MusicPaperRects[] = {
		{ TEXT("score_a"), 0.f, 0.f, 362.f, 512.f },
		{ TEXT("score_b"), 512.f, 0.f, 362.f, 512.f },
		{ TEXT("manuscript_a"), 1024.f, 0.f, 362.f, 512.f },
		{ TEXT("manuscript_b"), 1536.f, 0.f, 362.f, 512.f },
		{ TEXT("notebook_l"), 1024.f, 512.f, 362.f, 512.f },
		{ TEXT("notebook_r"), 1536.f, 512.f, 362.f, 512.f },
		{ TEXT("programme"), 0.f, 1024.f, 362.f, 512.f },
		{ TEXT("programme_in"), 512.f, 1024.f, 362.f, 512.f },
		{ TEXT("cover"), 1536.f, 1536.f, 362.f, 512.f },
	};
	/** The certificates on music_paper (2048), and the pictures on music_paintings (1024). */
	const FVector4f MusicCertificateA(1024.f, 1024.f, 512.f, 362.f);
	const FVector4f MusicCertificateB(1536.f, 1024.f, 362.f, 512.f);
	const FVector4f MusicCertificateC(1024.f, 1536.f, 512.f, 362.f);
	const FVector4f MusicCellist(0.f, 0.f, 512.f, 640.f);
	const FVector4f MusicRiver(512.f, 0.f, 512.f, 384.f);
	const FVector4f MusicStillLife(512.f, 384.f, 512.f, 384.f);

	/** The house's dust, the colour it is everywhere. */
	const FLinearColor MusicDust(0.42f, 0.40f, 0.36f);

	/** The top of library_side_chair's stuffed seat (Tools/make_library.py: side_chair, seat_z + 1.8 + 3). */
	constexpr float MusicSideChairSeat = 50.8f;

	/** For a model's own UVs, already in repeats of the slot's texel size: tiling at one. */
	void MusicSheet(UMaterialInstanceDynamic* Mat)
	{
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}

	// A decal reaches nine centimetres either way along its aim (FRoomBuilder::Stain). The floor's are
	// aimed down from four over the boards (whose top is at zero), so they reach up to thirteen.
	constexpr float MusicDecalReach = 9.f;
	constexpr float MusicFloorDecalZ = 4.f;
	/** The sizes a patch of the floor's film is tried at, largest first, to lay it between the furniture. */
	constexpr float MusicFilmScales[] = { 1.f, 0.7f, 0.5f };

	/** A floor decal's half extent on the plan: aimed down, its first size runs along world Y and its second along X before the roll (09-27). */
	FVector2D MusicFloorDecalHalfExtent(const FVector2D& Size, float Roll)
	{
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		return FVector2D((Sin * Size.X + Cos * Size.Y) * 0.5f, (Cos * Size.X + Sin * Size.Y) * 0.5f);
	}

	/** Keeps the walls' decals off things pinned or hung on them (the kitchen calendar's note). */
	void MusicNoDecals(UPrimitiveComponent* Part)
	{
		if (Part)
		{
			Part->SetReceivesDecals(false);
		}
	}

	/**
	 * A sheet laid over pieces of a wall, its UVs in centimetres from the wall's own origin, so a
	 * pattern runs on across every cut as if the wall were papered in one (the living room's facade).
	 * Both windings, the normal forced (the 09-21 pane lesson).
	 */
	UProceduralMeshComponent* MusicFacade(AActor* Owner, USceneComponent* Parent, const TArray<FBox2D>& Pieces, const FVector& Origin,
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
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("Silk")));
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

AMusicRoomActor::AMusicRoomActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);
	RoomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void AMusicRoomActor::Configure(const FMusicRoomSetup& InSetup, AStormWindowActor* InLeadStorm)
{
	Setup = InSetup;
	LeadStorm = InLeadStorm;

	// Here and not in BeginPlay: the motes are scattered in the component's own BeginPlay, which runs
	// before the owner's (see ARoomDressingActor::Configure).
	DustMotes->ConfigureVolume(
		FVector((EastX() - WestX()) * 0.47f, (SouthY() - NorthY()) * 0.47f, RoomHeight * 0.46f),
		FVector(MidX(), MidY(), RoomHeight * 0.5f));

	Openings = { { EWall::South, ArchX, ArchHalf, 0.f, ArchCrown } };
	for (const float X : WindowXs)
	{
		Openings.Add({ EWall::North, X, WindowHalf, WindowSill, WindowTop });
	}
}

void AMusicRoomActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning this room never relays the hall's or the library's.
	Random.Initialize(19750621);

	FRoomBuilder Build(this, RoomRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildFloor(Build);
	BuildWalls(Build);
	BuildWindows(Build);
	BuildCeiling(Build);
	BuildChandelier(Build);
	BuildEnsemble(Build);
	BuildListening(Build);
	BuildWestWall(Build);
	BuildEastWall(Build);
	BuildSouthWall(Build);
	BuildScatteredMusic(Build);
	// The floor's dust once the furniture stands, so it can be kept from under what stands against the
	// walls; the windows and the clues before the damp on the walls, since the tiebacks' hooks are on
	// them. Nothing between BuildCeiling and here draws from Random.
	BuildFloorDust(Build);
	SpawnWindows();
	BuildClues();
	BuildDamage(Build);
}

// ---------------------------------------------------------------------------------------------
// Materials. The house's rule holds: nothing much above a tenth, and warm, because the only fill in
// here is the cold light of three windows. The wood is the library's; what this room adds is the
// green of its velvet, the old gold of its silk, the varnish of the instruments and the crystal.
// ---------------------------------------------------------------------------------------------

void AMusicRoomActor::CacheMaterials(FRoomBuilder& Build)
{
	// A sheet instance per surface for the generated models' own UVs, a hair off any tint the
	// builder's boxes use so the cache hands back a separate instance (the living room's rule).
	auto Sheet = [&](const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale = 1.f)
	{
		UMaterialInstanceDynamic* Mat = Build.Surface(Set, Tint + FLinearColor(0.0007f, 0.f, 0.f, 0.f), RoughnessScale);
		MusicSheet(Mat);
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
			MusicSheet(Mat);
		}
		return Mat;
	};

	// dark_paneled_wood at the hall's, the living room's and the library's values.
	MatPanel = Build.Surface(RoomSurfaces::Wainscot, FLinearColor(2.1f, 3.3f, 3.5f));
	MatPanelSheet = Sheet(RoomSurfaces::Wainscot, FLinearColor(2.1f, 3.3f, 3.5f));
	// The silk: old gold at linear (0.095, 0.063, 0.021) brought down to about the plaster's value in
	// the library, and kept warm (the house's rule: the only fill is the cold windows, and a neutral
	// wall comes back blue in front of them).
	MatSilk = Atlas(TEXT("music_damask"), FLinearColor(0.56f, 0.60f, 0.72f), 1.2f);
	// The plaster at the corridor's values, which the stair hall shares, on the hall's face of the arch.
	MatCorridorPlaster = Sheet(RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f));
	// A tall plaster ceiling a long way from three windows, as the library's; its rose, its beads and
	// its cornice in the same plaster, on the models' own UVs.
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.17f, 0.16f, 0.15f));
	MatPlasterSheet = Sheet(RoomSurfaces::Ceiling, FLinearColor(0.17f, 0.16f, 0.15f));
	// The floor's boards are made in BuildFloor, from their own asset (the room is wider than deep).
	// The library's oak and mahogany.
	MatOak = Sheet(RoomSurfaces::DarkOak, FLinearColor(0.16f, 0.12f, 0.095f), 2.2f);
	MatOakBox = Build.Surface(RoomSurfaces::DarkOak, FLinearColor(0.16f, 0.12f, 0.095f), 2.2f);
	MatMahogany = Sheet(RoomSurfaces::PianoWood, FLinearColor(1.55f, 1.35f, 1.45f), 1.8f);
	// The instruments' varnish: lacquered_cherry_wood (0.029, 0.012, 0.005) lifted to an old amber
	// brown, glossier than the furniture under its dust. Instruments are the one thing in the house
	// that was kept polished.
	// At (2.6, 2.2, 1.6) the cello came out the tan of a cardboard box a couple of metres from the lantern.
	MatVarnish = Sheet(RoomSurfaces::PianoWood, FLinearColor(1.9f, 1.4f, 1.15f), 1.3f);
	// Black leathercloth for the case, the library's green leather for the chairs' seats.
	MatLeather = Sheet(RoomSurfaces::Leather, FLinearColor(0.16f, 0.20f, 0.30f), 1.6f);
	MatLeatherGreen = Sheet(RoomSurfaces::Leather, FLinearColor(0.26f, 1.35f, 3.6f), 1.4f);
	MatVelvet = Sheet(MusicVelvet, MusicVelvetTint, 1.15f);
	MatBrass = Sheet(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatWax = Build.Flat(FLinearColor(0.34f, 0.30f, 0.22f), 0.55f);

	// The baked atlases, straight from their assets: the models carry UVs into them. The spines at
	// the library's third, the paper at a quarter: paler burns out under the lantern.
	// At the library's third the cream and ochre papers came out as pale slats; music is bound in
	// paper, which is paler than cloth and leather to begin with.
	MatBooks = Atlas(TEXT("music_books"), FLinearColor(0.25f, 0.24f, 0.22f), 1.f);
	MatPaperSheet = Atlas(TEXT("music_paper"), FLinearColor(0.26f, 0.24f, 0.21f), 1.f);

	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.06f, 0.035f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);

	struct FFlat { const TCHAR* Name; FLinearColor Color; float Roughness; float Metallic; };
	const FFlat FlatList[] = {
		{ TEXT("Shadow"), FLinearColor(0.002f, 0.002f, 0.002f), 1.f, 0.f },
		// Gilt under dust: a dull old gold that still takes a highlight (the library's).
		{ TEXT("Gilt"), FLinearColor(0.20f, 0.14f, 0.055f), 0.42f, 1.f },
		{ TEXT("Ebony"), FLinearColor(0.006f, 0.0055f, 0.005f), 0.35f, 0.f },
		// Cut crystal under sixty years of dust, opaque: a translucent drop a few centimetres across is
		// nothing at all in a room this dark, and two hundred of them are a sorting problem. Glossy and
		// dark (0.055), they were invisible from the floor; dust is what a crystal that old looks like,
		// a pale grey drop with its facets still catching the lantern.
		{ TEXT("Crystal"), FLinearColor(0.15f, 0.155f, 0.16f), 0.3f, 0.f },
		// The chandelier's gilt has the same dust on it: duller than the frames'.
		{ TEXT("DustyGilt"), FLinearColor(0.13f, 0.095f, 0.045f), 0.62f, 0.8f },
		{ TEXT("Candle"), FLinearColor(0.34f, 0.30f, 0.22f), 0.55f, 0.f },
		{ TEXT("Maple"), FLinearColor(0.20f, 0.15f, 0.09f), 0.6f, 0.f },
		{ TEXT("String"), FLinearColor(0.22f, 0.21f, 0.19f), 0.35f, 1.f },
		{ TEXT("Hair"), FLinearColor(0.24f, 0.22f, 0.18f), 0.7f, 0.f },
		{ TEXT("Bone"), FLinearColor(0.20f, 0.17f, 0.12f), 0.45f, 0.f },
		{ TEXT("Rosewood"), FLinearColor(0.03f, 0.012f, 0.008f), 0.4f, 0.f },
		{ TEXT("Inlay"), FLinearColor(0.12f, 0.10f, 0.06f), 0.45f, 0.f },
		// The case's plush and the medal box's ground, a wine red gone brown.
		{ TEXT("Plush"), FLinearColor(0.03f, 0.005f, 0.007f), 0.95f, 0.f },
		{ TEXT("Felt"), FLinearColor(0.012f, 0.028f, 0.017f), 1.f, 0.f },
		// The score's card covers: the back of the one on the iron stand faces the room, and at the
		// library's card it was the brightest thing in front of the windows.
		{ TEXT("Card"), FLinearColor(0.11f, 0.085f, 0.055f), 0.9f, 0.f },
		{ TEXT("Stem"), FLinearColor(0.040f, 0.030f, 0.016f), 0.95f, 0.f },
		{ TEXT("Petal"), FLinearColor(0.060f, 0.018f, 0.016f), 0.9f, 0.f },
		// Hydrangea dried to paper, a dusty mauve-brown: at 0.13 the heads read as white flowers.
		{ TEXT("Bloom"), FLinearColor(0.07f, 0.058f, 0.055f), 0.95f, 0.f },
		{ TEXT("Ribbon"), FLinearColor(0.08f, 0.012f, 0.012f), 0.8f, 0.f },
		{ TEXT("Medal"), FLinearColor(0.18f, 0.13f, 0.05f), 0.4f, 1.f },
		// The drapes' cord, braid and fringe: an old bronze silk, darker than the library's gold.
		{ TEXT("Cord"), FLinearColor(0.07f, 0.055f, 0.025f), 0.6f, 0.25f },
	};
	for (const FFlat& Flat : FlatList)
	{
		Flats.Add(FName(Flat.Name), Build.Flat(Flat.Color, Flat.Roughness, Flat.Metallic));
	}
}

UMaterialInterface* AMusicRoomActor::F(const TCHAR* Name) const
{
	const TObjectPtr<UMaterialInstanceDynamic>* Found = Flats.Find(FName(Name));
	return Found ? Found->Get() : MatShell.Get();
}

// ---------------------------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------------------------

void AMusicRoomActor::Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
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

UStaticMeshComponent* AMusicRoomActor::Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
	std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	Dress(Mesh, Slots);
	return Mesh;
}

void AMusicRoomActor::Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw)
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

USceneComponent* AMusicRoomActor::Pivot(const FVector& Location, const FRotator& Rotation, const TCHAR* Name)
{
	USceneComponent* Scene = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
	Scene->SetMobility(EComponentMobility::Movable);
	Scene->AttachToComponent(RoomRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Scene->SetRelativeLocationAndRotation(Location, Rotation);
	Scene->RegisterComponent();
	AddInstanceComponent(Scene);
	return Scene;
}

UStaticMeshComponent* AMusicRoomActor::Paper(FRoomBuilder& Build, const TCHAR* Rect, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H)
{
	// One instance per rectangle, made straight from the asset and never registered with the builder,
	// so Add() does not re-tile it to the plane's size (the library's paper).
	const FName Key(Rect);
	UMaterialInstanceDynamic* Mat = nullptr;
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = SheetMats.Find(Key))
	{
		Mat = Found->Get();
	}
	else
	{
		const FMusicRect* R = Algo::FindByPredicate(MusicPaperRects, [&](const FMusicRect& Each) { return FCString::Strcmp(Each.Name, Rect) == 0; });
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_music_paper.MI_music_paper"));
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
	UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Facing, FVector(W, H, 1.f), Mat ? Mat : MatShell.Get(), false);
	if (Sheet)
	{
		Sheet->SetCastShadow(false);
		Sheet->SetReceivesDecals(false);
	}
	return Sheet;
}

UStaticMeshComponent* AMusicRoomActor::Picture(FRoomBuilder& Build, const TCHAR* Set, const FVector4f& Rect, float SheetSize, const FVector& Centre,
	const FVector& Normal, float W, float H, const FLinearColor& Tint)
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
			Mat->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
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

void AMusicRoomActor::Dust(FRoomBuilder& Build, const FVector& Centre, const FVector2D& Size, float Opacity) const
{
	// From four centimetres over the top, aimed down: a decal reaches nine either way (09-29).
	Build.Stain(RoomSurfaces::Damp, Centre + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, 0.f), Size, MusicDust, Opacity, 1.35f);
}

void AMusicRoomActor::DeskFrame(const FVector& Seat, float Yaw, float DeskZ, FVector& OutCentre, FVector& OutNormal, FVector& OutUp) const
{
	// Tools/make_music_room.py leans the desk back by DESK_LEAN about its middle: in the stand's frame
	// it faces (0, cos, sin) and runs up (0, -sin, cos).
	const FRotator Turn(0.f, Yaw, 0.f);
	const float Lean = FMath::DegreesToRadians(DeskLean);
	OutCentre = Seat + Turn.RotateVector(FVector(0.f, 0.f, DeskZ));
	OutNormal = Turn.RotateVector(FVector(0.f, FMath::Cos(Lean), FMath::Sin(Lean)));
	OutUp = Turn.RotateVector(FVector(0.f, -FMath::Sin(Lean), FMath::Cos(Lean)));
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers, the library's: U runs along a wall (X on north/south, Y on east/west), Z is
// height, and a wall's normal points into the room.
// ---------------------------------------------------------------------------------------------

float AMusicRoomActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector AMusicRoomActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector AMusicRoomActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

float AMusicRoomActor::FacingYaw(EWall Wall)
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

TArray<FBox2D> AMusicRoomActor::Holes(EWall Wall) const
{
	TArray<FBox2D> Out;
	for (const FOpening& Opening : Openings)
	{
		if (Opening.Wall != Wall)
		{
			continue;
		}
		if (Opening.Wall == EWall::South)
		{
			// The arch's carved surround stands on its own board out to here: the walls stop at it.
			Out.Add(FBox2D(FVector2D(Opening.CenterU - ArchSurroundHalf, 0.f), FVector2D(Opening.CenterU + ArchSurroundHalf, ArchSurroundTop)));
		}
		else
		{
			Out.Add(FBox2D(FVector2D(Opening.CenterU - Opening.HalfU, Opening.BottomZ), FVector2D(Opening.CenterU + Opening.HalfU, Opening.TopZ)));
		}
	}
	return Out;
}

void AMusicRoomActor::PanelPiece(FRoomBuilder& Build, const FVector& Centre, float Yaw, float W, float H)
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
		// The library's panelling, each piece cut from its own part of the photograph, at the bigger
		// fields a room panelled for show was given (the library's 1.85).
		const float Texel = RoomSurfaces::Wainscot.TexelSizeCm * 1.85f;
		const int32 Index = PanelMats.Num();
		Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(2.1f, 3.3f, 3.5f));
		Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(W / Texel, H / Texel, 0.f, 1.f));
		Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(FMath::Frac(Index * 0.317f), FMath::Frac(Index * 0.533f), 0.f, 1.f));
		PanelMats.Add(Mat);
	}
	Build.Box(Centre, FRotator(0.f, Yaw, 0.f), FVector(W, 0.6f, H), Mat ? Mat : MatPanel.Get(), false);
}

void AMusicRoomActor::SilkSheet(EWall Wall, const TArray<FBox2D>& Pieces)
{
	const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
	const FVector Origin = bAlongX ? FVector(0.f, WallFace(Wall), 0.f) : FVector(WallFace(Wall), 0.f, 0.f);
	MusicFacade(this, RoomRoot, Pieces, Origin + WallNormal(Wall) * 0.4f, bAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f),
		WallNormal(Wall), MusicDamaskRepeat, MatSilk);
}

void AMusicRoomActor::AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const
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

bool AMusicRoomActor::WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfU, float HalfZ, bool bOpenings) const
{
	// A wall decal projects onto everything within reach of the wall (the wine cellar's 10-08 note):
	// the arch's surround stands out of the south wall to 300, the windows have their architraves
	// round them. A decal near any of those goes elsewhere.
	const float Margin = 15.f;
	if (bOpenings)
	{
		for (const FBox2D& Hole : Holes(Wall))
		{
			const float Surround = Wall == EWall::North ? 14.f : 0.f;
			if (U + HalfU > Hole.Min.X - Surround - Margin && U - HalfU < Hole.Max.X + Surround + Margin
				&& Z + HalfZ > Hole.Min.Y - Margin && Z - HalfZ < Hole.Max.Y + Surround + Margin)
			{
				return true;
			}
		}
	}
	// Against a piece of furniture, a hook or a nail a few centimetres are enough (the wine cellar's).
	const float KeepMargin = 5.f;
	for (const FWallKeepOut& Keep : WallKeepOuts)
	{
		if (Keep.Wall == Wall
			&& U + HalfU + KeepMargin > Keep.U.X && U - HalfU - KeepMargin < Keep.U.Y
			&& Z + HalfZ + KeepMargin > Keep.Z.X && Z - HalfZ - KeepMargin < Keep.Z.Y)
		{
			return true;
		}
	}
	return false;
}

FBox AMusicRoomActor::RoomBox(const UPrimitiveComponent* Part) const
{
	return Part->Bounds.GetBox().TransformBy(GetActorTransform().Inverse());
}

void AMusicRoomActor::KeepWallDecalsOff(const UPrimitiveComponent* Part)
{
	if (!Part || !Part->IsRegistered() || !Part->bReceivesDecals)
	{
		return;
	}
	// A wall decal is put two centimetres off the plaster and reaches nine either way from there, so
	// anything within a dozen of a wall is under its reach (the wine cellar's figure).
	const FBox Box = RoomBox(Part);
	const float Reach = 12.f;
	const FVector2D Up(Box.Min.Z, Box.Max.Z);
	if (Box.Min.Y < NorthY() + Reach)
	{
		WallKeepOuts.Add({ EWall::North, FVector2D(Box.Min.X, Box.Max.X), Up });
	}
	if (Box.Max.Y > SouthY() - Reach)
	{
		WallKeepOuts.Add({ EWall::South, FVector2D(Box.Min.X, Box.Max.X), Up });
	}
	if (Box.Max.X > EastX() - Reach)
	{
		WallKeepOuts.Add({ EWall::East, FVector2D(Box.Min.Y, Box.Max.Y), Up });
	}
	if (Box.Min.X < WestX() + Reach)
	{
		WallKeepOuts.Add({ EWall::West, FVector2D(Box.Min.Y, Box.Max.Y), Up });
	}
}

void AMusicRoomActor::KeepSconce(EWall Wall, float U, float Z)
{
	// FRoomBuilder::Sconce's backplate is 9 wide and 17 tall about its point, and its pan and candle
	// stand up to about 20 over it: all of it in reach of the wall's damp, and none of it brass with
	// damp on it.
	WallKeepOuts.Add({ Wall, FVector2D(U - 7.f, U + 7.f), FVector2D(Z - 10.f, Z + 22.f) });
}

void AMusicRoomActor::KeepWallDecalsOff(const AActor* Actor)
{
	if (!Actor)
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
	for (const UPrimitiveComponent* Part : Parts)
	{
		KeepWallDecalsOff(Part);
	}
}

void AMusicRoomActor::KeepFloorDustOff(const UPrimitiveComponent* Part)
{
	if (!Part || !Part->IsRegistered())
	{
		return;
	}
	const FBox Box = RoomBox(Part);
	if (Box.Min.Z < MusicFloorDecalZ + MusicDecalReach)
	{
		FloorKeepOuts.Add(FBox2D(FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Max.Y)));
	}
}

bool AMusicRoomActor::FloorDustHitsSomething(const FVector2D& Centre, const FVector2D& HalfExtent) const
{
	// No margin: a decal's box is hard-edged. What the dust must not do is run in under a cabinet and
	// smear up its face, or up the arch's sill.
	const FBox2D Footprint(Centre - HalfExtent, Centre + HalfExtent);
	for (const FBox2D& Keep : FloorKeepOuts)
	{
		if (Footprint.Intersect(Keep))
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------

void AMusicRoomActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float Z0 = -20.f;
	const float Z1 = RoomHeight + 10.f;

	// Solid, colliding and never seen. The south wall up to the top of the stair hall's north wall is
	// the hall's (it cuts the arch): only what is over it is this room's. The west and east walls go
	// from the floor: below it they stand on the kitchen's, and nothing is beside them up here.
	auto Wall = [&](const FVector& Min, const FVector& Max)
	{
		Build.Box((Min + Max) * 0.5f, FRotator::ZeroRotator, Max - Min, MatShell);
	};
	Wall(FVector(WestX() - T, NorthY() - T, Z0), FVector(WestX(), SouthY() + T, Z1));
	Wall(FVector(EastX(), NorthY() - T, Z0), FVector(EastX() + T, SouthY() + T, Z1));
	Wall(FVector(WestX() - T, SouthY(), Setup.HallWallTop - 5.f), FVector(EastX() + T, SouthY() + T, Z1));
	// The façade, cut round its windows.
	TArray<FBox2D> WindowHoles;
	for (const FOpening& Opening : Openings)
	{
		if (Opening.Wall == EWall::North)
		{
			WindowHoles.Add(FBox2D(FVector2D(Opening.CenterU - Opening.HalfU, Opening.BottomZ), FVector2D(Opening.CenterU + Opening.HalfU, Opening.TopZ)));
		}
	}
	for (const FBox2D& Piece : RoomWalls::CutAround(WindowHoles, WestX() - T, EastX() + T, Z0, Z1))
	{
		Wall(FVector(Piece.Min.X, NorthY() - T, Piece.Min.Y), FVector(Piece.Max.X, NorthY(), Piece.Max.Y));
	}

	// The ceiling, and a dark slab under the floor.
	Wall(FVector(WestX() - T, NorthY() - T, RoomHeight), FVector(EastX() + T, SouthY() + T, RoomHeight + 10.f));
	Wall(FVector(WestX(), NorthY(), -14.f), FVector(EastX(), SouthY(), -4.f));
}

void AMusicRoomActor::BuildFloor(FRoomBuilder& Build)
{
	// Dark boards wall to wall, laid towards the windows: one box, turned so its local X runs along
	// world Y, which is the way the photograph's boards run on the top face. It collides.
	//
	// The builder's tiling gives the larger repeat count to a part's local X, and over the kitchen this
	// room is wider (720) than it is deep (620): local X is the short side, and through the builder the
	// boards came out squeezed along their length and stretched across it. So the floor has an
	// instance of its own, made straight from the asset and never registered with a builder (which
	// would re-tile it), its repeats written out for each side (the wine cellar's door strip, 10-09).
	const float AlongBoards = SouthY() - NorthY();
	const float AcrossBoards = EastX() - WestX();
	// old_wooden_floor_02 at the corridor's tint is the boards upstairs; this floor was laid to be
	// polished, and is darker by half, and rough with dust.
	const FLinearColor Tint(0.31f, 0.28f, 0.26f);
	const float RoughnessScale = 1.3f;
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr,
		*FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), RoomSurfaces::Floorboards.Set, RoomSurfaces::Floorboards.Set));
	MatFloor = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
	if (MatFloor)
	{
		const float Texel = RoomSurfaces::Floorboards.TexelSizeCm;
		MatFloor->SetVectorParameterValue(TEXT("Tint"), Tint);
		MatFloor->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
		MatFloor->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(AlongBoards / Texel, AcrossBoards / Texel, 0.f, 1.f));
		MatFloor->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	else
	{
		// The art pipeline has not been run: the builder's flat fallback.
		MatFloor = Build.Surface(RoomSurfaces::Floorboards, Tint, RoughnessScale);
	}
	Build.Box(FVector(MidX(), MidY(), -2.f), FRotator(0.f, 90.f, 0.f), FVector(AlongBoards, AcrossBoards, 4.f), MatFloor);
}

void AMusicRoomActor::BuildWalls(FRoomBuilder& Build)
{
	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Walls[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), SouthY() },
	};
	const float SilkTop = RoomHeight - 24.f;
	for (const FRun& R : Walls)
	{
		const TArray<FBox2D> Cut = Holes(R.Wall);
		const bool bAlongX = R.Wall == EWall::North || R.Wall == EWall::South;
		const FRotator Along = bAlongX ? FRotator::ZeroRotator : FRotator(0.f, 90.f, 0.f);
		// Oak panelling to the dado, cut round the windows and the arch's surround.
		for (const FBox2D& Piece : RoomWalls::CutAround(Cut, R.U0, R.U1, 0.f, DadoTop))
		{
			PanelPiece(Build, WallPoint(R.Wall, Piece.GetCenter().X, Piece.GetCenter().Y, 0.6f), FacingYaw(R.Wall), Piece.GetSize().X, Piece.GetSize().Y);
		}
		// The dado rail over it and the skirting at its foot.
		for (const FBox2D& Piece : RoomWalls::CutAround(Cut, R.U0, R.U1, DadoTop - 1.f, DadoTop + 5.f))
		{
			Build.Box(WallPoint(R.Wall, Piece.GetCenter().X, Piece.GetCenter().Y, 2.f), Along, FVector(Piece.GetSize().X, 3.4f, Piece.GetSize().Y), MatOakBox, false);
		}
		for (const FBox2D& Piece : RoomWalls::CutAround(Cut, R.U0, R.U1, 0.f, 20.f))
		{
			Build.Box(WallPoint(R.Wall, Piece.GetCenter().X, Piece.GetCenter().Y, 2.f), Along, FVector(Piece.GetSize().X, 2.8f, Piece.GetSize().Y), MatOakBox, false);
		}
		// The silk over the rail, up behind the cornice.
		SilkSheet(R.Wall, RoomWalls::CutAround(Cut, R.U0, R.U1, DadoTop + 5.f, SilkTop));
	}

	// The plaster crown moulding where the walls meet the ceiling: the library's cornice laid end to
	// end along each wall and scaled to fit; at a corner the runs go through each other.
	struct FCorniceRun { FVector Start; FVector Dir; float Length; float Yaw; };
	const FCorniceRun Cornices[] = {
		{ FVector(WestX(), NorthY(), RoomHeight), FVector(1.f, 0.f, 0.f), EastX() - WestX(), 0.f },
		{ FVector(EastX(), SouthY(), RoomHeight), FVector(-1.f, 0.f, 0.f), EastX() - WestX(), 180.f },
		{ FVector(WestX(), SouthY(), RoomHeight), FVector(0.f, -1.f, 0.f), SouthY() - NorthY(), -90.f },
		{ FVector(EastX(), NorthY(), RoomHeight), FVector(0.f, 1.f, 0.f), SouthY() - NorthY(), 90.f },
	};
	for (const FCorniceRun& Run : Cornices)
	{
		const int32 Pieces = FMath::Max(1, FMath::CeilToInt(Run.Length / 100.f));
		const float Piece = Run.Length / Pieces;
		for (int32 i = 0; i < Pieces; ++i)
		{
			if (UStaticMeshComponent* Mesh = Place(Build, MusicProps::Cornice, Run.Start + Run.Dir * (Piece * (i + 0.5f)), FRotator(0.f, Run.Yaw, 0.f),
				{ { TEXT("Trim"), MatPlasterSheet } }))
			{
				Mesh->SetRelativeScale3D(FVector(Piece / 100.f, 1.f, 1.f));
				Mesh->SetReceivesDecals(false);
			}
		}
	}
}

void AMusicRoomActor::BuildWindows(FRoomBuilder& Build)
{
	// The three tall windows: oak architraves up the jambs, the reveals panelled, and the library's
	// round heads laid over the tops of the rectangular openings, so from inside each one is arched.
	const float Spring = WindowTop - WindowHalf;
	for (const float X : WindowXs)
	{
		for (const float Side : { -1.f, 1.f })
		{
			Build.Box(WallPoint(EWall::North, X + Side * (WindowHalf + 6.f), (WindowSill - 4.f + Spring) * 0.5f, 1.7f), FRotator::ZeroRotator,
				FVector(12.f, 2.8f, Spring - WindowSill + 4.f), MatOakBox, false);
			PanelPiece(Build, FVector(X + Side * (WindowHalf - 0.3f), NorthY() - 4.5f, (WindowSill + Spring) * 0.5f), Side > 0.f ? 90.f : -90.f,
				9.f, Spring - WindowSill);
		}
		if (UStaticMeshComponent* Head = Place(Build, MusicProps::WindowArch, FVector(X, NorthY(), WindowTop), FRotator::ZeroRotator,
			{ { TEXT("Trim"), MatPanelSheet }, { TEXT("Oak"), MatOak } }))
		{
			Head->SetReceivesDecals(false);
		}
	}
}

void AMusicRoomActor::BuildCeiling(FRoomBuilder& Build)
{
	const float C = RoomHeight;
	Build.Mark(FVector(MidX(), MidY(), C), FRotator(0.f, 0.f, 180.f), FVector2D(EastX() - WestX(), SouthY() - NorthY()), MatCeiling);

	// The ceiling's mouldings: a double bead run round a panel inset from the cornice, and a second
	// panel round the rose. music_ceiling_bead is a metre along X, laid end to end and scaled to fit.
	auto Bead = [&](float X0, float X1, float Y0, float Y1)
	{
		struct FSide { FVector Start; FVector Dir; float Length; float Yaw; };
		const FSide Sides[] = {
			{ FVector(X0, Y0, C), FVector(1.f, 0.f, 0.f), X1 - X0, 0.f },
			{ FVector(X0, Y1, C), FVector(1.f, 0.f, 0.f), X1 - X0, 0.f },
			{ FVector(X0, Y0, C), FVector(0.f, 1.f, 0.f), Y1 - Y0, 90.f },
			{ FVector(X1, Y0, C), FVector(0.f, 1.f, 0.f), Y1 - Y0, 90.f },
		};
		for (const FSide& S : Sides)
		{
			const int32 Pieces = FMath::Max(1, FMath::CeilToInt(S.Length / 100.f));
			const float Piece = S.Length / Pieces;
			for (int32 i = 0; i < Pieces; ++i)
			{
				if (UStaticMeshComponent* Mesh = Place(Build, MusicProps::CeilingBead, S.Start + S.Dir * (Piece * (i + 0.5f)), FRotator(0.f, S.Yaw, 0.f),
					{ { TEXT("Plaster"), MatPlasterSheet } }))
				{
					Mesh->SetRelativeScale3D(FVector(Piece / 100.f, 1.f, 1.f));
				}
			}
		}
	};
	const float Inset = 92.f;
	Bead(WestX() + Inset, EastX() - Inset, NorthY() + Inset, SouthY() - Inset);
	Bead(WestX() + Inset + 16.f, EastX() - Inset - 16.f, NorthY() + Inset + 16.f, SouthY() - Inset - 16.f);
	const FVector Hook = ChandelierHook();
	Bead(Hook.X - 150.f, Hook.X + 150.f, Hook.Y - 150.f, Hook.Y + 150.f);

	// Water through the ceiling over the windows and in a corner, and a crack across the panel.
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = Random.FRandRange(WestX() + 80.f, EastX() - 80.f);
		const float Y = (i < 4) ? NorthY() + Random.FRandRange(40.f, 220.f) : Random.FRandRange(NorthY() + 80.f, SouthY() - 80.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float SizeV = Random.FRandRange(70.f, 160.f);
		const float SizeU = Random.FRandRange(90.f, 200.f);
		const FVector2D Size(SizeU, SizeV);
		const float Opacity = Random.FRandRange(0.35f, 0.6f);
		Build.Stain(RoomSurfaces::Damp, FVector(X, Y, C - 2.f), FRotator(90.f, 0.f, Roll), Size, FLinearColor(0.32f, 0.27f, 0.21f), Opacity, 1.2f);
	}
	Build.Crack(FVector(MidX() + 140.f, MidY() - 180.f, C - 2.f), FRotator(90.f, 0.f, 30.f), FVector2D(180.f, 120.f), 0.7f, 18.f);

	// Cobwebs in the four upper corners.
	const FVector2D Corners[4] = { { WestX(), NorthY() }, { EastX(), NorthY() }, { WestX(), SouthY() }, { EastX(), SouthY() } };
	for (const FVector2D& Corner : Corners)
	{
		const float SX = Corner.X < MidX() ? 1.f : -1.f;
		const float SY = Corner.Y < MidY() ? 1.f : -1.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float In = 24.f + i * 20.f;
			Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SX * In, Corner.Y + SY * In, C - 40.f - i * 8.f),
				FRotator(0.f, SX * SY > 0.f ? 45.f : -45.f, 180.f), FVector(In * 1.6f, In * 1.6f, 1.f), MatWeb, false);
		}
	}
}

void AMusicRoomActor::BuildChandelier(FRoomBuilder& Build)
{
	// The crystal chandelier from the rose in the middle of the ceiling: dead, dusted grey, a few
	// drops gone and a swag broken, hanging a little off true from its chain, as fifty winters of the
	// house settling leave a thing that heavy.
	const FVector Hook = ChandelierHook();
	Place(Build, MusicProps::CeilingRose, Hook, FRotator(0.f, 11.f, 0.f), { { TEXT("Plaster"), MatPlasterSheet } });
	const float Chain = 62.f;
	const float RoseDepth = 9.f;
	for (int32 Link = 0; Link * 6.f < Chain; ++Link)
	{
		Build.Box(Hook - FVector(0.f, 0.f, RoseDepth + 3.f + Link * 6.f), FRotator(0.f, (Link % 2) * 90.f, 0.f), FVector(1.1f, 3.4f, 7.f), F(TEXT("DustyGilt")), false);
	}
	const FVector Top = Hook - FVector(0.f, 0.f, RoseDepth + Chain);
	Place(Build, MusicProps::Chandelier, Top, FRotator(2.5f, 15.f, -3.f),
		{ { TEXT("Gilt"), F(TEXT("DustyGilt")) }, { TEXT("Crystal"), F(TEXT("Crystal")) }, { TEXT("Candle"), F(TEXT("Candle")) } });
	// Webs strung between the arms, and one from the lowest tier down.
	for (int32 i = 0; i < 3; ++i)
	{
		const float A = 40.f + i * 115.f;
		const FVector At = Top + FRotator(0.f, A, 0.f).RotateVector(FVector(34.f, 0.f, -66.f));
		Build.Add(FRoomShapes::Plane(), At, FRotator(0.f, A + 90.f, 90.f), FVector(30.f, 22.f, 1.f), MatWeb, false);
	}
	Build.Add(FRoomShapes::Plane(), Top - FVector(0.f, 0.f, 112.f), FRotator(0.f, 30.f, 90.f), FVector(24.f, 34.f, 1.f), MatWeb, false);
}

void AMusicRoomActor::BuildEnsemble(FRoomBuilder& Build)
{
	// Under the west window, the bench, and on it the violin in its case, open.
	const FVector Bench = BenchSeat();
	UStaticMeshComponent* BenchMesh = Place(Build, MusicProps::Bench, Bench, FRotator::ZeroRotator, { { TEXT("Mahogany"), MatMahogany }, { TEXT("Velvet"), MatVelvet } });
	KeepFloorDustOff(BenchMesh);
	Blocker(Build, Bench + FVector(0.f, 0.f, 24.f), FVector(BenchL, BenchD, 48.f));
	Place(Build, MusicProps::ViolinCase, Bench + FVector(4.f, 1.f, BenchTop), FRotator(0.f, 3.f, 0.f),
		{ { TEXT("Leather"), MatLeather }, { TEXT("Plush"), F(TEXT("Plush")) }, { TEXT("Brass"), MatBrass }, { TEXT("Varnish"), MatVarnish },
		  { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("Maple"), F(TEXT("Maple")) }, { TEXT("String"), F(TEXT("String")) } });
	Build.Add(FRoomShapes::Plane(), Bench + FVector(-40.f, 0.f, 22.f), FRotator(0.f, 0.f, 90.f), FVector(30.f, 24.f, 1.f), MatWeb, false);

	// The cello's stand in front of the middle window, its desk to the chair behind it; the chair
	// pushed back and turned, as it is when somebody gets up and goes; the cello in its stand beside it.
	FVector Desk, Normal, Up;
	DeskFrame(WoodStandSeat(), WoodStandYaw(), WoodDeskZ, Desk, Normal, Up);
	// Everything standing in the middle of the floor keeps the floor's film of dust off its feet, read
	// off its bounds like what stands against the walls: a floor decal projects onto anything in its
	// box, and the ensemble is exactly where the film is laid.
	KeepFloorDustOff(Place(Build, MusicProps::StandWood, WoodStandSeat(), FRotator(0.f, WoodStandYaw(), 0.f), { { TEXT("Mahogany"), MatMahogany }, { TEXT("Brass"), MatBrass } }));
	Blocker(Build, WoodStandSeat() + FVector(0.f, 0.f, 50.f), FVector(40.f, 40.f, 100.f));
	// The bow laid on the ledge in front of the music, across the desk.
	{
		const FVector Right = FVector::CrossProduct(Normal, Up).GetSafeNormal();
		const FVector Frog = Desk + Up * (-DeskH * 0.5f - 0.4f) + Normal * 2.6f - Right * 35.f;
		Place(Build, MusicProps::CelloBow, Frog, FRotationMatrix::MakeFromXZ(Right, Up).Rotator(),
			{ { TEXT("Varnish"), MatVarnish }, { TEXT("Bone"), F(TEXT("Bone")) }, { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("String"), F(TEXT("String")) },
			  { TEXT("Hair"), F(TEXT("Hair")) } });
	}
	KeepFloorDustOff(Place(Build, MusicProps::SideChair, CellistChairSeat(), FRotator(0.f, 28.f, 0.f), { { TEXT("Mahogany"), MatMahogany }, { TEXT("LeatherGreen"), MatLeatherGreen } }));
	Blocker(Build, CellistChairSeat() + FVector(0.f, 0.f, 45.f), FVector(44.f, 44.f, 90.f), 28.f);
	KeepFloorDustOff(Place(Build, MusicProps::Cello, CelloSeat(), FRotator(0.f, 24.f, 0.f),
		{ { TEXT("Varnish"), MatVarnish }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("Maple"), F(TEXT("Maple")) },
		  { TEXT("String"), F(TEXT("String")) }, { TEXT("Mahogany"), MatMahogany }, { TEXT("Felt"), F(TEXT("Felt")) } }));
	Blocker(Build, CelloSeat() + FVector(0.f, 0.f, 68.f), FVector(52.f, 52.f, 136.f), 24.f);

	// The violinist's iron stand, an orchestral score open on its desk; her chair beside it with the
	// bow laid across the seat where she put it down.
	DeskFrame(IronStandSeat(), IronStandYaw(), IronDeskZ, Desk, Normal, Up);
	KeepFloorDustOff(Place(Build, MusicProps::StandIron, IronStandSeat(), FRotator(0.f, IronStandYaw(), 0.f), { { TEXT("Iron"), MatIron } }));
	Blocker(Build, IronStandSeat() + FVector(0.f, 0.f, 50.f), FVector(44.f, 44.f, 100.f));
	Place(Build, MusicProps::OpenScore, Desk + Up * (-DeskH * 0.5f - 0.35f) + Normal * 0.6f, FRotationMatrix::MakeFromZY(Normal, Up).Rotator(),
		{ { TEXT("Card"), F(TEXT("Card")) }, { TEXT("Paper"), MatPaperSheet } });
	const float ViolinChairYaw = -8.f;
	KeepFloorDustOff(Place(Build, MusicProps::SideChair, ViolinChairSeat(), FRotator(0.f, ViolinChairYaw, 0.f), { { TEXT("Mahogany"), MatMahogany }, { TEXT("LeatherGreen"), MatLeatherGreen } }));
	Blocker(Build, ViolinChairSeat() + FVector(0.f, 0.f, 45.f), FVector(44.f, 44.f, 90.f), ViolinChairYaw);
	{
		// The bow rests on its frog and its head across the stuffed seat.
		const FRotator Across(0.f, ViolinChairYaw + 74.f, 0.f);
		Place(Build, MusicProps::ViolinBow, ViolinChairSeat() + Across.RotateVector(FVector(-37.f, 0.f, 0.f)) + FVector(0.f, 0.f, MusicSideChairSeat + 0.1f), Across,
			{ { TEXT("Varnish"), MatVarnish }, { TEXT("Bone"), F(TEXT("Bone")) }, { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("String"), F(TEXT("String")) },
			  { TEXT("Hair"), F(TEXT("Hair")) } });
	}

	// The guitar on its stand in the corner by the east window, turned to the room. Within a decal's
	// reach of the east wall and of the floor's dust along its foot: kept off both, from its bounds.
	UStaticMeshComponent* Guitar = Place(Build, MusicProps::Guitar, GuitarSeat(), FRotator(0.f, 58.f, 0.f),
		{ { TEXT("Maple"), F(TEXT("Maple")) }, { TEXT("Rosewood"), F(TEXT("Rosewood")) }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Inlay"), F(TEXT("Inlay")) },
		  { TEXT("Bone"), F(TEXT("Bone")) }, { TEXT("Mahogany"), MatMahogany }, { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("String"), F(TEXT("String")) },
		  { TEXT("Iron"), MatIron }, { TEXT("Felt"), F(TEXT("Felt")) } });
	KeepWallDecalsOff(Guitar);
	KeepFloorDustOff(Guitar);
	Blocker(Build, GuitarSeat() + FVector(0.f, 0.f, 52.f), FVector(40.f, 40.f, 104.f), 58.f);
	Build.Add(FRoomShapes::Plane(), GuitarSeat() + FVector(0.f, 0.f, 8.f), FRotator(0.f, 58.f, 90.f), FVector(28.f, 14.f, 1.f), MatWeb, false);
	// No bust in the corner past the bench: the same marble_bust_01 already stands in the hall and in
	// the library (the user's call).
}

void AMusicRoomActor::BuildListening(FRoomBuilder& Build)
{
	// The two green armchairs, turned to where the music was played, and the table between them with
	// the metronome on it. Somebody sat here to listen; somebody else sat here to be played to.
	const FVector Ensemble = (WoodStandSeat() + IronStandSeat()) * 0.5f - FVector(0.f, 40.f, 0.f);
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector Seat = ArmchairSeat(i);
		const FVector To = (Ensemble - Seat).GetSafeNormal2D();
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(-To.X, To.Y)) + (i == 0 ? -6.f : 8.f);
		// GreenChair_01 faces its local +Y (its back is the long end of its 66), green velvet as shipped;
		// the living room's tint, and matt: under the lantern its sheen read as leather there (09-27).
		if (UStaticMeshComponent* Chair = Build.PropSeated(RoomProps::GreenChair, Seat, FRotator(0.f, Yaw, 0.f), 0.f, false))
		{
			// Darker than the living room's: up close to the lantern, at its tint the velvet came out mint.
			FRoomShapes::TintSlots(Chair, FLinearColor(0.42f, 0.85f, 0.72f));
			for (int32 Slot = 0; Slot < Chair->GetNumMaterials(); ++Slot)
			{
				if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Chair->GetMaterial(Slot)))
				{
					Instance->SetScalarParameterValue(TEXT("RoughnessScale"), 1.7f);
				}
			}
			KeepFloorDustOff(Chair);
		}
		Blocker(Build, Seat + FVector(0.f, 0.f, 50.f), FVector(70.f, 70.f, 100.f), Yaw);
		// A web slung between the front legs, low.
		Build.Add(FRoomShapes::Plane(), Seat + FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.f, 20.f, 10.f)), FRotator(0.f, Yaw, 90.f), FVector(40.f, 14.f, 1.f), MatWeb, false);
	}
	const FVector Table = SideTableSeat();
	KeepFloorDustOff(Place(Build, MusicProps::SideTable, Table, FRotator(0.f, 32.f, 0.f), { { TEXT("Mahogany"), MatMahogany } }));
	Blocker(Build, Table + FVector(0.f, 0.f, 33.f), FVector(50.f, 40.f, 66.f), 32.f);
	MusicNoDecals(Place(Build, MusicProps::Metronome, Table + FVector(-12.f, 8.f, SideTableTop), FRotator(0.f, 150.f, 0.f),
		{ { TEXT("Mahogany"), MatMahogany }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Brass"), MatBrass }, { TEXT("String"), F(TEXT("String")) } }));
	Dust(Build, Table + FVector(0.f, 0.f, SideTableTop), FVector2D(42.f, 56.f), 0.4f);
	// A programme fallen open beside the further chair, lying face up: its inside, the order of the
	// evening, to the ceiling.
	Paper(Build, TEXT("programme_in"), ArmchairSeat(1) + FVector(46.f, -38.f, 0.2f), FVector::UpVector, FRotator(0.f, 120.f, 0.f).RotateVector(FVector(1.f, 0.f, 0.f)), 15.f, 21.2f);
}

void AMusicRoomActor::BuildWestWall(FRoomBuilder& Build)
{
	const float Facing = FacingYaw(EWall::West);

	// The instrument cupboard, its right-hand glazed door standing open.
	USceneComponent* CabinetFrame = Pivot(CabinetSeat(), FRotator(0.f, Facing, 0.f), TEXT("CabinetFrame"));
	FRoomBuilder Ca(this, CabinetFrame);
	UStaticMeshComponent* Cabinet = Place(Ca, MusicProps::Cabinet, FVector::ZeroVector, FRotator::ZeroRotator,
		{ { TEXT("Mahogany"), MatMahogany }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Brass"), MatBrass }, { TEXT("Glass"), MatGlass },
		  { TEXT("Felt"), F(TEXT("Felt")) }, { TEXT("Leather"), MatLeather }, { TEXT("Card"), F(TEXT("Card")) } });
	KeepWallDecalsOff(Cabinet);
	KeepFloorDustOff(Cabinet);
	const FVector DoorHinge(CabinetW * 0.5f - 3.f, CabinetD * 0.5f - 2.f, 100.f);
	const FRotator DoorOpen(0.f, -72.f, 0.f);
	const float DoorLeaf = CabinetW * 0.5f - 4.f;
	Place(Ca, MusicProps::CabinetDoor, DoorHinge, DoorOpen, { { TEXT("Mahogany"), MatMahogany }, { TEXT("Glass"), MatGlass }, { TEXT("Brass"), MatBrass } });
	// The leaf stands out into the room at head height: it runs along its own -X from the hinge.
	Blocker(Ca, DoorHinge + DoorOpen.RotateVector(FVector(-DoorLeaf * 0.5f, 0.f, 0.f)) + FVector(0.f, 0.f, (CabinetH - 16.f - 100.f) * 0.5f),
		FVector(DoorLeaf, 6.f, CabinetH - 16.f - 100.f), DoorOpen.Yaw);
	Blocker(Ca, FVector(0.f, 0.f, CabinetH * 0.5f), FVector(CabinetW + 8.f, CabinetD + 4.f, CabinetH));
	Dust(Ca, FVector(0.f, 0.f, CabinetH), FVector2D(CabinetD + 4.f, CabinetW + 8.f), 0.5f);

	// The open shelf of music, its three boards of thin volumes in their publishers' colours, a few
	// gone over, a pile of the loose ones on top.
	USceneComponent* ShelfFrame = Pivot(ShelfSeat(), FRotator(0.f, Facing, 0.f), TEXT("ShelfFrame"));
	FRoomBuilder Sh(this, ShelfFrame);
	UStaticMeshComponent* Shelf = Place(Sh, MusicProps::Shelf, FVector::ZeroVector, FRotator::ZeroRotator, { { TEXT("Mahogany"), MatMahogany } });
	KeepWallDecalsOff(Shelf);
	KeepFloorDustOff(Shelf);
	Blocker(Sh, FVector(0.f, 0.f, ShelfTop * 0.5f), FVector(ShelfW + 4.f, ShelfD + 2.f, ShelfTop));
	{
		// Rows stand with their spines a centimetre and a half behind the boards' front edge; each board
		// is filled left to right with whatever rows fit, a gap where some were taken out.
		const float Spine = ShelfD * 0.5f - 1.5f;
		const float X0 = -ShelfInside * 0.5f + 1.f;
		struct FSlot { int32 Board; float X; int32 Row; };
		const FSlot Rows[] = {
			{ 0, 0.f, 0 }, { 0, 46.f, 1 }, { 0, 85.f, 2 },
			{ 1, 0.f, 2 }, { 1, 33.f, 0 }, { 1, 79.f, 1 },
			{ 2, 6.f, 1 }, { 2, 46.f, 2 },
		};
		for (const FSlot& Slot : Rows)
		{
			Place(Sh, MusicProps::BookRows[Slot.Row], FVector(X0 + Slot.X, Spine, ShelfBoards[Slot.Board]), FRotator::ZeroRotator, { { TEXT("Books"), MatBooks } });
		}
		// A few gone over against the shelf's far side, leaning to +X with their tops on it (the group is
		// 16.5 across with its lean), and a pile lying flat on the last board's end.
		Place(Sh, MusicProps::BooksLeaning, FVector(ShelfInside * 0.5f - 17.f, Spine, ShelfBoards[0]), FRotator::ZeroRotator, { { TEXT("Books"), MatBooks } });
		Place(Sh, MusicProps::BooksPile, FVector(X0 + 112.f, Spine + 1.f, ShelfBoards[2]), FRotator(0.f, 4.f, 0.f),
			{ { TEXT("Books"), MatBooks }, { TEXT("Paper"), MatPaperSheet } });
		Place(Sh, MusicProps::BooksPile, FVector(-36.f, Spine + 2.f, ShelfTop), FRotator(0.f, -10.f, 0.f), { { TEXT("Books"), MatBooks }, { TEXT("Paper"), MatPaperSheet } });
		Dust(Sh, FVector(0.f, 0.f, ShelfTop), FVector2D(ShelfD, ShelfW), 0.45f);
	}

	// Over it, the woman at her cello, in oils.
	const FVector Portrait = WallPoint(EWall::West, ShelfSeat().Y, 236.f, 0.f);
	MusicNoDecals(Place(Build, MusicProps::FrameCellist, Portrait, FRotator(0.f, Facing, 0.f), { { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } }));
	Picture(Build, TEXT("music_paintings"), MusicCellist, 1024.f, Portrait + WallNormal(EWall::West) * 1.4f, WallNormal(EWall::West), CellistArtW, CellistArtH,
		FLinearColor(0.42f, 0.40f, 0.38f));
	MusicNoDecals(Build.Sph(Portrait + WallNormal(EWall::West) * 1.f + FVector(0.f, 0.f, CellistArtH * 0.5f + 22.f), 1.6f, MatIron));
	// A sconce either side of her, a hand's width off the frame. The frame is the picture and ten of
	// gilt round it (Tools/make_music_room.py: music_frame_cellist), and a sconce's drip pan is five
	// either side of its point: fourteen of wall between the pan and the gilt. At 92 out, over the
	// kitchen, the southern one was four centimetres past the south wall's face with its backplate in
	// the corner; at this offset it keeps fourteen off that wall as well.
	const float SconceOut = CellistArtW * 0.5f + 10.f + 14.f + 5.f;
	ensureMsgf(ShelfSeat().Y + SconceOut + 5.f < SouthY() - 10.f, TEXT("the cellist's southern sconce has run into the south-west corner"));
	for (const float Side : { -1.f, 1.f })
	{
		const float U = ShelfSeat().Y + Side * SconceOut;
		Build.Sconce(WallPoint(EWall::West, U, 214.f, 0.f), 0.f, Side < 0.f ? 7.f : 4.f, MatBrass, MatWax, F(TEXT("Shadow")));
		KeepSconce(EWall::West, U, 214.f);
	}

	// A pile of scores on the boards between the cupboard and the shelf, left where they were put.
	KeepFloorDustOff(Place(Build, MusicProps::BooksPile, FVector(WestX() + 22.f, -306.f, 0.f), FRotator(0.f, 98.f, 0.f), { { TEXT("Books"), MatBooks }, { TEXT("Paper"), MatPaperSheet } }));
}

void AMusicRoomActor::BuildEastWall(FRoomBuilder& Build)
{
	const float Facing = FacingYaw(EWall::East);

	// The sideboard, and on it the family: three photographs on their easels, roses dried in their
	// vase with petals fallen round it, hydrangea gone to paper in the brass one. Built in its own
	// frame: local X along it (to the south), +Y out into the room, the top at SideboardTop.
	USceneComponent* BoardFrame = Pivot(SideboardSeat(), FRotator(0.f, Facing, 0.f), TEXT("SideboardFrame"));
	FRoomBuilder S(this, BoardFrame);
	UStaticMeshComponent* Board = Place(S, MusicProps::Sideboard, FVector::ZeroVector, FRotator::ZeroRotator,
		{ { TEXT("Mahogany"), MatMahogany }, { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("Brass"), MatBrass } });
	KeepWallDecalsOff(Board);
	KeepFloorDustOff(Board);
	Blocker(S, FVector(0.f, 0.f, SideboardTop * 0.5f), FVector(SideboardW + 4.f, SideboardD + 2.f, SideboardTop));
	const float Top = SideboardTop;
	if (UStaticMeshComponent* Vase = S.PropSeated(RoomProps::CeramicVase, FVector(-60.f, -6.f, Top), FRotator(0.f, 30.f, 0.f), 40.f, false))
	{
		// antique_ceramic_vase_01 is white glazed china with a blue print; held down to old china's value.
		FRoomShapes::TintSlots(Vase, FLinearColor(0.22f, 0.21f, 0.19f));
		KeepWallDecalsOff(Vase);
	}
	// The flowers lean out of their vases towards the wall as well as the room, inside the reach of the
	// silk's damp: kept off, from their bounds, like the vases.
	KeepWallDecalsOff(Place(S, MusicProps::DriedRoses, FVector(-60.f, -6.f, Top + 39.f), FRotator(0.f, 10.f, 0.f),
		{ { TEXT("Stem"), F(TEXT("Stem")) }, { TEXT("Petal"), F(TEXT("Petal")) } }));
	KeepWallDecalsOff(Place(S, MusicProps::Petals, FVector(-50.f, 8.f, Top), FRotator(0.f, 20.f, 0.f), { { TEXT("Petal"), F(TEXT("Petal")) } }));
	if (UStaticMeshComponent* Vase = S.PropSeated(RoomProps::BrassVase, FVector(60.f, -8.f, Top), FRotator(0.f, -20.f, 0.f), 44.f, false))
	{
		// brass_vase_01's polished brass, tarnished: at 0.42 it read as cream china.
		FRoomShapes::TintSlots(Vase, FLinearColor(0.24f, 0.19f, 0.13f));
		KeepWallDecalsOff(Vase);
	}
	KeepWallDecalsOff(Place(S, MusicProps::DriedHydrangea, FVector(60.f, -8.f, Top + 43.f), FRotator(0.f, 40.f, 0.f),
		{ { TEXT("Stem"), F(TEXT("Stem")) }, { TEXT("Bloom"), F(TEXT("Bloom")) } }));
	Dust(S, FVector(0.f, 0.f, Top), FVector2D(SideboardD, SideboardW), 0.45f);

	// Over it, what she won: three certificates and the medal in its box, framed and hung together, all
	// clear of the dried flowers under them (the roses come up to 148 and the hydrangea to 161).
	struct FAward { const TCHAR* Frame; FVector4f Rect; float U; float Z; float W; float H; };
	const FAward Awards[] = {
		{ MusicProps::FrameCertLand, MusicCertificateA, -398.f, 180.f, CertLong, CertShort },
		{ MusicProps::FrameCertPort, MusicCertificateB, -262.f, 190.f, CertShort, CertLong },
		{ MusicProps::FrameCertLand, MusicCertificateC, -330.f, 238.f, CertLong, CertShort },
	};
	const FVector In = WallNormal(EWall::East);
	for (const FAward& Award : Awards)
	{
		const FVector At = WallPoint(EWall::East, Award.U, Award.Z, 0.f);
		MusicNoDecals(Place(Build, Award.Frame, At, FRotator(0.f, Facing, 0.f), { { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } }));
		Picture(Build, TEXT("music_paper"), Award.Rect, 2048.f, At + In * 1.0f, In, Award.W, Award.H, FLinearColor(0.19f, 0.175f, 0.15f));
		MusicNoDecals(Build.Sph(At + In * 1.f + FVector(0.f, 0.f, Award.H * 0.5f + 10.f), 1.2f, MatIron));
	}
	MusicNoDecals(Place(Build, MusicProps::MedalBox, WallPoint(EWall::East, -330.f, 180.f, 0.f), FRotator(0.f, Facing, 0.f),
		{ { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Plush"), F(TEXT("Plush")) }, { TEXT("Ribbon"), F(TEXT("Ribbon")) }, { TEXT("Medal"), F(TEXT("Medal")) } }));
	for (const float U : { -472.f, -188.f })
	{
		Build.Sconce(WallPoint(EWall::East, U, 196.f, 0.f), 180.f, U < -300.f ? 9.f : 5.f, MatBrass, MatWax, F(TEXT("Shadow")));
		KeepSconce(EWall::East, U, 196.f);
	}

	// North of the sideboard, over the guitar, the violin and the score on a table, in oils.
	{
		const FVector At = WallPoint(EWall::East, -566.f, 214.f, 0.f);
		MusicNoDecals(Place(Build, MusicProps::FrameLandscape, At, FRotator(0.f, Facing, 0.f), { { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } }));
		Picture(Build, TEXT("music_paintings"), MusicStillLife, 1024.f, At + In * 1.4f, In, LandscapeArtW, LandscapeArtH, FLinearColor(0.42f, 0.40f, 0.38f));
		MusicNoDecals(Build.Sph(At + In * 1.f + FVector(0.f, 0.f, LandscapeArtH * 0.5f + 20.f), 1.6f, MatIron));
	}
}

void AMusicRoomActor::BuildSouthWall(FRoomBuilder& Build)
{
	const float Facing = FacingYaw(EWall::South);
	const FVector In = WallNormal(EWall::South);

	// The arch from the stair hall's north gallery: one model through the wall, its hall face cased
	// like the hall's archway and its room face a carved oak surround. It is drawn with its outer face
	// at -Y; here the hall is at +Y, so it is turned round. The hall's plaster is at the corridor's
	// values, and its joinery the staircase oak, RoughWood at (0.290, 0.272, 0.250), here on its own
	// instance a hair off with the tiling at one. No decals on it: the hall's damp is laid without a test.
	UMaterialInstanceDynamic* HallTrim = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.2907f, 0.272f, 0.250f));
	MusicSheet(HallTrim);
	if (UStaticMeshComponent* Arch = Place(Build, MusicProps::Arch, FVector(ArchX, SouthY() + Setup.WallThickness * 0.5f, 0.f), FRotator(0.f, 180.f, 0.f),
		{ { TEXT("Shadow"), F(TEXT("Shadow")) }, { TEXT("CorridorWall"), MatCorridorPlaster }, { TEXT("RoomWall"), MatPanelSheet },
		  { TEXT("Oak"), MatOak }, { TEXT("Trim"), HallTrim } }))
	{
		Arch->SetReceivesDecals(false);
	}
	// The sill through the arch is the model's; the dust keeps off it and the surround's plinths.
	FloorKeepOuts.Add(FBox2D(FVector2D(ArchX - ArchSurroundHalf, SouthY() - 60.f), FVector2D(ArchX + ArchSurroundHalf, SouthY() + 1.f)));

	// West of the arch, the long-case clock, stopped. vintage_grandfather_clock_01 faces its local +Y
	// and its back is 18.8 behind its origin: turned to face out of this wall, its origin goes 19.8
	// into the room so its back is on the plaster.
	if (UStaticMeshComponent* Clock = Build.Prop(RoomProps::LongcaseClock, ClockSeat() + In * 19.8f, FRotator(0.f, Facing, 0.f), 0.f, false))
	{
		FRoomShapes::TintSlots(Clock, FLinearColor(0.42f, 0.38f, 0.34f), 0);
		if (Clock->GetNumMaterials() > 1)
		{
			Clock->SetMaterial(1, Build.Glass(RoomPalette::GlassShard, 0.035f, 0.06f));
		}
		KeepWallDecalsOff(Clock);
		KeepFloorDustOff(Clock);
	}
	Blocker(Build, ClockSeat() + In * 22.f + FVector(0.f, 0.f, 110.f), FVector(64.f, 44.f, 220.f));
	// Aimed down, the first size runs along world Y: 44 out from the wall, 64 along it (the 09-27 note).
	Dust(Build, ClockSeat() + In * 22.f + FVector(0.f, 0.f, 219.f), FVector2D(44.f, 64.f), 0.45f);

	// Between the clock and the arch, the river at dusk, and a sconce between it and the arch. The
	// arch is close to the east wall: there is no wall beside it on that side.
	{
		const FVector At = WallPoint(EWall::South, -1760.f, 214.f, 0.f);
		MusicNoDecals(Place(Build, MusicProps::FrameLandscape, At, FRotator(0.f, Facing, 0.f), { { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Shadow"), F(TEXT("Shadow")) } }));
		Picture(Build, TEXT("music_paintings"), MusicRiver, 1024.f, At + In * 1.4f, In, LandscapeArtW, LandscapeArtH, FLinearColor(0.42f, 0.40f, 0.38f));
		MusicNoDecals(Build.Sph(At + In * 1.f + FVector(0.f, 0.f, LandscapeArtH * 0.5f + 20.f), 1.6f, MatIron));
	}
	{
		const float U = ArchX - ArchSurroundHalf - 44.f;
		Build.Sconce(WallPoint(EWall::South, U, 200.f, 0.f), -90.f, 6.f, MatBrass, MatWax, F(TEXT("Shadow")));
		KeepSconce(EWall::South, U, 200.f);
	}
}

void AMusicRoomActor::BuildScatteredMusic(FRoomBuilder& Build)
{
	// Sheets of music slid off the stands onto the boards round them, and one carried halfway to the
	// arch: the music stopped one day and nobody picked it up. Its own stream, every number drawn.
	FRandomStream Sheets(6201);
	const TCHAR* Rects[] = { TEXT("score_a"), TEXT("score_b"), TEXT("manuscript_a"), TEXT("score_b"), TEXT("score_a"), TEXT("cover"), TEXT("manuscript_a") };
	const FVector2D Spots[] = { { -1699.f, -360.f }, { -1668.f, -318.f }, { -1564.f, -372.f }, { -1797.f, -352.f }, { -1502.f, -430.f }, { -1731.f, -300.f }, { -1650.f, -180.f } };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Spots); ++i)
	{
		const float Turn = Sheets.FRandRange(0.f, 360.f);
		const float DX = Sheets.FRandRange(-12.f, 12.f);
		const float DY = Sheets.FRandRange(-12.f, 12.f);
		Paper(Build, Rects[i], FVector(Spots[i].X + DX, Spots[i].Y + DY, 0.15f + i * 0.03f), FVector::UpVector,
			FRotator(0.f, Turn, 0.f).RotateVector(FVector(1.f, 0.f, 0.f)), 21.f, 29.7f);
	}
	// Two lying on top of each other under the iron stand, where the score slipped.
	Paper(Build, TEXT("score_a"), IronStandSeat() + FVector(6.f, 18.f, 0.3f), FVector::UpVector, FVector(0.2f, -1.f, 0.f).GetSafeNormal(), 21.f, 29.7f);
}

void AMusicRoomActor::BuildFloorDust(FRoomBuilder& Build)
{
	// Dust along the feet of the walls, where nobody's feet went. Aimed down, the first size runs along
	// world Y: roll 90 along the north and south walls, none along the east and west (the 09-27 note).
	// Kept off the arch's sill and off whatever stands on the floor. A skipped strip has drawn all its
	// numbers (09-29).
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
		case 0: Spot = FVector2D(FMath::Lerp(WestX() + 60.f, EastX() - 60.f, Along), NorthY() + Out); Roll = 90.f; break;
		case 1: Spot = FVector2D(FMath::Lerp(WestX() + 60.f, EastX() - 60.f, Along), SouthY() - Out); Roll = 90.f; break;
		case 2: Spot = FVector2D(EastX() - Out, FMath::Lerp(NorthY() + 60.f, SouthY() - 60.f, Along)); break;
		default: Spot = FVector2D(WestX() + Out, FMath::Lerp(NorthY() + 60.f, SouthY() - 60.f, Along)); break;
		}
		if (FloorDustHitsSomething(Spot, MusicFloorDecalHalfExtent(FVector2D(Length, Width), Roll + Turn)))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(Spot.X, Spot.Y, MusicFloorDecalZ), FRotator(-90.f, 0.f, Roll + Turn), FVector2D(Length, Width), MusicDust, Opacity, 1.35f);
	}
	// A thin film over the whole floor, kept out from under the furniture like the rest. The middle of
	// this room is the ensemble's, and at its drawn size a film lands on a stand's, a chair's or the
	// cello's feet almost anywhere in it (all seven did, once they were kept off): one that would is
	// laid smaller, and left out only when even half its size reaches something. Every number is drawn
	// before the test (09-29).
	for (int32 i = 0; i < 7; ++i)
	{
		const float X = Random.FRandRange(WestX() + 140.f, EastX() - 140.f);
		const float Y = Random.FRandRange(NorthY() + 160.f, SouthY() - 160.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float SizeV = Random.FRandRange(130.f, 220.f);
		const float SizeU = Random.FRandRange(170.f, 260.f);
		const float Opacity = Random.FRandRange(0.14f, 0.22f);
		const float* Fit = Algo::FindByPredicate(MusicFilmScales, [&](float Scale)
		{
			return !FloorDustHitsSomething(FVector2D(X, Y), MusicFloorDecalHalfExtent(FVector2D(SizeU, SizeV) * Scale, Roll));
		});
		if (!Fit)
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(X, Y, MusicFloorDecalZ), FRotator(-90.f, 0.f, Roll), FVector2D(SizeU, SizeV) * *Fit, MusicDust, Opacity, 1.4f);
	}
	// Where the dust never settled: in from the arch, and on between the two stands to the players'
	// chairs, and across to the armchairs. Each patch is 150 along world Y and 80 across (aimed down,
	// no roll), laid between what stands there rather than under it: over the kitchen two of them
	// were under the cello's stand and the nearer armchair. The first starts clear of the arch's sill.
	const FVector2D PathHalf = MusicFloorDecalHalfExtent(FVector2D(150.f, 80.f), 0.f);
	const FVector2D Path[] = {
		{ ArchX - 20.f, SouthY() - 60.f - PathHalf.Y - 2.f },
		{ -1560.f, -300.f },
		// The gap between the two stands' feet is 83 across, off centre between their seats.
		{ -1682.f, -350.f },
		{ -1640.f, -215.f },
	};
	for (const FVector2D& P : Path)
	{
		if (FloorDustHitsSomething(P, PathHalf))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(P.X, P.Y, MusicFloorDecalZ), FRotator(-90.f, 0.f, 0.f), FVector2D(150.f, 80.f),
			FLinearColor(0.10f, 0.085f, 0.07f), 0.28f, 1.3f, 0.8f);
	}
	// The rain in at the sills for years, under each window: out from the wall along world Y, along it
	// along X (no roll, 09-27), its box kept off the skirting's face (the library's figures).
	// Not under the bench: its legs and stretchers are within the decal's reach.
	for (const float X : WindowXs)
	{
		if (FloorDustHitsSomething(FVector2D(X, NorthY() + 42.f), MusicFloorDecalHalfExtent(FVector2D(70.f, 140.f), 0.f)))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(X, NorthY() + 42.f, MusicFloorDecalZ), FRotator(-90.f, 0.f, 0.f), FVector2D(70.f, 140.f),
			FLinearColor(0.16f, 0.12f, 0.09f), 0.6f, 0.55f, 0.6f);
	}
}

void AMusicRoomActor::BuildDamage(FRoomBuilder& Build)
{
	const FLinearColor DampTint(0.46f, 0.38f, 0.30f);
	// Mould is a dark warm bloom, never green (the house's rule).
	const FLinearColor MouldTint(0.20f, 0.19f, 0.13f);
	auto Stain = [&](EWall Wall, float U, float Z, float SU, float SZ, const FLinearColor& Tint, float Opacity, float Roll, float Edge)
	{
		// The rolled footprint, for the keep-outs: a decal is tested by its whole extent, not its middle.
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		if (WallDecalHitsSomething(Wall, U, Z, (Cos * SU + Sin * SZ) * 0.5f, (Sin * SU + Cos * SZ) * 0.5f))
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
		// Water down from the ceiling line, in the silk under the cornice. Every number is drawn before
		// the test, in order (09-29, 10-05).
		for (int32 i = 0; i < 5; ++i)
		{
			const float U = Random.FRandRange(R.U0 + 40.f, R.U1 - 40.f);
			const float SU = Random.FRandRange(80.f, 180.f);
			const float SZ = Random.FRandRange(60.f, 140.f);
			const float Opacity = Random.FRandRange(0.4f, 0.6f);
			Stain(R.Wall, U, RoomHeight - 40.f - SZ * 0.3f, SU, SZ, DampTint * 0.7f, Opacity, 0.f, 1.25f);
		}
		// Faint blooms on the silk and the panelling, and mould low down.
		for (int32 i = 0; i < 6; ++i)
		{
			const float U = Random.FRandRange(R.U0 + 30.f, R.U1 - 30.f);
			const float Z = Random.FRandRange(30.f, RoomHeight - 80.f);
			const float SU = Random.FRandRange(60.f, 150.f);
			const float SZ = Random.FRandRange(60.f, 140.f);
			const float Opacity = Random.FRandRange(0.12f, 0.24f);
			const float Roll = Random.FRandRange(0.f, 360.f);
			Stain(R.Wall, U, Z, SU, SZ, DampTint * 0.8f, Opacity, Roll, 1.15f);
		}
		// A crack or two in the plaster behind the silk, where the house has moved.
		for (int32 i = 0; i < 2; ++i)
		{
			const float U = Random.FRandRange(R.U0 + 60.f, R.U1 - 60.f);
			const float Z = Random.FRandRange(DadoTop + 60.f, RoomHeight - 60.f);
			const float SU = Random.FRandRange(40.f, 80.f);
			const float SZ = Random.FRandRange(80.f, 160.f);
			const float Roll = Random.FRandRange(-20.f, 20.f);
			// Tested by its rolled footprint, as the stains are: at twenty degrees a 160-tall crack
			// leans its ends out by a third of its height.
			const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
			const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
			if (!WallDecalHitsSomething(R.Wall, U, Z, (Cos * SU + Sin * SZ) * 0.5f, (Sin * SU + Cos * SZ) * 0.5f))
			{
				FVector Location;
				FRotator Rotation;
				AimAt(R.Wall, U, Z, Roll, Location, Rotation);
				Build.Crack(Location, Rotation, FVector2D(SU, SZ), 0.8f, 30.f);
			}
		}
	}

	// Round the windows, where the rain has come in round the frames: patches on the silk beyond the
	// architraves, kept off the tiebacks' hooks.
	for (const float X : WindowXs)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const float Side = (i % 2 == 0) ? -1.f : 1.f;
			const float Out = Random.FRandRange(34.f, 46.f);
			const float Z = Random.FRandRange(WindowSill + 80.f, WindowTop - 60.f);
			const float SU = Random.FRandRange(16.f, 30.f);
			const float SZ = Random.FRandRange(26.f, 60.f);
			const float Opacity = Random.FRandRange(0.5f, 0.75f);
			const float U = X + Side * (WindowHalf + Out);
			if (WallDecalHitsSomething(EWall::North, U, Z, SU * 0.5f, SZ * 0.5f, /*bOpenings*/ false))
			{
				continue;
			}
			FVector Location;
			FRotator Rotation;
			AimAt(EWall::North, U, Z, 0.f, Location, Rotation);
			Build.Stain(RoomSurfaces::Damp, Location, Rotation, FVector2D(SU, SZ), MouldTint, Opacity, 1.1f);
		}
	}
}

void AMusicRoomActor::SpawnWindows()
{
	// Three followers of the bedroom's storm in the north wall, over the kitchen's windows. The
	// kitchen's west window has a view of its own out there (sky, ground, trees), right in front of
	// these, and from up here its ground three and a half metres down is right for an upper floor. No
	// own view and no own trees: a second sky, ground or treeline on one stretch of the house stands
	// two worlds through each other (the 10-02 note).
	for (int32 i = 0; i < 3; ++i)
	{
		const FTransform Transform(FRotator(0.f, -90.f, 0.f),
			GetActorTransform().TransformPosition(FVector(WindowXs[i], NorthY() - Setup.WallThickness * 0.5f, 0.f)));
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
		// Three tall windows are a lot of sky, and this room is meant to be mostly dark (the library's).
		WindowSetup.PortalScale = 0.38f;
		// No through-the-walls glow: the kitchen is below and the hall beside, and both have windows of
		// their own. The strike comes in through the glass, on the sky portals.
		WindowSetup.GlowScale = 0.f;
		// Emerald velvet, heavy, to the floor, tied back on dark bronze cords; the middle window's
		// left-hand cord has given way and that drape hangs loose, moving in what comes in round the
		// glass. The pole stands further out for the folds, and the sill is a narrow board they fall past.
		WindowSetup.CurtainSurface = &MusicVelvet;
		WindowSetup.CurtainTint = MusicVelvetTint;
		WindowSetup.DrapeWidth = 84.f;
		WindowSetup.DrapeFold = 1.8f;
		WindowSetup.DrapeTie = 0.62f;
		WindowSetup.DrapeTieGone = (i == 1) ? 0 : -1;
		WindowSetup.bDrapeToFloor = true;
		WindowSetup.PoleProud = 24.f;
		WindowSetup.InnerSillProud = 10.f;
		WindowSetup.CurtainTrim = F(TEXT("Cord"));
		WindowSetup.Seed = 19750621 + i * 101;
		Window->Configure(WindowSetup);
		Window->SetLead(LeadStorm);
		Window->FinishSpawning(Transform);
		Windows.Add(Window);
		// The tiebacks' hooks beside the window are inside the reach of the damp round the frames: a
		// hand's breadth round each is kept clear (the library's).
		const float HookReach = 6.f;
		for (const FVector& Hook : Window->GetTieHooks())
		{
			const FVector Local = GetActorTransform().InverseTransformPosition(Hook);
			WallKeepOuts.Add({ EWall::North, FVector2D(Local.X - HookReach, Local.X + HookReach), FVector2D(Local.Z - HookReach, Local.Z + HookReach) });
		}
	}
}

AClueActor* AMusicRoomActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
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

void AMusicRoomActor::BuildClues()
{
	// Where this room's clues will go. Set dressing only, as everywhere in the house until the game is
	// built end to end: no text, no prompt (the 09-30 decision).

	// The music on the cello's stand: a page of manuscript and the page after it, which stops partway
	// down, a note crossed out and nothing written after it.
	{
		FVector Desk, Normal, Up;
		DeskFrame(WoodStandSeat(), WoodStandYaw(), WoodDeskZ, Desk, Normal, Up);
		const FVector Right = FVector::CrossProduct(Normal, Up).GetSafeNormal();
		const float H = 29.7f;
		const FVector Foot = Desk + Up * (-DeskH * 0.5f - 0.35f + H * 0.5f) + Normal * 0.5f;
		if (AClueActor* Music = SpawnClue(Foot, FRotator::ZeroRotator))
		{
			// The clue sits between the two sheets, turned by nothing, so the room's vectors are its own.
			// The reader's left is -Right: the first page there, the unfinished one on the right.
			FRoomBuilder B(Music, Music->GetRootScene());
			Paper(B, TEXT("manuscript_a"), -Right * 10.8f, Normal, Up, 21.f, H);
			Paper(B, TEXT("manuscript_b"), Right * 10.8f + Normal * 0.05f, Normal, Up, 21.f, H);
		}
	}

	// The concert programme on the table between the armchairs, and the order of the evening under it.
	{
		const FVector Table = SideTableSeat() + FVector(10.f, -6.f, SideTableTop + 0.15f);
		if (AClueActor* Programme = SpawnClue(Table, FRotator::ZeroRotator))
		{
			FRoomBuilder B(Programme, Programme->GetRootScene());
			Paper(B, TEXT("programme_in"), FVector(2.f, 3.f, 0.f), FVector::UpVector, FRotator(0.f, 70.f, 0.f).RotateVector(FVector(1.f, 0.f, 0.f)), 15.f, 21.2f);
			Paper(B, TEXT("programme"), FVector(-1.f, -1.f, 0.12f), FVector::UpVector, FRotator(0.f, 52.f, 0.f).RotateVector(FVector(1.f, 0.f, 0.f)), 15.f, 21.2f);
		}
	}

	// The photographs on the sideboard: the woman at her cello in this room, the girl with her first
	// violin and her mother at her shoulder, the two of them taking a bow.
	const FTransform Board(FRotator(0.f, FacingYaw(EWall::East), 0.f), SideboardSeat());
	if (AClueActor* Photos = SpawnClue(Board.TransformPosition(FVector(-16.f, -12.f, SideboardTop)), FRotator(0.f, FacingYaw(EWall::East), 0.f)))
	{
		FRoomBuilder B(Photos, Photos->GetRootScene());
		const TPair<FVector, float> Seats[3] = { { FVector(6.f, -3.f, 0.f), -6.f }, { FVector(-18.f, -1.f, 0.f), 10.f }, { FVector(28.f, 2.f, 0.f), -14.f } };
		for (int32 i = 0; i < 3; ++i)
		{
			// Within the reach of the dust on the sideboard's top, which would smear down their faces.
			MusicNoDecals(Place(B, MusicProps::Photos[i], Seats[i].Key, FRotator(0.f, Seats[i].Value, 0.f),
				{ { TEXT("Gilt"), F(TEXT("Gilt")) }, { TEXT("Ebony"), F(TEXT("Ebony")) }, { TEXT("Paper"), MatPaperSheet } }));
		}
	}

	// Her music notebook, open on the sideboard in front of them: exercises in pencil, notes in ink.
	if (AClueActor* Notebook = SpawnClue(Board.TransformPosition(FVector(8.f, 9.f, SideboardTop + 0.4f)), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Notebook, Notebook->GetRootScene());
		// Read from the room, facing the wall: the top of the page is towards the wall, and the reader's
		// right is the sideboard's +X.
		const FVector ToWall = -WallNormal(EWall::East);
		const FVector Along = Board.TransformVectorNoScale(FVector(1.f, 0.f, 0.f));
		const FRotator Skew(0.f, 6.f, 0.f);
		B.Box(FVector(0.f, 0.f, -0.25f), FRotator(0.f, FacingYaw(EWall::East) + 6.f, 0.f), FVector(44.f, 30.5f, 0.5f), MatLeather, false);
		Paper(B, TEXT("notebook_l"), Skew.RotateVector(-Along * 10.6f) + FVector(0.f, 0.f, 0.05f), FVector::UpVector, Skew.RotateVector(ToWall), 20.6f, 29.f);
		Paper(B, TEXT("notebook_r"), Skew.RotateVector(Along * 10.6f) + FVector(0.f, 0.f, 0.05f), FVector::UpVector, Skew.RotateVector(ToWall), 20.6f, 29.f);
	}
}
