#include "StudyActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "StormWindowActor.h"
#include "DustMotesComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Algo/Find.h"

namespace
{
	/** The generated models (Tools/make_study.py), /Game/Meshes/<name>. */
	namespace StudyProps
	{
		const TCHAR* DoorCase = TEXT("study_door_case");
		const TCHAR* Desk = TEXT("study_desk");
		const TCHAR* Tumbler = TEXT("study_tumbler");
		const TCHAR* TumblerEmpty = TEXT("study_tumbler_empty");
		const TCHAR* Phone = TEXT("study_phone");
		const TCHAR* Humidor = TEXT("study_humidor");
		const TCHAR* Ashtray = TEXT("study_ashtray");
		const TCHAR* Folders = TEXT("study_folders");
		const TCHAR* Photos = TEXT("study_photos");
		const TCHAR* ClubChair = TEXT("study_club_chair");
		const TCHAR* Sofa = TEXT("study_sofa");
		const TCHAR* CoffeeTable = TEXT("study_coffee_table");
		const TCHAR* CardTable = TEXT("study_card_table");
		const TCHAR* Cards = TEXT("study_cards");
		const TCHAR* Tray = TEXT("study_tray");
		const TCHAR* Bar = TEXT("study_bar");
		const TCHAR* Safe = TEXT("study_safe");
		const TCHAR* Bookcase = TEXT("study_bookcase");
		const TCHAR* ArchiveBoxes = TEXT("study_archive_boxes");
		const TCHAR* Board = TEXT("study_board");
		const TCHAR* Portrait = TEXT("study_portrait");
		const TCHAR* Map = TEXT("study_map");
		const TCHAR* Sconce = TEXT("study_sconce");
		const TCHAR* CoatStand = TEXT("study_coat_stand");
		const TCHAR* Fedora = TEXT("study_fedora");
		// The house's own pieces, from the rooms that have them.
		const TCHAR* DeskChair = TEXT("library_desk_chair");
		const TCHAR* SideChair = TEXT("library_side_chair");
		const TCHAR* BankersLamp = TEXT("library_bankers_lamp");
		const TCHAR* FloorLamp = TEXT("library_floor_lamp");
		const TCHAR* Pen = TEXT("library_pen");
		const TCHAR* InkBottle = TEXT("library_ink_bottle");
		const TCHAR* Decanter = TEXT("wine_decanter");
		const TCHAR* Stopper = TEXT("wine_decanter_stopper");
		const TCHAR* Cornice = TEXT("library_cornice");
		const TCHAR* CeilingRose = TEXT("music_ceiling_rose");
		const TCHAR* CeilingBead = TEXT("music_ceiling_bead");
		const TCHAR* Dado = TEXT("bedroom_dado");
		const TCHAR* Skirting = TEXT("bedroom_skirting");
		const TCHAR* PanelMoulding = TEXT("bedroom_panel_moulding");
		const TCHAR* BookRows[10] = {
			TEXT("library_books_a"), TEXT("library_books_b"), TEXT("library_books_c"), TEXT("library_books_d"), TEXT("library_books_e"),
			TEXT("library_books_f"), TEXT("library_books_g"), TEXT("library_books_h"), TEXT("library_books_i"), TEXT("library_books_j"),
		};
		const TCHAR* BookPiles[2] = { TEXT("library_books_pile_a"), TEXT("library_books_pile_b") };
		/** The rows' exact lengths (Tools/make_library.py: ROW_LENGTHS). */
		constexpr float RowLengths[10] = { 46.f, 44.f, 47.f, 42.f, 45.f, 46.f, 43.f, 47.f, 30.f, 34.f };
	}

	// The photographed surfaces this room is made of (Poly Haven, CC0), at the texel sizes the
	// generated models' UVs are laid in (Tools/make_study.py: SLOTS).
	/** Flamed black veneer: measured linear (0.094, 0.082, 0.075); tinted to black lacquer. */
	const FRoomSurface StudyLacquer{ TEXT("flamed_black_veneer"), 100.f };
	const FLinearColor StudyLacquerTint(0.11f, 0.105f, 0.11f);
	/**
	 * A satin lacquer: the veneer's own roughness (0.70) taken down to about a half. Matt (0.9), a
	 * surface this dark is grey under the lantern, because its broad sheen outshines its colour; a full
	 * gloss (0.22) breaks up in Lumen's reflections into a glitter of noisy specks across the panelling
	 * and the doors. In between, and held darker, it stays black with a soft highlight.
	 */
	constexpr float StudyLacquerRoughness = 0.72f;
	/** Red leather: measured (0.084, 0.029, 0.014), a red-brown; tinted to burgundy. */
	const FRoomSurface StudyLeather{ TEXT("leather_red_02"), 40.f };
	const FLinearColor StudyLeatherTint(0.40f, 0.10f, 0.30f);
	/** Scratched metal plate: measured (0.091, 0.068, 0.049); tinted to old gold, and to black steel. */
	const FRoomSurface StudyMetal{ TEXT("metal_plate_02"), 100.f };
	const FLinearColor StudyGiltTint(2.2f, 2.0f, 1.1f);
	const FLinearColor StudySteelTint(0.24f, 0.30f, 0.40f);
	/** Velour: measured (0.42, 0.027, 0.064), a bright red; tinted down to a burgundy. */
	const FRoomSurface StudyVelvet{ TEXT("velour_velvet"), 60.f };
	const FLinearColor StudyVelvetTint(0.15f, 0.24f, 0.17f);
	/** Painted plaster for the ceiling: measured 0.4; held dark, with a warm cast. */
	const FRoomSurface StudyPlaster{ TEXT("painted_plaster_wall"), 200.f };
	const FLinearColor StudyCeilingTint(0.11f, 0.09f, 0.085f);
	/** The music room's damask (Tools/make_music_art.py), one repeat every 64cm, measured (0.080, 0.052, 0.018): tinted burgundy. */
	constexpr float StudyDamaskRepeat = 64.f;
	const FLinearColor StudyDamaskTint(0.66f, 0.15f, 0.55f);

	/** The house's dust, the colour it is everywhere. */
	const FLinearColor StudyDust(0.42f, 0.40f, 0.36f);

	/** For a model's own UVs, already in repeats of the slot's texel size: tiling at one. */
	void StudySheet(UMaterialInstanceDynamic* Mat)
	{
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}

	// A decal reaches nine centimetres either way along its aim (FRoomBuilder::Stain). The floor's are
	// aimed down from four over the boards (whose top is at zero), so they reach up to thirteen.
	constexpr float StudyDecalReach = 9.f;
	constexpr float StudyFloorDecalZ = 4.f;
	/** The sizes a patch of the floor's film is tried at, largest first, to lay it between the furniture. */
	constexpr float StudyFilmScales[] = { 1.f, 0.7f, 0.5f };

	/** A floor decal's half extent on the plan: aimed down, its first size runs along world Y and its second along X before the roll (09-27). */
	FVector2D StudyFloorDecalHalfExtent(const FVector2D& Size, float Roll)
	{
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		return FVector2D((Sin * Size.X + Cos * Size.Y) * 0.5f, (Cos * Size.X + Sin * Size.Y) * 0.5f);
	}

	/** A wall decal's rolled half extent, (along, up). */
	FVector2D StudyWallHalfExtent(float SU, float SZ, float Roll)
	{
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		return FVector2D((Cos * SU + Sin * SZ) * 0.5f, (Sin * SU + Cos * SZ) * 0.5f);
	}

	/**
	 * A sheet laid over pieces of a wall, its UVs in centimetres from the wall's own origin, so the
	 * damask runs on across every cut as if the wall were hung in one length (the music room's).
	 * Both windings, the normal forced (the 09-21 pane lesson).
	 */
	UProceduralMeshComponent* StudyFacade(AActor* Owner, USceneComponent* Parent, const TArray<FBox2D>& Pieces, const FVector& Origin,
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
		// UE builds TangentY as TangentZ ^ TangentX: with V running down the wall that points up on
		// some walls and down on others, which turns a normal map upside down (the bathroom's 11-11
		// note). Flipped where (Face ^ Along) points up.
		const bool bFlipY = FVector::CrossProduct(Face, Along).Z > 0.f;
		for (const FBox2D& Piece : Pieces)
		{
			const int32 First = Verts.Num();
			const FVector2D Corners[4] = { Piece.Min, FVector2D(Piece.Max.X, Piece.Min.Y), Piece.Max, FVector2D(Piece.Min.X, Piece.Max.Y) };
			for (const FVector2D& C : Corners)
			{
				Verts.Add(Origin + Along * C.X + FVector(0.f, 0.f, C.Y));
				Normals.Add(Face);
				UVs.Add(FVector2D(C.X / TexelCm, -C.Y / TexelCm));
				Tangents.Add(FProcMeshTangent(Along, bFlipY));
			}
			Tris.Append({ First, First + 1, First + 2, First, First + 2, First + 1, First, First + 2, First + 3, First, First + 3, First + 2 });
		}
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("Damask")));
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

TArray<TPair<FName, UMaterialInterface*>> AStudyActor::DoorLeafSlots(UObject* Outer)
{
	// Straight from the assets, the model's UVs already in repeats of the slots' texel sizes.
	auto Sheet = [Outer](const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale) -> UMaterialInterface*
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), Set.Set, Set.Set));
		UMaterialInstanceDynamic* Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, Outer) : nullptr;
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("Tint"), Tint);
			Mat->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
			StudySheet(Mat);
		}
		return Mat;
	};
	UMaterialInterface* Shadow = nullptr;
	if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), StudyLacquer.Set, StudyLacquer.Set)))
	{
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Parent, Outer);
		Mat->SetVectorParameterValue(TEXT("Tint"), StudyLacquerTint * 0.25f);
		Mat->SetScalarParameterValue(TEXT("RoughnessScale"), 2.f);
		StudySheet(Mat);
		Shadow = Mat;
	}
	return {
		{ FName(TEXT("Lacquer")), Sheet(StudyLacquer, StudyLacquerTint, StudyLacquerRoughness) },
		{ FName(TEXT("Gilt")), Sheet(StudyMetal, StudyGiltTint, 1.f) },
		{ FName(TEXT("Shadow")), Shadow },
	};
}

const TCHAR* AStudyActor::DoorLeafModel()
{
	return TEXT("study_door");
}

AStudyActor::AStudyActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);
	RoomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void AStudyActor::Configure(const FStudySetup& InSetup, AStormWindowActor* InLeadStorm)
{
	Setup = InSetup;
	LeadStorm = InLeadStorm;

	// Here and not in BeginPlay: the motes are scattered in the component's own BeginPlay, which runs
	// before the owner's (see ARoomDressingActor::Configure).
	DustMotes->ConfigureVolume(
		FVector((EastX() - WestX()) * 0.47f, (SouthY() - NorthY()) * 0.47f, RoomHeight * 0.46f),
		FVector(MidX(), MidY(), RoomHeight * 0.5f));

	Openings = { { EWall::South, Setup.DoorX, DoorOpeningWidth * 0.5f, 0.f, DoorOpeningHeight } };
	for (const float X : WindowXs)
	{
		Openings.Add({ EWall::North, X, WindowHalf, WindowSill, WindowTop });
	}
}

void AStudyActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning this room never relays the corridor's.
	Random.Initialize(19531104);

	FRoomBuilder Build(this, RoomRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildFloor(Build);
	BuildWalls(Build);
	BuildWindows(Build);
	BuildDoorCase(Build);
	BuildCeiling(Build);
	BuildChandelier(Build);
	BuildDesk(Build);
	BuildSeating(Build);
	BuildWestWall(Build);
	BuildEastWall(Build);
	BuildSouthWall(Build);
	BuildCardTable(Build);
	BuildSconces(Build);
	// The floor's dust once the furniture stands, the windows and the clues before the damp on the
	// walls, since the tiebacks' hooks and the board are on them.
	BuildFloorDust(Build);
	SpawnWindows();
	BuildClues();
	BuildDamage(Build);
}

// ---------------------------------------------------------------------------------------------
// Materials. The house's rule holds: nothing much above a tenth, and warm. What this room adds is
// black, burgundy and gold, and every one of them is a photograph rather than a flat colour: black
// veneer under its lacquer, red leather, scratched metal for the gilt, velour for the drapes.
// ---------------------------------------------------------------------------------------------

void AStudyActor::CacheMaterials(FRoomBuilder& Build)
{
	// A sheet instance per surface for the generated models' own UVs, a hair off any tint the
	// builder's boxes use so the cache hands back a separate instance (the living room's rule).
	auto Sheet = [&](const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale = 1.f)
	{
		UMaterialInstanceDynamic* Mat = Build.Surface(Set, Tint + FLinearColor(0.0007f, 0.f, 0.f, 0.f), RoughnessScale);
		StudySheet(Mat);
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
			StudySheet(Mat);
		}
		return Mat;
	};

	MatLacquer = Sheet(StudyLacquer, StudyLacquerTint, StudyLacquerRoughness);
	MatLacquerBox = Build.Surface(StudyLacquer, StudyLacquerTint, StudyLacquerRoughness);
	// Rougher than the photograph: at its own 0.5 the cushions caught the windows as pale sheets.
	MatLeather = Sheet(StudyLeather, StudyLeatherTint, 1.45f);
	MatGilt = Sheet(StudyMetal, StudyGiltTint, 1.f);
	MatGiltBox = Build.Surface(StudyMetal, StudyGiltTint, 1.f);
	MatSteel = Sheet(StudyMetal, StudySteelTint, 1.2f);
	MatDamask = Atlas(TEXT("music_damask"), StudyDamaskTint, 1.3f);
	MatCeiling = Build.Surface(StudyPlaster, StudyCeilingTint);
	MatPlasterSheet = Sheet(StudyPlaster, StudyCeilingTint);
	// The board, the portrait, the map and the folders: paper at a quarter of the bake or it burns out
	// under the lantern; the carpet a little over half its own.
	MatPictures = Atlas(TEXT("study_pictures"), FLinearColor(0.32f, 0.30f, 0.28f), 1.f);
	MatRug = Atlas(TEXT("study_rug"), FLinearColor(0.95f, 0.85f, 0.85f), 1.f);
	MatBooks = Atlas(TEXT("library_books"), FLinearColor(0.26f, 0.24f, 0.22f), 1.f);

	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.06f, 0.035f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);
	// Cut crystal under dust, opaque (the music room's 10-21 note).
	MatCrystal = Build.Flat(FLinearColor(0.15f, 0.155f, 0.16f), 0.3f);

	struct FFlat { const TCHAR* Name; FLinearColor Color; float Roughness; float Metallic; };
	const FFlat FlatList[] = {
		{ TEXT("Shadow"), FLinearColor(0.002f, 0.002f, 0.002f), 1.f, 0.f },
		{ TEXT("Card"), FLinearColor(0.20f, 0.19f, 0.17f), 0.6f, 0.f },
		{ TEXT("CardBack"), FLinearColor(0.06f, 0.006f, 0.01f), 0.6f, 0.f },
		{ TEXT("ChipRed"), FLinearColor(0.10f, 0.012f, 0.012f), 0.45f, 0.f },
		{ TEXT("ChipBlack"), FLinearColor(0.008f, 0.008f, 0.008f), 0.45f, 0.f },
		{ TEXT("ChipIvory"), FLinearColor(0.24f, 0.22f, 0.18f), 0.45f, 0.f },
		{ TEXT("Cigar"), FLinearColor(0.045f, 0.022f, 0.01f), 0.85f, 0.f },
		{ TEXT("Ash"), FLinearColor(0.12f, 0.118f, 0.112f), 1.f, 0.f },
		{ TEXT("Buff"), FLinearColor(0.15f, 0.11f, 0.065f), 0.9f, 0.f },
		{ TEXT("Paper"), FLinearColor(0.22f, 0.20f, 0.17f), 0.9f, 0.f },
		{ TEXT("Cardboard"), FLinearColor(0.10f, 0.07f, 0.04f), 0.95f, 0.f },
		// A filament lamp's glass gone grey with dust.
		{ TEXT("Bulb"), FLinearColor(0.20f, 0.19f, 0.17f), 0.3f, 0.f },
		// Whisky left to dry for years: a dark varnish in the bottom of the glass.
		{ TEXT("Liquor"), FLinearColor(0.07f, 0.03f, 0.008f), 0.15f, 0.f },
		{ TEXT("Ribbon"), FLinearColor(0.045f, 0.006f, 0.008f), 0.8f, 0.f },
		// The banker's lamp's shade in burgundy glass instead of the library's green, and its lining.
		{ TEXT("LampShade"), FLinearColor(0.045f, 0.003f, 0.006f), 0.25f, 0.f },
		{ TEXT("LampInside"), FLinearColor(0.20f, 0.18f, 0.15f), 0.4f, 0.f },
		{ TEXT("Ink"), FLinearColor(0.004f, 0.004f, 0.006f), 0.2f, 0.f },
	};
	for (const FFlat& Flat : FlatList)
	{
		Flats.Add(FName(Flat.Name), Build.Flat(Flat.Color, Flat.Roughness, Flat.Metallic));
	}
	// Cloth that is a photograph too: the card table's baize, the lamp shades and the hat's felt in the
	// velour, at three values.
	Flats.Add(TEXT("Felt"), Sheet(StudyVelvet, StudyVelvetTint * 0.8f, 1.2f));
	Flats.Add(TEXT("Shade"), Sheet(StudyVelvet, StudyVelvetTint * 0.9f, 1.1f));
	Flats.Add(TEXT("Felt2"), Sheet(StudyVelvet, FLinearColor(0.03f, 0.35f, 0.25f), 1.2f));

	// Every slot name the study's models use, dressed once here (PlaceStudy).
	SlotMats.Add(TEXT("Lacquer"), MatLacquer);
	SlotMats.Add(TEXT("Leather"), MatLeather);
	SlotMats.Add(TEXT("Gilt"), MatGilt);
	SlotMats.Add(TEXT("Steel"), MatSteel);
	SlotMats.Add(TEXT("Glass"), MatGlass);
	SlotMats.Add(TEXT("Pictures"), MatPictures);
	for (const TCHAR* Name : { TEXT("Shadow"), TEXT("Felt"), TEXT("Liquor"), TEXT("Card"), TEXT("CardBack"), TEXT("ChipRed"), TEXT("ChipBlack"),
		TEXT("ChipIvory"), TEXT("Cigar"), TEXT("Ash"), TEXT("Buff"), TEXT("Paper"), TEXT("Cardboard"), TEXT("Shade"), TEXT("Bulb"), TEXT("Felt2"), TEXT("Ribbon") })
	{
		SlotMats.Add(FName(Name), F(Name));
	}
}

UMaterialInterface* AStudyActor::F(const TCHAR* Name) const
{
	const TObjectPtr<UMaterialInstanceDynamic>* Found = Flats.Find(FName(Name));
	return Found ? Found->Get() : MatShell.Get();
}

// ---------------------------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------------------------

void AStudyActor::Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
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

UStaticMeshComponent* AStudyActor::Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
	std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	Dress(Mesh, Slots);
	return Mesh;
}

UStaticMeshComponent* AStudyActor::PlaceStudy(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	if (Mesh)
	{
		for (const FName& Slot : Mesh->GetMaterialSlotNames())
		{
			if (const TObjectPtr<UMaterialInterface>* Mat = SlotMats.Find(Slot))
			{
				Mesh->SetMaterialByName(Slot, Mat->Get());
			}
		}
	}
	return Mesh;
}

void AStudyActor::Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw)
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

void AStudyActor::Dust(FRoomBuilder& Build, const FVector& Centre, const FVector2D& Size, float Opacity) const
{
	// From four centimetres over the top, aimed down: a decal reaches nine either way (09-29). On dark
	// lacquer and leather the dust goes on the smooth paint photograph through a soft edge: the damp
	// photograph's blotches read as camouflage on a dark surface (the 11-06 rule).
	Build.Stain(RoomSurfaces::Paint, Centre + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, 0.f), Size, StudyDust, Opacity, 0.6f);
}

float AStudyActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector AStudyActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector AStudyActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

float AStudyActor::FacingYaw(EWall Wall)
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

TArray<FBox2D> AStudyActor::Holes(EWall Wall) const
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
			// The door case stands out to its cornice's reach either side and up to its top: the walls'
			// finishes stop at it.
			Out.Add(FBox2D(FVector2D(Opening.CenterU - Opening.HalfU - DoorCaseReach, 0.f), FVector2D(Opening.CenterU + Opening.HalfU + DoorCaseReach, DoorCaseTop)));
		}
		else
		{
			Out.Add(FBox2D(FVector2D(Opening.CenterU - Opening.HalfU - WindowCasing, Opening.BottomZ - 4.f),
				FVector2D(Opening.CenterU + Opening.HalfU + WindowCasing, Opening.TopZ + WindowCasing)));
		}
	}
	return Out;
}

void AStudyActor::DamaskSheet(EWall Wall, const TArray<FBox2D>& Pieces)
{
	const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
	const FVector Origin = bAlongX ? FVector(0.f, WallFace(Wall), 0.f) : FVector(WallFace(Wall), 0.f, 0.f);
	StudyFacade(this, RoomRoot, Pieces, Origin + WallNormal(Wall) * 0.4f, bAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f),
		WallNormal(Wall), StudyDamaskRepeat, MatDamask);
}

void AStudyActor::PanelPiece(FRoomBuilder& Build, EWall Wall, const FBox2D& Piece, float Proud)
{
	const float W = Piece.GetSize().X;
	const float H = Piece.GetSize().Y;
	if (W < 0.5f || H < 0.5f)
	{
		return;
	}
	// Each piece on its own instance, the repeats written out for its size: through the builder's
	// tiling a piece narrower than it is tall stretches the veneer into stripes (the 10-09 rule).
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr,
		*FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), StudyLacquer.Set, StudyLacquer.Set));
	UMaterialInstanceDynamic* Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
	if (Mat)
	{
		const float Texel = StudyLacquer.TexelSizeCm;
		const int32 Index = PanelMats.Num();
		Mat->SetVectorParameterValue(TEXT("Tint"), StudyLacquerTint);
		Mat->SetScalarParameterValue(TEXT("RoughnessScale"), StudyLacquerRoughness);
		Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(W / Texel, H / Texel, 0.f, 1.f));
		Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(FMath::Frac(Index * 0.317f), FMath::Frac(Index * 0.533f), 0.f, 1.f));
		PanelMats.Add(Mat);
	}
	Build.Box(WallPoint(Wall, Piece.GetCenter().X, Piece.GetCenter().Y, Proud), FRotator(0.f, FacingYaw(Wall), 0.f), FVector(W, 0.8f, H),
		Mat ? Mat : MatLacquerBox.Get(), false);
}

void AStudyActor::Moulding(FRoomBuilder& Build, const TCHAR* Name, EWall Wall, float U0, float U1, float Z, float Proud, UMaterialInterface* Mat)
{
	// The bedroom's mouldings are a metre along X with their profile in YZ out of the wall from its
	// foot; laid end to end and scaled to fit. Along north and east walls U runs the way the model's X
	// does after the facing yaw; along south and west it runs the other way, which a symmetric run does
	// not mind.
	const float Length = U1 - U0;
	if (Length < 1.f)
	{
		return;
	}
	const int32 Pieces = FMath::Max(1, FMath::CeilToInt(Length / 100.f));
	const float Piece = Length / Pieces;
	for (int32 i = 0; i < Pieces; ++i)
	{
		if (UStaticMeshComponent* Mesh = Place(Build, Name, WallPoint(Wall, U0 + Piece * (i + 0.5f), Z, Proud), FRotator(0.f, FacingYaw(Wall), 0.f),
			{ { TEXT("Paint"), Mat } }))
		{
			Mesh->SetRelativeScale3D(FVector(Piece / 100.f, 1.f, 1.f));
			Mesh->SetReceivesDecals(false);
		}
	}
}

void AStudyActor::AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const
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

bool AStudyActor::WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfU, float HalfZ, bool bOpenings) const
{
	// A wall decal projects onto everything within reach of the wall (the wine cellar's 10-08 note):
	// the door case and the window architraves stand out of it, so a decal near them goes elsewhere.
	const float Margin = 15.f;
	if (bOpenings)
	{
		for (const FBox2D& Hole : Holes(Wall))
		{
			if (U + HalfU > Hole.Min.X - Margin && U - HalfU < Hole.Max.X + Margin
				&& Z + HalfZ > Hole.Min.Y - Margin && Z - HalfZ < Hole.Max.Y + Margin)
			{
				return true;
			}
		}
	}
	// Against a piece of furniture, a frame or a sconce a few centimetres are enough.
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

FBox AStudyActor::RoomBox(const UPrimitiveComponent* Part) const
{
	return Part->Bounds.GetBox().TransformBy(GetActorTransform().Inverse());
}

void AStudyActor::KeepWallDecalsOff(const UPrimitiveComponent* Part)
{
	if (!Part || !Part->IsRegistered())
	{
		return;
	}
	// A wall decal is put two centimetres off the wall and reaches nine either way from there, so
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

void AStudyActor::KeepFloorDustOff(const UPrimitiveComponent* Part)
{
	if (!Part || !Part->IsRegistered())
	{
		return;
	}
	const FBox Box = RoomBox(Part);
	if (Box.Min.Z < StudyFloorDecalZ + StudyDecalReach)
	{
		FloorKeepOuts.Add(FBox2D(FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Max.Y)));
	}
}

bool AStudyActor::FloorDustHitsSomething(const FVector2D& Centre, const FVector2D& HalfExtent) const
{
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
// The shell.
// ---------------------------------------------------------------------------------------------

void AStudyActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float Z0 = -20.f;
	const float Z1 = RoomHeight + 10.f;
	auto Wall = [&](const FVector& Min, const FVector& Max)
	{
		Build.Box((Min + Max) * 0.5f, FRotator::ZeroRotator, Max - Min, MatShell);
	};
	// Solid, colliding and never seen. The west wall is the music room's and the stair hall's, both
	// taller than this room. The south wall up to the corridor's ceiling is the corridor's (it cuts the
	// door), and the east wall up to the bedroom's is the bedroom's: only what is over those is this
	// room's. North of the bedroom the east wall is the house's outside wall, from the floor.
	Wall(FVector(WestX() - T, SouthY(), Setup.CorridorHeight - 5.f), FVector(EastX() + T, SouthY() + T, Z1));
	Wall(FVector(EastX(), Setup.BedroomNorthY, Setup.CorridorHeight - 5.f), FVector(EastX() + T, SouthY() + T, Z1));
	Wall(FVector(EastX(), NorthY() - T, Z0), FVector(EastX() + T, Setup.BedroomNorthY, Z1));
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

void AStudyActor::BuildFloor(FRoomBuilder& Build)
{
	// Herringbone parquet, the living room's, laid darker: one box with an instance of its own, made
	// straight from the asset with its repeats written out for each side, since the room is deeper than
	// it is wide and the builder's tiling would squeeze one way (the 10-24 rule). The photograph is shot
	// freshly lacquered, so its roughness is taken up or the floor mirrors the windows (09-27).
	const float SizeX = EastX() - WestX();
	const float SizeY = SouthY() - NorthY();
	const FLinearColor Tint(0.17f, 0.18f, 0.22f);
	const float RoughnessScale = 5.5f;
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr,
		*FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), RoomSurfaces::Parquet.Set, RoomSurfaces::Parquet.Set));
	MatFloor = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
	if (MatFloor)
	{
		const float Texel = RoomSurfaces::Parquet.TexelSizeCm;
		MatFloor->SetVectorParameterValue(TEXT("Tint"), Tint);
		MatFloor->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
		MatFloor->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(SizeX / Texel, SizeY / Texel, 0.f, 1.f));
		MatFloor->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	else
	{
		MatFloor = Build.Surface(RoomSurfaces::Parquet, Tint, RoughnessScale);
	}
	Build.Box(FVector(MidX(), MidY(), -2.f), FRotator::ZeroRotator, FVector(SizeX, SizeY, 4.f), MatFloor);

	// The carpet under the desk and the club chairs: the baked rug laid on the floor as one sheet,
	// the top of its image (the desk chair's wear) to the north. A dark box under it for its edge.
	const FVector Rug = RugSeat();
	Build.Box(Rug + FVector(0.f, 0.f, 0.45f), FRotator::ZeroRotator, FVector(RugW, RugD, 0.9f), F(TEXT("Shadow")), false);
	if (UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Rug + FVector(0.f, 0.f, 0.95f), FRotator::ZeroRotator, FVector(RugW, RugD, 1.f), MatRug, false))
	{
		Sheet->SetCastShadow(false);
	}
}

void AStudyActor::BuildWalls(FRoomBuilder& Build)
{
	struct FRun { EWall Wall; float U0; float U1; };
	const FRun Walls[] = {
		{ EWall::North, WestX(), EastX() },
		{ EWall::South, WestX(), EastX() },
		{ EWall::East, NorthY(), SouthY() },
		{ EWall::West, NorthY(), SouthY() },
	};
	const float CorniceDrop = 28.f;
	const float DamaskTop = RoomHeight - CorniceDrop + 4.f;
	for (const FRun& R : Walls)
	{
		const TArray<FBox2D> Cut = Holes(R.Wall);
		// Black lacquered panelling to the dado, cut round the openings, and on it a gilt bolection
		// round each field: a field every ninety-odd centimetres along each stretch of wall.
		for (const FBox2D& Piece : RoomWalls::CutAround(Cut, R.U0, R.U1, 0.f, DadoTop))
		{
			PanelPiece(Build, R.Wall, Piece, 0.4f);
			const float Span = Piece.GetSize().X;
			if (Piece.Max.Y < DadoTop - 1.f || Span < 40.f)
			{
				continue;
			}
			const int32 Fields = FMath::Max(1, FMath::RoundToInt(Span / 92.f));
			const float FieldW = Span / Fields;
			for (int32 k = 0; k < Fields; ++k)
			{
				const float FU0 = Piece.Min.X + FieldW * k + 9.f;
				const float FU1 = Piece.Min.X + FieldW * (k + 1) - 9.f;
				const float FZ0 = 30.f;
				const float FZ1 = DadoTop - 14.f;
				Moulding(Build, StudyProps::PanelMoulding, R.Wall, FU0, FU1, FZ0, 0.8f, MatGilt);
				Moulding(Build, StudyProps::PanelMoulding, R.Wall, FU0, FU1, FZ1 - 3.6f, 0.8f, MatGilt);
				for (const float U : { FU0 + 1.8f, FU1 - 1.8f })
				{
					// Up the field's sides: pitch turns a run in the wall's plane (the 09-21 rule).
					const float Height = FZ1 - FZ0;
					if (UStaticMeshComponent* Mesh = Place(Build, StudyProps::PanelMoulding, WallPoint(R.Wall, U, FZ0 + Height * 0.5f, 0.8f),
						FRotator(90.f, FacingYaw(R.Wall), 0.f), { { TEXT("Paint"), MatGilt } }))
					{
						Mesh->SetRelativeScale3D(FVector(Height / 100.f, 1.f, 1.f));
						Mesh->SetReceivesDecals(false);
					}
				}
			}
		}
		// The skirting in black at its foot and the gilt dado rail over it, both cut round the openings.
		for (const FBox2D& Piece : RoomWalls::CutAround(Cut, R.U0, R.U1, 0.f, 22.f))
		{
			Moulding(Build, StudyProps::Skirting, R.Wall, Piece.Min.X, Piece.Max.X, 0.f, 0.4f, MatLacquer);
		}
		for (const FBox2D& Piece : RoomWalls::CutAround(Cut, R.U0, R.U1, DadoTop - 1.f, DadoTop + 7.f))
		{
			Moulding(Build, StudyProps::Dado, R.Wall, Piece.Min.X, Piece.Max.X, DadoTop - 1.f, 0.4f, MatGilt);
		}
		// The damask over the rail, up behind the cornice, and a gilt line under the cornice.
		DamaskSheet(R.Wall, RoomWalls::CutAround(Cut, R.U0, R.U1, DadoTop + 5.f, DamaskTop));
		for (const FBox2D& Piece : RoomWalls::CutAround(Cut, R.U0, R.U1, RoomHeight - CorniceDrop - 6.f, RoomHeight - CorniceDrop - 2.f))
		{
			Moulding(Build, StudyProps::PanelMoulding, R.Wall, Piece.Min.X, Piece.Max.X, RoomHeight - CorniceDrop - 6.f, 0.4f, MatGilt);
		}
	}

	// The cornice where the walls meet the ceiling: the library's, in black, laid end to end and
	// scaled to fit; at a corner the runs go through each other.
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
			if (UStaticMeshComponent* Mesh = Place(Build, StudyProps::Cornice, Run.Start + Run.Dir * (Piece * (i + 0.5f)), FRotator(0.f, Run.Yaw, 0.f),
				{ { TEXT("Trim"), MatLacquer } }))
			{
				Mesh->SetRelativeScale3D(FVector(Piece / 100.f, 1.f, 1.f));
				Mesh->SetReceivesDecals(false);
			}
		}
	}
}

void AStudyActor::BuildWindows(FRoomBuilder& Build)
{
	// The two tall windows behind the desk: black architraves with a gilt bead along the sight edge,
	// the reveals lined in the lacquer, a gilt line round the outer edge.
	for (const float X : WindowXs)
	{
		const float Z0 = WindowSill - 4.f;
		const float Z1 = WindowTop + WindowCasing;
		for (const float Side : { -1.f, 1.f })
		{
			const float U = X + Side * (WindowHalf + WindowCasing * 0.5f);
			Build.Box(WallPoint(EWall::North, U, (Z0 + Z1) * 0.5f, 1.6f), FRotator::ZeroRotator, FVector(WindowCasing, 3.2f, Z1 - Z0), MatLacquerBox, false);
			Build.Box(FVector(X + Side * (WindowHalf - 0.6f), NorthY() - Setup.WallThickness * 0.5f, (WindowSill + WindowTop) * 0.5f), FRotator::ZeroRotator,
				FVector(1.2f, Setup.WallThickness - 6.f, WindowTop - WindowSill), MatLacquerBox, false);
			Build.Cyl(WallPoint(EWall::North, X + Side * (WindowHalf + 0.8f), (Z0 + WindowTop) * 0.5f, 3.4f), FRotator::ZeroRotator,
				FVector(1.1f, 1.1f, WindowTop - Z0), MatGiltBox, false);
		}
		Build.Box(WallPoint(EWall::North, X, WindowTop + WindowCasing * 0.5f, 1.6f), FRotator::ZeroRotator, FVector(WindowHalf * 2.f + WindowCasing * 2.f, 3.2f, WindowCasing), MatLacquerBox, false);
		Build.Box(FVector(X, NorthY() - Setup.WallThickness * 0.5f, WindowTop - 0.6f), FRotator::ZeroRotator,
			FVector(WindowHalf * 2.f, Setup.WallThickness - 6.f, 1.2f), MatLacquerBox, false);
		Build.Cyl(WallPoint(EWall::North, X, WindowTop + 0.8f, 3.4f), FRotator(90.f, 0.f, 0.f), FVector(1.1f, 1.1f, WindowHalf * 2.f + 1.6f), MatGiltBox, false);
	}
}

void AStudyActor::BuildDoorCase(FRoomBuilder& Build)
{
	// The case round the door is one model through the wall, both faces at once: its origin in the
	// wall's middle plane at the foot of the opening, +Y into the room.
	PlaceStudy(Build, StudyProps::DoorCase, FVector(Setup.DoorX, SouthY() + Setup.WallThickness * 0.5f, 0.f), FRotator(0.f, 180.f, 0.f));
}

void AStudyActor::BuildCeiling(FRoomBuilder& Build)
{
	const float C = RoomHeight;
	Build.Mark(FVector(MidX(), MidY(), C), FRotator(0.f, 0.f, 180.f), FVector2D(EastX() - WestX(), SouthY() - NorthY()), MatCeiling);

	// A gilt bead run round a panel inset from the cornice, a second inside it, and a square round the
	// rose: the music room's ceiling bead, a metre along X, laid end to end and scaled.
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
				if (UStaticMeshComponent* Mesh = Place(Build, StudyProps::CeilingBead, S.Start + S.Dir * (Piece * (i + 0.5f)), FRotator(0.f, S.Yaw, 0.f),
					{ { TEXT("Plaster"), MatGilt } }))
				{
					Mesh->SetRelativeScale3D(FVector(Piece / 100.f, 1.f, 1.f));
				}
			}
		}
	};
	const float Inset = 80.f;
	Bead(WestX() + Inset, EastX() - Inset, NorthY() + Inset, SouthY() - Inset);
	Bead(WestX() + Inset + 14.f, EastX() - Inset - 14.f, NorthY() + Inset + 14.f, SouthY() - Inset - 14.f);
	const FVector Hook = ChandelierHook();
	Bead(Hook.X - 130.f, Hook.X + 130.f, Hook.Y - 130.f, Hook.Y + 130.f);

	// Water through the ceiling over the windows and in a corner, a crack across the panel. On the
	// dark paint, the smooth photograph through a soft edge (the 11-06 rule).
	for (int32 i = 0; i < 6; ++i)
	{
		const float X = Random.FRandRange(WestX() + 80.f, EastX() - 80.f);
		const float Y = (i < 3) ? NorthY() + Random.FRandRange(40.f, 200.f) : Random.FRandRange(NorthY() + 80.f, SouthY() - 80.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float SizeV = Random.FRandRange(70.f, 150.f);
		const float SizeU = Random.FRandRange(90.f, 180.f);
		const float Opacity = Random.FRandRange(0.3f, 0.5f);
		Build.Stain(RoomSurfaces::Paint, FVector(X, Y, C - 2.f), FRotator(90.f, 0.f, Roll), FVector2D(SizeU, SizeV), FLinearColor(0.30f, 0.25f, 0.19f), Opacity, 0.7f);
	}
	Build.Crack(FVector(MidX() + 160.f, MidY() + 160.f, C - 2.f), FRotator(90.f, 0.f, 25.f), FVector2D(180.f, 110.f), 0.7f, 18.f);

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

void AStudyActor::BuildChandelier(FRoomBuilder& Build)
{
	// The chandelier from a gilt rose over the middle of the room: the hall's Chandelier_03 at half its
	// size, dead, its brass dulled and its crystal grey with dust.
	const FVector Hook = ChandelierHook();
	Place(Build, StudyProps::CeilingRose, Hook, FRotator(0.f, 11.f, 0.f), { { TEXT("Plaster"), MatGilt } });
	const float Chain = 60.f;
	const float RoseDepth = 9.f;
	for (int32 Link = 0; Link * 6.f < Chain; ++Link)
	{
		Build.Box(Hook - FVector(0.f, 0.f, RoseDepth + 3.f + Link * 6.f), FRotator(0.f, (Link % 2) * 90.f, 0.f), FVector(1.1f, 3.4f, 7.f), MatGiltBox, false);
	}
	const FVector Top = Hook - FVector(0.f, 0.f, RoseDepth + Chain);
	if (UStaticMeshComponent* Body = Build.Prop(RoomProps::Chandelier, Top, FRotator(0.f, 22.f, 0.f), 112.f, false))
	{
		FRoomShapes::TintSlots(Body, FLinearColor(0.22f, 0.18f, 0.12f), 0);
		FRoomShapes::TintSlots(Body, FLinearColor(0.50f, 0.50f, 0.52f), 1);
	}
	for (int32 i = 0; i < 3; ++i)
	{
		const float A = 30.f + i * 120.f;
		Build.Add(FRoomShapes::Plane(), Top + FRotator(0.f, A, 0.f).RotateVector(FVector(26.f, 0.f, -60.f)), FRotator(0.f, A + 90.f, 90.f),
			FVector(24.f, 20.f, 1.f), MatWeb, false);
	}
}

// ---------------------------------------------------------------------------------------------
// The furniture.
// ---------------------------------------------------------------------------------------------

void AStudyActor::BuildDesk(FRoomBuilder& Build)
{
	const FVector Desk = DeskSeat();
	KeepFloorDustOff(PlaceStudy(Build, StudyProps::Desk, Desk, FRotator::ZeroRotator));
	Blocker(Build, Desk + FVector(0.f, 0.f, DeskTop * 0.5f), FVector(DeskW, DeskD, DeskTop));

	// His chair behind it, pushed back and turned, as when somebody gets up and goes: the library's tall
	// buttoned tub in the burgundy leather, its mahogany in the black lacquer.
	const FVector Chair = ChairSeat();
	KeepFloorDustOff(Place(Build, StudyProps::DeskChair, Chair, FRotator(0.f, ChairYaw, 0.f),
		{ { TEXT("Mahogany"), MatLacquer }, { TEXT("Leather"), MatLeather }, { TEXT("Iron"), MatSteel } }));
	Blocker(Build, Chair + FVector(0.f, 0.f, 60.5f), FVector(68.f, 67.f, 121.f), ChairYaw);

	// The top, as he sees it from the chair (his right hand is -X): the lamp at the back left, the
	// telephone at his left hand, the case files in front of him with two prints off the board beside
	// them (a clue, BuildClues), the ink and the pen at his right, the cigar box, the ashtray and the
	// glass he did not finish at the far right.
	const float Top = DeskTop + 0.15f;
	Place(Build, StudyProps::BankersLamp, Desk + FVector(62.f, -28.f, Top), FRotator(0.f, 168.f, 0.f),
		{ { TEXT("Brass"), MatGilt }, { TEXT("Shade"), F(TEXT("LampShade")) }, { TEXT("ShadeInside"), F(TEXT("LampInside")) } });
	PlaceStudy(Build, StudyProps::Phone, Desk + FVector(84.f, 12.f, Top), FRotator(0.f, 200.f, 0.f));
	Place(Build, StudyProps::InkBottle, Desk + FVector(-52.f, -22.f, Top), FRotator(0.f, 20.f, 0.f),
		{ { TEXT("Glass"), MatGlass }, { TEXT("Ink"), F(TEXT("Ink")) }, { TEXT("Ebony"), MatLacquer } });
	Place(Build, StudyProps::Pen, Desk + FVector(-44.f, -8.f, Top + 0.2f), FRotator(0.f, 58.f, 0.f), { { TEXT("Ebony"), MatLacquer }, { TEXT("Gilt"), MatGilt } });
	PlaceStudy(Build, StudyProps::Humidor, Desk + FVector(-86.f, -24.f, Top), FRotator(0.f, 184.f, 0.f));
	PlaceStudy(Build, StudyProps::Ashtray, Desk + FVector(-84.f, 14.f, Top), FRotator(0.f, 30.f, 0.f));
	PlaceStudy(Build, StudyProps::Tumbler, Desk + FVector(-64.f, 22.f, Top), FRotator::ZeroRotator);
	// Dust only along the back of the top and in its far corners, where nothing was moved: laid over the
	// whole leather, the pale film and the leather's creases came out as a slab of pink marble.
	Dust(Build, Desk + FVector(0.f, -DeskD * 0.5f + 12.f, DeskTop), FVector2D(18.f, DeskW - 20.f), 0.14f);
}

void AStudyActor::BuildSeating(FRoomBuilder& Build)
{
	// The two Chesterfield club chairs in front of the desk, facing it, turned a little to each other.
	for (const int32 Side : { -1, 1 })
	{
		const FVector Seat = ClubSeat(Side);
		KeepFloorDustOff(PlaceStudy(Build, StudyProps::ClubChair, Seat, FRotator(0.f, ClubYaw(Side), 0.f)));
		Blocker(Build, Seat + FVector(0.f, 0.f, ChesterTop * 0.5f), FVector(ClubW, ChesterD, ChesterTop), ClubYaw(Side));
		Dust(Build, Seat + FVector(0.f, 0.f, 46.f), FVector2D(50.f, 54.f), 0.12f);
	}
	// A web under the nearer one's seat, from its foot to the carpet.
	Build.Add(FRoomShapes::Plane(), ClubSeat(1) + FVector(30.f, 30.f, 8.f), FRotator(0.f, 20.f, 90.f), FVector(22.f, 12.f, 1.f), MatWeb, false);

	// The sofa on the east wall, the low table in front of it with a glass and the evening's paper.
	const FVector Sofa = SofaSeat();
	UStaticMeshComponent* SofaMesh = PlaceStudy(Build, StudyProps::Sofa, Sofa, FRotator(0.f, FacingYaw(EWall::East), 0.f));
	KeepFloorDustOff(SofaMesh);
	KeepWallDecalsOff(SofaMesh);
	Blocker(Build, Sofa + FVector(0.f, 0.f, ChesterTop * 0.5f), FVector(ChesterD, SofaW, ChesterTop));
	const FVector Table = CoffeeSeat();
	KeepFloorDustOff(PlaceStudy(Build, StudyProps::CoffeeTable, Table, FRotator(0.f, 90.f, 0.f)));
	Blocker(Build, Table + FVector(0.f, 0.f, CoffeeTop * 0.5f), FVector(CoffeeD, CoffeeW, CoffeeTop));
	PlaceStudy(Build, StudyProps::TumblerEmpty, Table + FVector(8.f, -30.f, CoffeeTop), FRotator::ZeroRotator);
	PlaceStudy(Build, StudyProps::Ashtray, Table + FVector(-10.f, 26.f, CoffeeTop), FRotator(0.f, 75.f, 0.f));
	Dust(Build, Table + FVector(0.f, 0.f, CoffeeTop), FVector2D(CoffeeW - 4.f, CoffeeD - 4.f), 0.2f);
	// The standard lamp at the sofa's end, its shade in the burgundy velour.
	const FVector Lamp(EastX() - 34.f, SofaY + SofaW * 0.5f + 32.f, 0.f);
	KeepFloorDustOff(Place(Build, StudyProps::FloorLamp, Lamp, FRotator(0.f, 30.f, 0.f), { { TEXT("Brass"), MatGilt }, { TEXT("Fabric"), F(TEXT("Shade")) } }));
}

void AStudyActor::BuildWestWall(FRoomBuilder& Build)
{
	// A black bookcase north of the board, filled from the library's rows of books.
	auto FillCase = [&](const FVector& Seat, float Yaw, int32 Seed)
	{
		UStaticMeshComponent* Case = PlaceStudy(Build, StudyProps::Bookcase, Seat, FRotator(0.f, Yaw, 0.f));
		KeepFloorDustOff(Case);
		KeepWallDecalsOff(Case);
		Blocker(Build, Seat + FVector(0.f, 0.f, CaseH * 0.5f), FVector(CaseW, CaseD, CaseH), Yaw);
		FRandomStream Shelf(Seed);
		TArray<UInstancedStaticMeshComponent*> Rows;
		for (const TCHAR* Name : StudyProps::BookRows)
		{
			Rows.Add(Build.Instances(FRoomShapes::Prop(Name), MatBooks));
		}
		const FTransform Bay(FRotator(0.f, Yaw, 0.f), Seat);
		const float X0 = -CaseInterior * 0.5f + 0.6f;
		const float Front = CaseD * 0.5f - 2.4f;
		// The counter and the three open shelves over it; the top one has books lying flat.
		for (int32 Level = 0; Level < 3; ++Level)
		{
			const float Z = CaseShelves[Level];
			float Cursor = X0 + Shelf.FRandRange(0.f, 1.5f);
			while (true)
			{
				const float Remaining = X0 + CaseInterior - 1.2f - Cursor;
				const int32 Pick = Shelf.RandRange(0, 9);
				const float Roll = Shelf.FRand();
				const float Gap = Shelf.FRandRange(6.f, 18.f);
				if (Roll < 0.18f)
				{
					Cursor += Gap;
					continue;
				}
				int32 Row = Pick;
				if (StudyProps::RowLengths[Row] > Remaining)
				{
					Row = INDEX_NONE;
					for (int32 k = 0; k < 10; ++k)
					{
						if (StudyProps::RowLengths[k] <= Remaining && (Row == INDEX_NONE || StudyProps::RowLengths[k] > StudyProps::RowLengths[Row]))
						{
							Row = k;
						}
					}
				}
				if (Row == INDEX_NONE || !Rows[Row])
				{
					break;
				}
				Rows[Row]->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Cursor, Front, Z)) * Bay);
				Cursor += StudyProps::RowLengths[Row] + Shelf.FRandRange(0.f, 0.5f);
			}
		}
		Place(Build, StudyProps::BookPiles[Seed % 2], Bay.TransformPosition(FVector(-22.f, 0.f, CaseShelves[3])), FRotator(0.f, Yaw + 8.f, 0.f), { { TEXT("Books"), MatBooks } });
		PlaceStudy(Build, StudyProps::Humidor, Bay.TransformPosition(FVector(30.f, 2.f, CaseShelves[3])), FRotator(0.f, Yaw - 6.f, 0.f));
		Dust(Build, Bay.TransformPosition(FVector(0.f, 0.f, CaseH)), FVector2D(CaseD, CaseW), 0.45f);
	};
	FillCase(WestCaseSeat(), FacingYaw(EWall::West), 7101);
	for (int32 i = 0; i < 2; ++i)
	{
		FillCase(EastCaseSeat(i), FacingYaw(EWall::East), 7102 + i);
	}

	// The case board on the west wall is a clue (BuildClues). Under it the files that would not fit
	// on it, in boxes.
	UStaticMeshComponent* Boxes = PlaceStudy(Build, StudyProps::ArchiveBoxes, FVector(WestX() + 25.f, BoardY, 0.f), FRotator(0.f, FacingYaw(EWall::West), 0.f));
	KeepFloorDustOff(Boxes);
	KeepWallDecalsOff(Boxes);
	Blocker(Build, FVector(WestX() + 25.f, BoardY, 28.f), FVector(46.f, 96.f, 56.f));
}

void AStudyActor::BuildEastWall(FRoomBuilder& Build)
{
	// The city map over the sofa, in its black frame.
	UStaticMeshComponent* Map = PlaceStudy(Build, StudyProps::Map, WallPoint(EWall::East, SofaY, MapZ, 0.5f), FRotator(0.f, FacingYaw(EWall::East), 0.f));
	KeepWallDecalsOff(Map);

	// The safe in the south-east corner, facing the room: the one thing in here the dust has not
	// got into, because it was never opened after.
	const FVector Safe = SafeSeat();
	UStaticMeshComponent* SafeMesh = PlaceStudy(Build, StudyProps::Safe, Safe, FRotator(0.f, FacingYaw(EWall::East), 0.f));
	KeepFloorDustOff(SafeMesh);
	KeepWallDecalsOff(SafeMesh);
	Blocker(Build, Safe + FVector(0.f, 0.f, SafeH * 0.5f), FVector(SafeD, SafeW, SafeH));
	Dust(Build, Safe + FVector(0.f, 0.f, SafeH), FVector2D(SafeW - 4.f, SafeD - 4.f), 0.45f);
}

void AStudyActor::BuildSouthWall(FRoomBuilder& Build)
{
	// The drinks cabinet west of the door, the decanters out on their tray and the tumblers by them.
	const FVector Bar = BarSeat();
	UStaticMeshComponent* BarMesh = PlaceStudy(Build, StudyProps::Bar, Bar, FRotator(0.f, FacingYaw(EWall::South), 0.f));
	KeepFloorDustOff(BarMesh);
	KeepWallDecalsOff(BarMesh);
	Blocker(Build, Bar + FVector(0.f, 0.f, BarTop * 0.5f), FVector(BarW, BarD, BarTop));
	const FTransform Top(FRotator(0.f, FacingYaw(EWall::South), 0.f), Bar + FVector(0.f, 0.f, BarTop));
	PlaceStudy(Build, StudyProps::Tray, Top.TransformPosition(FVector(-40.f, -2.f, 0.f)), FRotator(0.f, 180.f + 4.f, 0.f));
	// One decanter on the tray, stoppered (the stopper's collar sits on the lip, wine cellar
	// make_wine_cellar.py: 29.3 - 3.5); the other beside it, its stopper out and lying on the top.
	const FVector OnTray = Top.TransformPosition(FVector(-40.f, -2.f, TrayTop));
	Place(Build, StudyProps::Decanter, OnTray, FRotator::ZeroRotator, { { TEXT("Crystal"), MatGlass }, { TEXT("Residue"), F(TEXT("Liquor")) } });
	Place(Build, StudyProps::Stopper, OnTray + FVector(0.f, 0.f, 25.8f), FRotator::ZeroRotator, { { TEXT("Crystal"), MatGlass } });
	Place(Build, StudyProps::Decanter, Top.TransformPosition(FVector(8.f, 4.f, 0.f)), FRotator(0.f, 40.f, 0.f), { { TEXT("Crystal"), MatGlass }, { TEXT("Residue"), F(TEXT("Liquor")) } });
	if (UStaticMeshComponent* Loose = Build.PropSeated(StudyProps::Stopper, Top.TransformPosition(FVector(30.f, -10.f, 0.f)), FRotator(80.f, -30.f, 0.f), 0.f, false))
	{
		Dress(Loose, { { TEXT("Crystal"), MatGlass } });
	}
	for (const FVector& At : { FVector(46.f, 8.f, 0.f), FVector(58.f, -4.f, 0.f), FVector(70.f, 8.f, 0.f) })
	{
		PlaceStudy(Build, StudyProps::TumblerEmpty, Top.TransformPosition(At), FRotator::ZeroRotator);
	}
	Dust(Build, Bar + FVector(0.f, 0.f, BarTop), FVector2D(BarD - 6.f, BarW - 6.f), 0.42f);

	// The coat stand inside the door, his hat on its top hook.
	const FVector Stand = CoatStandSeat();
	KeepFloorDustOff(PlaceStudy(Build, StudyProps::CoatStand, Stand, FRotator(0.f, 15.f, 0.f)));
	Blocker(Build, Stand + FVector(0.f, 0.f, CoatStandH * 0.5f), FVector(40.f, 40.f, CoatStandH));
	PlaceStudy(Build, StudyProps::Fedora, Stand + FVector(2.f, 1.f, CoatStandH - 5.f), FRotator(-9.f, 140.f, 6.f));
}

void AStudyActor::BuildCardTable(FRoomBuilder& Build)
{
	// The card table in the south-west, the game left on it: a hand face up, another face down, the
	// chips, two glasses and the ashtray. Three chairs round it, one pushed back.
	const FVector Table = CardTableSeat();
	KeepFloorDustOff(PlaceStudy(Build, StudyProps::CardTable, Table, FRotator::ZeroRotator));
	Blocker(Build, Table + FVector(0.f, 0.f, CardTableTop * 0.5f), FVector(CardTableR * 1.6f, CardTableR * 1.6f, CardTableTop));
	PlaceStudy(Build, StudyProps::Cards, Table + FVector(0.f, 0.f, CardTableTop), FRotator(0.f, 12.f, 0.f));
	PlaceStudy(Build, StudyProps::Tumbler, Table + FVector(-34.f, 20.f, CardTableTop), FRotator::ZeroRotator);
	PlaceStudy(Build, StudyProps::TumblerEmpty, Table + FVector(30.f, 26.f, CardTableTop), FRotator::ZeroRotator);
	PlaceStudy(Build, StudyProps::Ashtray, Table + FVector(4.f, -32.f, CardTableTop), FRotator(0.f, -40.f, 0.f));
	struct FSeat { float Angle; float Reach; float Twist; };
	const FSeat Seats[] = { { 20.f, 70.f, 0.f }, { 140.f, 72.f, 0.f }, { 260.f, 92.f, 24.f } };
	for (const FSeat& S : Seats)
	{
		const FVector Dir = FRotator(0.f, S.Angle, 0.f).Vector();
		const FVector At = Table + Dir * S.Reach;
		// A yaw of A turns the chair's +Y to (-sin A, cos A); facing the table is -Dir.
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.X, -Dir.Y)) + S.Twist;
		KeepFloorDustOff(Place(Build, StudyProps::SideChair, At, FRotator(0.f, Yaw, 0.f), { { TEXT("Mahogany"), MatLacquer }, { TEXT("LeatherGreen"), MatLeather } }));
		Blocker(Build, At + FVector(0.f, 0.f, 45.f), FVector(44.f, 44.f, 90.f), Yaw);
	}
	Dust(Build, Table + FVector(0.f, 0.f, CardTableTop), FVector2D(CardTableR * 1.7f, CardTableR * 1.7f), 0.12f);
	Build.Add(FRoomShapes::Plane(), Table + FVector(14.f, 10.f, 30.f), FRotator(0.f, 70.f, 90.f), FVector(26.f, 20.f, 1.f), MatWeb, false);
}

void AStudyActor::BuildSconces(FRoomBuilder& Build)
{
	// Gilt sconces with burgundy shades, dead: a pair either side of the portrait, either side of the
	// map, either side of the board.
	struct FSconce { EWall Wall; float U; };
	const FSconce Sconces[] = {
		{ EWall::North, Setup.DoorX - 105.f }, { EWall::North, Setup.DoorX + 105.f },
		{ EWall::East, SofaY - 120.f }, { EWall::East, SofaY + 120.f },
		{ EWall::West, BoardY - 150.f }, { EWall::West, BoardY + 150.f },
	};
	for (const FSconce& S : Sconces)
	{
		KeepWallDecalsOff(PlaceStudy(Build, StudyProps::Sconce, WallPoint(S.Wall, S.U, 182.f, 0.2f), FRotator(0.f, FacingYaw(S.Wall), 0.f)));
	}
	// The portrait over his chair, between the windows: a man in a black hat.
	KeepWallDecalsOff(PlaceStudy(Build, StudyProps::Portrait, PortraitAt() + FVector(0.f, 0.3f, 0.f), FRotator::ZeroRotator));
}

// ---------------------------------------------------------------------------------------------
// Decay.
// ---------------------------------------------------------------------------------------------

void AStudyActor::BuildFloorDust(FRoomBuilder& Build)
{
	// Dust along the feet of the walls. Aimed down, the first size runs along world Y: roll 90 along
	// the north and south walls, none along the east and west (09-27). Kept off whatever stands on the
	// floor and off the door's sweep. A skipped strip has drawn all its numbers (09-29).
	const FVector2D Hinge(Setup.HingeX, SouthY());
	auto ReachesDoor = [&](const FVector2D& Centre, const FVector2D& Half)
	{
		const float Leaf = Setup.DoorLeafWidth + 10.f;
		const FBox2D Sweep(FVector2D(Hinge.X - Leaf, Hinge.Y - Leaf), FVector2D(Hinge.X + 4.f, Hinge.Y + Setup.WallThickness));
		return Sweep.Intersect(FBox2D(Centre - Half, Centre + Half));
	};
	for (int32 i = 0; i < 16; ++i)
	{
		const int32 Side = i % 4;
		const float Along = Random.FRandRange(0.f, 1.f);
		const float Out = Random.FRandRange(10.f, 24.f);
		const float Length = Random.FRandRange(70.f, 150.f);
		const float Width = Random.FRandRange(16.f, 26.f);
		const float Opacity = Random.FRandRange(0.22f, 0.34f);
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
		const FVector2D Half = StudyFloorDecalHalfExtent(FVector2D(Length, Width), Roll + Turn);
		if (FloorDustHitsSomething(Spot, Half) || ReachesDoor(Spot, Half))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(Spot.X, Spot.Y, StudyFloorDecalZ), FRotator(-90.f, 0.f, Roll + Turn), FVector2D(Length, Width), StudyDust, Opacity, 1.35f);
	}
	// A thin film over the open floor, between the furniture: laid smaller where it would reach a
	// foot, left out only when even half its size does. Every number drawn before the test (09-29).
	for (int32 i = 0; i < 7; ++i)
	{
		const float X = Random.FRandRange(WestX() + 120.f, EastX() - 120.f);
		const float Y = Random.FRandRange(NorthY() + 140.f, SouthY() - 140.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float SizeV = Random.FRandRange(120.f, 200.f);
		const float SizeU = Random.FRandRange(160.f, 240.f);
		const float Opacity = Random.FRandRange(0.14f, 0.22f);
		const float* Fit = Algo::FindByPredicate(StudyFilmScales, [&](float Scale)
		{
			const FVector2D Half = StudyFloorDecalHalfExtent(FVector2D(SizeU, SizeV) * Scale, Roll);
			return !FloorDustHitsSomething(FVector2D(X, Y), Half) && !ReachesDoor(FVector2D(X, Y), Half);
		});
		if (!Fit)
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(X, Y, StudyFloorDecalZ), FRotator(-90.f, 0.f, Roll), FVector2D(SizeU, SizeV) * *Fit, StudyDust, Opacity, 1.4f);
	}
	// The rain in at the sills for years, under each window: out from the wall along world Y, along it
	// along X (no roll), its box kept off the skirting's face.
	for (const float X : WindowXs)
	{
		const FVector2D Half = StudyFloorDecalHalfExtent(FVector2D(70.f, 140.f), 0.f);
		if (FloorDustHitsSomething(FVector2D(X, NorthY() + 42.f), Half))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(X, NorthY() + 42.f, StudyFloorDecalZ), FRotator(-90.f, 0.f, 0.f), FVector2D(70.f, 140.f),
			FLinearColor(0.16f, 0.12f, 0.09f), 0.55f, 0.55f, 0.6f);
	}
}

void AStudyActor::BuildDamage(FRoomBuilder& Build)
{
	const FLinearColor DampTint(0.46f, 0.38f, 0.30f);
	// Mould is a dark warm bloom, never green (the house's rule).
	const FLinearColor MouldTint(0.20f, 0.19f, 0.13f);
	auto Stain = [&](EWall Wall, float U, float Z, float SU, float SZ, const FLinearColor& Tint, float Opacity, float Roll, float Edge)
	{
		const FVector2D Half = StudyWallHalfExtent(SU, SZ, Roll);
		if (WallDecalHitsSomething(Wall, U, Z, Half.X, Half.Y))
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
		// Water down from the ceiling line, into the damask under the cornice. Every number is drawn
		// before the test, in order (09-29, 10-05).
		for (int32 i = 0; i < 4; ++i)
		{
			const float U = Random.FRandRange(R.U0 + 40.f, R.U1 - 40.f);
			const float SU = Random.FRandRange(70.f, 150.f);
			const float SZ = Random.FRandRange(60.f, 130.f);
			const float Opacity = Random.FRandRange(0.4f, 0.6f);
			Stain(R.Wall, U, RoomHeight - 40.f - SZ * 0.3f, SU, SZ, DampTint * 0.7f, Opacity, 0.f, 1.15f);
		}
		// Faint blooms on the damask and the panelling.
		for (int32 i = 0; i < 5; ++i)
		{
			const float U = Random.FRandRange(R.U0 + 30.f, R.U1 - 30.f);
			const float Z = Random.FRandRange(30.f, RoomHeight - 80.f);
			const float SU = Random.FRandRange(60.f, 130.f);
			const float SZ = Random.FRandRange(60.f, 120.f);
			const float Opacity = Random.FRandRange(0.12f, 0.22f);
			const float Roll = Random.FRandRange(0.f, 360.f);
			Stain(R.Wall, U, Z, SU, SZ, DampTint * 0.8f, Opacity, Roll, 1.1f);
		}
		// A crack in the plaster behind the silk, where the house has moved.
		{
			const float U = Random.FRandRange(R.U0 + 60.f, R.U1 - 60.f);
			const float Z = Random.FRandRange(DadoTop + 60.f, RoomHeight - 60.f);
			const float SU = Random.FRandRange(40.f, 80.f);
			const float SZ = Random.FRandRange(80.f, 150.f);
			const float Roll = Random.FRandRange(-20.f, 20.f);
			const FVector2D Half = StudyWallHalfExtent(SU, SZ, Roll);
			if (!WallDecalHitsSomething(R.Wall, U, Z, Half.X, Half.Y))
			{
				FVector Location;
				FRotator Rotation;
				AimAt(R.Wall, U, Z, Roll, Location, Rotation);
				Build.Crack(Location, Rotation, FVector2D(SU, SZ), 0.8f, 30.f);
			}
		}
	}

	// Round the windows, where the rain has come in round the frames: mould on the damask beyond the
	// architraves, kept off the tiebacks' hooks.
	for (const float X : WindowXs)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const float Side = (i % 2 == 0) ? -1.f : 1.f;
			const float Out = Random.FRandRange(30.f, 42.f);
			const float Z = Random.FRandRange(WindowSill + 80.f, WindowTop - 50.f);
			const float SU = Random.FRandRange(16.f, 28.f);
			const float SZ = Random.FRandRange(26.f, 56.f);
			const float Opacity = Random.FRandRange(0.5f, 0.7f);
			const float U = X + Side * (WindowHalf + WindowCasing + Out);
			if (WallDecalHitsSomething(EWall::North, U, Z, SU * 0.5f, SZ * 0.5f, /*bOpenings*/ false))
			{
				continue;
			}
			FVector Location;
			FRotator Rotation;
			AimAt(EWall::North, U, Z, 0.f, Location, Rotation);
			Build.Stain(RoomSurfaces::Damp, Location, Rotation, FVector2D(SU, SZ), MouldTint, Opacity, 1.05f);
		}
	}
}

void AStudyActor::SpawnWindows()
{
	// Two followers of the bedroom's storm in the north wall. The kitchen's own view (sky, ground,
	// trees) is out there a storey down, as the music room had it when it stood here. No own view and
	// no own trees: two worlds on one stretch of the house stand through each other (10-02).
	for (int32 i = 0; i < 2; ++i)
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
		// A dark room: little sky.
		WindowSetup.PortalScale = 0.36f;
		// No through-the-walls glow: the bedroom and the corridor are beside this room. The strike comes
		// in through the glass, on the sky portals.
		WindowSetup.GlowScale = 0.f;
		// Burgundy velour, heavy, to the floor, tied back on gilt cords; the east window's right-hand
		// cord has given way and that drape hangs loose.
		WindowSetup.CurtainSurface = &StudyVelvet;
		WindowSetup.CurtainTint = StudyVelvetTint;
		WindowSetup.DrapeWidth = 62.f;
		WindowSetup.DrapeFold = 1.7f;
		WindowSetup.DrapeTie = 0.6f;
		WindowSetup.DrapeTieGone = (i == 1) ? 1 : -1;
		WindowSetup.bDrapeToFloor = true;
		WindowSetup.PoleProud = 22.f;
		WindowSetup.InnerSillProud = 8.f;
		WindowSetup.CurtainTrim = MatGilt;
		WindowSetup.Seed = 19531104 + i * 101;
		Window->Configure(WindowSetup);
		Window->SetLead(LeadStorm);
		Window->FinishSpawning(Transform);
		Windows.Add(Window);
		const float HookReach = 6.f;
		for (const FVector& Hook : Window->GetTieHooks())
		{
			const FVector Local = GetActorTransform().InverseTransformPosition(Hook);
			WallKeepOuts.Add({ EWall::North, FVector2D(Local.X - HookReach, Local.X + HookReach), FVector2D(Local.Z - HookReach, Local.Z + HookReach) });
		}
	}
}

AClueActor* AStudyActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
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

void AStudyActor::BuildClues()
{
	// Where this room's clues will go. Set dressing only, as everywhere in the house until the game is
	// built end to end: no text, no prompt (the 09-30 decision).

	// His case board on the west wall: a road map with a ring round one bend, photographs, cuttings,
	// cards in his hand, red thread from pin to pin into the ring.
	if (AClueActor* Board = SpawnClue(WallPoint(EWall::West, BoardY, BoardZ, 0.3f), FRotator(0.f, FacingYaw(EWall::West), 0.f)))
	{
		FRoomBuilder B(Board, Board->GetRootScene());
		if (UStaticMeshComponent* Mesh = PlaceStudy(B, StudyProps::Board, FVector::ZeroVector, FRotator::ZeroRotator))
		{
			Mesh->SetReceivesDecals(false);
			KeepWallDecalsOff(Mesh);
		}
	}

	// The case files on the desk in front of his chair, and two prints off the board beside them.
	const FVector Desk = DeskSeat();
	if (AClueActor* Files = SpawnClue(Desk + FVector(-6.f, -18.f, DeskTop + 0.2f), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Files, Files->GetRootScene());
		if (UStaticMeshComponent* Mesh = PlaceStudy(B, StudyProps::Folders, FVector::ZeroVector, FRotator(0.f, -4.f, 0.f)))
		{
			Mesh->SetReceivesDecals(false);
		}
		if (UStaticMeshComponent* Mesh = PlaceStudy(B, StudyProps::Photos, FVector(34.f, 6.f, 0.1f), FRotator(0.f, 6.f, 0.f)))
		{
			Mesh->SetReceivesDecals(false);
		}
	}
}
