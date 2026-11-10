#include "BathroomActor.h"
#include "RoomBuildLibrary.h"
#include "StormWindowActor.h"
#include "ClueActor.h"
#include "DustMotesComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	/** The generated models (Tools/make_bathroom.py), /Game/Meshes/<name>. */
	namespace BathProps
	{
		const TCHAR* Bath = TEXT("bathroom_bath");
		const TCHAR* Filler = TEXT("bathroom_bath_filler");
		const TCHAR* Screen = TEXT("bathroom_shower_screen");
		const TCHAR* ShowerSet = TEXT("bathroom_shower_set");
		const TCHAR* Drain = TEXT("bathroom_shower_drain");
		const TCHAR* ShowerBottles = TEXT("bathroom_shower_bottles");
		const TCHAR* Vanity = TEXT("bathroom_vanity");
		const TCHAR* Mirror = TEXT("bathroom_mirror");
		const TCHAR* Bulb = TEXT("bathroom_bulb");
		const TCHAR* BulbBroken = TEXT("bathroom_bulb_broken");
		const TCHAR* BulbShards = TEXT("bathroom_bulb_shards");
		const TCHAR* Pendant = TEXT("bathroom_pendant");
		const TCHAR* PendantShards = TEXT("bathroom_pendant_shards");
		const TCHAR* Sconce = TEXT("bathroom_sconce");
		const TCHAR* SconceBroken = TEXT("bathroom_sconce_broken");
		const TCHAR* Toilet = TEXT("bathroom_toilet");
		const TCHAR* RollHolder = TEXT("bathroom_roll_holder");
		const TCHAR* TowelRail = TEXT("bathroom_towel_rail");
		const TCHAR* TowelBig = TEXT("bathroom_towel_big");
		const TCHAR* TowelHand = TEXT("bathroom_towel_hand");
		const TCHAR* TowelBath = TEXT("bathroom_towel_bath");
		const TCHAR* TowelFolded = TEXT("bathroom_towel_folded");
		const TCHAR* Chair = TEXT("bathroom_chair");
		const TCHAR* Fern = TEXT("bathroom_fern");
		const TCHAR* Caddy = TEXT("bathroom_caddy");
		const TCHAR* Duck = TEXT("bathroom_duck");
		const TCHAR* ToothGlass = TEXT("bathroom_tooth_glass");
		const TCHAR* ShavingSet = TEXT("bathroom_shaving_set");
		const TCHAR* SoapDish = TEXT("bathroom_soap_dish");
		const TCHAR* Bottles = TEXT("bathroom_bottles");
		const TCHAR* TilePieces = TEXT("bathroom_tile_pieces");
		const TCHAR* TileCap = TEXT("bathroom_tile_cap");
		const TCHAR* TilePencil = TEXT("bathroom_tile_pencil");
		const TCHAR* TileSkirt = TEXT("bathroom_tile_skirt");
		// Shared with the other rooms.
		const TCHAR* Cornice = TEXT("library_cornice");
	}

	/** Model constants the actor needs from make_bathroom.py beyond the header's. */
	constexpr float BathRailBars[3] = { 44.f, 66.f, 88.f };
	constexpr float BathBulbZs[5] = { -40.f, -20.f, 0.f, 20.f, 40.f };
	/** The roll the bath towel hangs over in its model (make_bathroom.py: towel_on_bath). */
	constexpr float BathTowelRoll = 1.6f;
	/** The water's surface in the bath (make_bathroom.py: bath, the water slab's top). */
	constexpr float BathWaterZ = 26.1f;
	/** The cistern's top (make_bathroom.py: toilet, cz1 + 2.2). */
	constexpr float BathCisternTop = 218.2f;
	/** The cornice's drop under the ceiling (make_library.py: cornice). */
	constexpr float BathCorniceDrop = 34.f;
	/** A decal reaches nine either way along its aim (FRoomBuilder::Stain); floor ones go at +4. */
	constexpr float BathFloorDecalZ = 4.f;

	/** The house's dust, and the film of it laid on the paint photograph (the bedroom's 11-06 rule). */
	const FLinearColor BathFilm(0.21f, 0.20f, 0.17f);
	const FLinearColor BathWater(0.20f, 0.15f, 0.08f);
	const FLinearColor BathMould(0.10f, 0.13f, 0.09f);
	const FLinearColor BathRust(0.30f, 0.12f, 0.04f);

	/** For a model's own UVs, already in repeats of the slot's texel size: tiling at one. */
	void BathSheet(UMaterialInstanceDynamic* Mat)
	{
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}

	void BathNoDecals(UPrimitiveComponent* Part)
	{
		if (Part)
		{
			Part->SetReceivesDecals(false);
		}
	}

	/** An instance straight from a surface's asset, with its repeats written out (the 10-24 floor rule). */
	UMaterialInstanceDynamic* BathOwnInstance(AActor* Owner, const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale, const FVector2D& Tiling)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), Set.Set, Set.Set));
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Parent, Owner);
		Mat->SetVectorParameterValue(TEXT("Tint"), Tint);
		Mat->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
		Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(Tiling.X, Tiling.Y, 0.f, 1.f));
		Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		return Mat;
	}

	/**
	 * A sheet laid over pieces of a wall, its UVs in centimetres from the wall's own origin over
	 * TexelCm, so the tiles and the paint run on across every cut (the bedroom's facade). Both
	 * windings, the normal forced (the 09-21 pane lesson).
	 */
	UProceduralMeshComponent* BathFacade(AActor* Owner, USceneComponent* Parent, const TArray<FBox2D>& Pieces, const FVector& Origin,
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
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("BathWall")));
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

	/** A footprint's half extent once turned by its roll: its two sizes rotated. */
	FVector2D BathRolledHalf(const FVector2D& Size, float Roll)
	{
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		return FVector2D(Cos * Size.X * 0.5f + Sin * Size.Y * 0.5f, Sin * Size.X * 0.5f + Cos * Size.Y * 0.5f);
	}
}

ABathroomActor::ABathroomActor()
{
	// Ticks only to tell the storms' windows whether the eye is in here (see Tick).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);
	RoomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void ABathroomActor::Configure(const FBathroomSetup& InSetup)
{
	Setup = InSetup;

	// Here and not in BeginPlay: the motes are scattered in the component's own BeginPlay, which runs
	// before the owner's (see ARoomDressingActor::Configure).
	DustMotes->ConfigureVolume(
		FVector(RoomWidth * 0.47f, RoomDepth * 0.47f, RoomHeight * 0.46f),
		FVector(MidX(), MidY(), RoomHeight * 0.5f));
}

void ABathroomActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning this room never relays the corridor or the girl's room.
	Random.Initialize(19621110);

	FRoomBuilder Build(this, RoomRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildFloor(Build);
	BuildWalls(Build);
	BuildTileTrim(Build);
	BuildCeiling(Build);
	BuildBath(Build);
	BuildShower(Build);
	BuildVanity(Build);
	BuildMirror(Build);
	BuildLamps(Build);
	BuildToilet(Build);
	BuildFurniture(Build);
	BuildDecay(Build);
	BuildClues();
}

void ABathroomActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// No window: nothing of the storm gets in here but its sound. The flash glows of the windows
	// without a view of their own are unshadowed point lights 24m across (the 10-05 cellar note),
	// and the corridor's, the girl's room's and the bedroom's are all within reach of this room,
	// through its walls. While the eye is in here they stay dark, as they do underground.
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector Eye = GetActorTransform().InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation());
	const bool bInside = Eye.X > WestX() && Eye.X < EastX() && Eye.Y > NorthY() && Eye.Y < SouthY() && Eye.Z > -10.f && Eye.Z < RoomHeight;
	if (bInside != bEyeInside)
	{
		bEyeInside = bInside;
		AStormWindowActor::SetViewSealed(bInside);
	}
}

void ABathroomActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AStormWindowActor::SetViewSealed(false);
	Super::EndPlay(EndPlayReason);
}

void ABathroomActor::CacheMaterials(FRoomBuilder& Build)
{
	// A sheet instance per surface for the generated models' own UVs, a hair off any tint the
	// builder's boxes use so the cache hands back a separate instance (the living room's rule).
	auto Sheet = [&](const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale = 1.f)
	{
		UMaterialInstanceDynamic* Mat = Build.Surface(Set, Tint + FLinearColor(0.0007f, 0.f, 0.f, 0.f), RoughnessScale);
		BathSheet(Mat);
		return Mat;
	};

	MatShell = Build.Flat(FLinearColor(0.02f, 0.02f, 0.02f), 1.f);

	// The tiles measure linear (0.639, 0.615, 0.541): a quarter of that is a white gone grey and
	// ivory in the dust, at about 0.16 — the palest surface in the room, as white tiles are, and
	// still a good way under what the lantern burns out. A little rougher than the bake's glaze, so
	// each tile takes a soft highlight rather than mirroring the flame.
	MatTiles = BathOwnInstance(this, RoomSurfaces::BathTiles, FLinearColor(0.255f, 0.255f, 0.262f), 1.35f, FVector2D(1.f, 1.f));
	if (!MatTiles)
	{
		MatTiles = Sheet(RoomSurfaces::BathTiles, FLinearColor(0.255f, 0.255f, 0.262f), 1.35f);
	}
	// Over the tiles, bottle green on the room's smooth paint (linear 0.596 / 0.580 / 0.534):
	// about (0.03, 0.05, 0.037). Dark, as the Victorians painted a bathroom over its tiles, and
	// cool, the one cool colour in the house: there is no window to throw a cold light in here for
	// a warm tint to answer.
	MatWall = Sheet(RoomSurfaces::Paint, FLinearColor(0.052f, 0.086f, 0.069f), 1.1f);
	MatCeiling = Build.Surface(RoomSurfaces::Paint, FLinearColor(0.24f, 0.225f, 0.20f), 1.1f);
	// The cornice and the door's architrave: ivory paint, a shade under the ceiling's.
	MatTrim = Sheet(RoomSurfaces::Paint, FLinearColor(0.25f, 0.236f, 0.212f), 1.f);
	MatTrimBox = Build.Surface(RoomSurfaces::Paint, FLinearColor(0.25f, 0.236f, 0.212f), 1.f);
	// The vanity's lacquer: a deep green on the paint photograph, at about (0.024, 0.06, 0.042), and
	// half as rough again as the walls are matt.
	MatLacquer = Sheet(RoomSurfaces::Paint, FLinearColor(0.04f, 0.10f, 0.078f), 0.55f);
	// White marble with a grey vein, dulled; the shower's tray the same stone near black.
	MatMarble = Sheet(RoomSurfaces::Marble, FLinearColor(0.30f, 0.29f, 0.27f), 1.25f);
	MatSlate = Build.Surface(RoomSurfaces::Marble, FLinearColor(0.035f, 0.035f, 0.04f), 1.5f);
	// Towels on the linen photograph (blue linen, linear 0.284 / 0.408 / 0.614): an ivory towel gone
	// the yellow-grey of cloth nobody has washed in years, a sage one, the mat a faded rose.
	MatTowel = Sheet(RoomSurfaces::Drapery, FLinearColor(0.60f, 0.40f, 0.23f), 1.25f);
	MatTowelGreen = Sheet(RoomSurfaces::Drapery, FLinearColor(0.18f, 0.17f, 0.09f), 1.25f);
	MatMat = Sheet(RoomSurfaces::Drapery, FLinearColor(0.30f, 0.16f, 0.11f), 1.35f);

	// Glass: the tumbler and the bottles at the clock crystal's value (09-21); the screen milky with
	// limescale; the bulbs' globes grey with dust.
	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.035f, 0.06f);
	MatScreenGlass = Build.Glass(FLinearColor(0.20f, 0.21f, 0.20f), 0.09f, 0.25f);
	MatBulbGlass = Build.Glass(FLinearColor(0.26f, 0.25f, 0.21f), 0.2f, 0.22f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);

	struct FFlat { const TCHAR* Name; FLinearColor Color; float Roughness; float Metallic; };
	const FFlat FlatList[] = {
		{ TEXT("Shadow"), FLinearColor(0.004f, 0.004f, 0.004f), 1.f, 0.f },
		// White enamel and glazed china, under the dust: a shade under the tiles, satin.
		{ TEXT("Enamel"), FLinearColor(0.19f, 0.185f, 0.17f), 0.24f, 0.f },
		{ TEXT("Ceramic"), FLinearColor(0.19f, 0.185f, 0.17f), 0.26f, 0.f },
		{ TEXT("Glaze"), FLinearColor(0.165f, 0.16f, 0.145f), 0.3f, 0.f },
		{ TEXT("GlazeBlack"), FLinearColor(0.008f, 0.008f, 0.009f), 0.28f, 0.f },
		{ TEXT("Biscuit"), FLinearColor(0.12f, 0.08f, 0.055f), 0.95f, 0.f },
		// The bath's outside: black paint on iron, flat.
		{ TEXT("BathPaint"), FLinearColor(0.011f, 0.011f, 0.012f), 0.55f, 0.f },
		// Brass gone brown, every fitting in the room; chrome on his razor and the bath's hose.
		{ TEXT("Brass"), FLinearColor(0.17f, 0.115f, 0.05f), 0.48f, 1.f },
		{ TEXT("Chrome"), FLinearColor(0.26f, 0.26f, 0.26f), 0.3f, 1.f },
		{ TEXT("BlackMetal"), FLinearColor(0.012f, 0.012f, 0.013f), 0.5f, 0.f },
		{ TEXT("CastIron"), FLinearColor(0.018f, 0.018f, 0.02f), 0.7f, 0.f },
		// The mirror: silvered glass gone dull, and still a mirror; the decay is in decals over it. At
		// 0.16, and still at 0.085, it gave back the lantern-lit room behind the eye as a white board.
		{ TEXT("Mirror"), FLinearColor(0.05f, 0.05f, 0.05f), 0.08f, 1.f },
		// Opal glass: the pendant's globe and the sconces' shades, milk-white under dust.
		{ TEXT("Opal"), FLinearColor(0.18f, 0.175f, 0.16f), 0.3f, 0.f },
		{ TEXT("Filament"), FLinearColor(0.03f, 0.028f, 0.025f), 0.6f, 0.f },
		// The water in the bath and the pan: brown, still, and the one mirror-smooth thing here.
		{ TEXT("Water"), FLinearColor(0.012f, 0.009f, 0.005f), 0.03f, 0.f },
		{ TEXT("Wood"), FLinearColor(0.045f, 0.018f, 0.009f), 0.45f, 0.f },
		{ TEXT("Bentwood"), FLinearColor(0.032f, 0.019f, 0.011f), 0.5f, 0.f },
		{ TEXT("Cane"), FLinearColor(0.11f, 0.075f, 0.04f), 0.8f, 0.f },
		{ TEXT("Paper"), FLinearColor(0.16f, 0.155f, 0.14f), 0.9f, 0.f },
		{ TEXT("Sponge"), FLinearColor(0.10f, 0.068f, 0.034f), 0.95f, 0.f },
		{ TEXT("Soap"), FLinearColor(0.15f, 0.12f, 0.085f), 0.7f, 0.f },
		{ TEXT("Wax"), FLinearColor(0.17f, 0.15f, 0.11f), 0.6f, 0.f },
		// The duck: a yellow gone the colour of old teeth.
		{ TEXT("Duck"), FLinearColor(0.19f, 0.13f, 0.025f), 0.45f, 0.f },
		{ TEXT("Beak"), FLinearColor(0.19f, 0.06f, 0.012f), 0.45f, 0.f },
		{ TEXT("Black"), FLinearColor(0.01f, 0.01f, 0.011f), 0.4f, 0.f },
		// Three toothbrushes: his blue, her green, hers — the small one — pink.
		{ TEXT("BrushA"), FLinearColor(0.02f, 0.045f, 0.13f), 0.35f, 0.f },
		{ TEXT("BrushB"), FLinearColor(0.02f, 0.08f, 0.045f), 0.35f, 0.f },
		{ TEXT("BrushC"), FLinearColor(0.20f, 0.05f, 0.11f), 0.35f, 0.f },
		{ TEXT("Bristle"), FLinearColor(0.17f, 0.165f, 0.15f), 0.8f, 0.f },
		{ TEXT("Badger"), FLinearColor(0.07f, 0.058f, 0.044f), 0.9f, 0.f },
		{ TEXT("BottleA"), FLinearColor(0.17f, 0.165f, 0.155f), 0.4f, 0.f },
		{ TEXT("BottleB"), FLinearColor(0.015f, 0.025f, 0.035f), 0.35f, 0.f },
		{ TEXT("BottleC"), FLinearColor(0.07f, 0.045f, 0.03f), 0.4f, 0.f },
		{ TEXT("Liquid"), FLinearColor(0.09f, 0.045f, 0.012f), 0.1f, 0.f },
		{ TEXT("Cap"), FLinearColor(0.01f, 0.01f, 0.01f), 0.4f, 0.f },
		{ TEXT("Pot"), FLinearColor(0.09f, 0.035f, 0.018f), 0.85f, 0.f },
		{ TEXT("Soil"), FLinearColor(0.025f, 0.018f, 0.012f), 1.f, 0.f },
		{ TEXT("Frond"), FLinearColor(0.07f, 0.045f, 0.02f), 0.85f, 0.f },
		{ TEXT("Stem"), FLinearColor(0.045f, 0.03f, 0.018f), 0.9f, 0.f },
	};
	for (const FFlat& Flat : FlatList)
	{
		Flats.Add(FName(Flat.Name), Build.Flat(Flat.Color, Flat.Roughness, Flat.Metallic));
	}
	// The slots that take a sheet or a glass rather than a flat colour.
	SlotMats.Add(TEXT("Lacquer"), MatLacquer);
	SlotMats.Add(TEXT("Marble"), MatMarble);
	SlotMats.Add(TEXT("Towel"), MatTowel);
	SlotMats.Add(TEXT("Towel2"), MatTowelGreen);
	SlotMats.Add(TEXT("Glass"), MatGlass);
	SlotMats.Add(TEXT("BulbGlass"), MatBulbGlass);
	SlotMats.Add(TEXT("Trim"), MatTrim);
	for (const TPair<FName, TObjectPtr<UMaterialInstanceDynamic>>& Flat : Flats)
	{
		SlotMats.Add(Flat.Key, Flat.Value);
	}
}

UMaterialInterface* ABathroomActor::F(const TCHAR* Name) const
{
	const TObjectPtr<UMaterialInstanceDynamic>* Found = Flats.Find(FName(Name));
	return Found ? Found->Get() : MatVoid.Get();
}

void ABathroomActor::Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Overrides) const
{
	if (!Mesh)
	{
		return;
	}
	// Every slot by its name from the room's set, then whatever this placement says otherwise.
	for (const FName& Slot : Mesh->GetMaterialSlotNames())
	{
		if (const TObjectPtr<UMaterialInterface>* Found = SlotMats.Find(Slot))
		{
			Mesh->SetMaterialByName(Slot, Found->Get());
		}
	}
	for (const TPair<const TCHAR*, UMaterialInterface*>& Slot : Overrides)
	{
		if (Slot.Value && Mesh->GetMaterialIndex(FName(Slot.Key)) != INDEX_NONE)
		{
			Mesh->SetMaterialByName(FName(Slot.Key), Slot.Value);
		}
	}
}

UStaticMeshComponent* ABathroomActor::Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
	std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Overrides)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	Dress(Mesh, Overrides);
	return Mesh;
}

void ABathroomActor::Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw)
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

void ABathroomActor::Dust(FRoomBuilder& Build, const FVector& Top, const FVector2D& Size, float Yaw, float Opacity) const
{
	// Aimed down, a decal lays its first size along world Y and its second along X (09-27): Size is
	// (X, Y) in the piece's own frame and swapped here, the roll turning it with the piece.
	Build.Stain(RoomSurfaces::Paint, Top + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, Yaw), FVector2D(Size.Y, Size.X), BathFilm, Opacity * 0.8f, 0.45f);
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers.
// ---------------------------------------------------------------------------------------------

float ABathroomActor::WallFace(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return NorthY();
	case EWall::South: return SouthY();
	case EWall::East:  return EastX();
	default:           return WestX();
	}
}

FVector ABathroomActor::WallNormal(EWall Wall) const
{
	switch (Wall)
	{
	case EWall::North: return FVector(0.f, 1.f, 0.f);
	case EWall::South: return FVector(0.f, -1.f, 0.f);
	case EWall::East:  return FVector(-1.f, 0.f, 0.f);
	default:           return FVector(1.f, 0.f, 0.f);
	}
}

FVector ABathroomActor::WallPoint(EWall Wall, float U, float Z, float Proud) const
{
	const FVector OnFace = (Wall == EWall::North || Wall == EWall::South)
		? FVector(U, WallFace(Wall), Z)
		: FVector(WallFace(Wall), U, Z);
	return OnFace + WallNormal(Wall) * Proud;
}

float ABathroomActor::FacingYaw(EWall Wall)
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

TArray<FBox2D> ABathroomActor::Openings(EWall Wall, bool bWithArchitrave) const
{
	TArray<FBox2D> Holes;
	if (Wall == EWall::North)
	{
		const float Trim = bWithArchitrave ? DoorCasing : 0.f;
		const float Half = Setup.DoorHalf + Trim;
		Holes.Add(FBox2D(FVector2D(Setup.DoorX - Half, 0.f), FVector2D(Setup.DoorX + Half, Setup.DoorHeight + Trim)));
	}
	return Holes;
}

TArray<FVector> ABathroomActor::TileRuns(EWall Wall) const
{
	// The shower's corner is tiled to over the head on its two walls, to just past the screen's
	// channel on the north wall and to the tray's end on the west; the rest of the room to the cap.
	const float ScreenEnd = WestX() + TrayWidth + 5.f;
	const float TrayEnd = NorthY() + TrayDepth;
	TArray<FVector> Runs;
	switch (Wall)
	{
	case EWall::North:
		Runs.Add(FVector(WestX(), ScreenEnd, ShowerTileTop));
		Runs.Add(FVector(ScreenEnd, EastX(), TileTop));
		break;
	case EWall::West:
		Runs.Add(FVector(NorthY(), TrayEnd, ShowerTileTop));
		Runs.Add(FVector(TrayEnd, SouthY(), TileTop));
		break;
	case EWall::South:
		Runs.Add(FVector(WestX(), EastX(), TileTop));
		break;
	default:
		Runs.Add(FVector(NorthY(), SouthY(), TileTop));
		break;
	}
	return Runs;
}

TArray<FBox2D> ABathroomActor::WallKeepOuts(EWall Wall) const
{
	TArray<FBox2D> Out;
	switch (Wall)
	{
	case EWall::West:
	{
		// The vanity's back on the tiles, its mixer standing to 116; the mirror with its frame and
		// bulbs over it; the shower's valve, riser and arm; the screen's stay where it meets this wall;
		// the hamper ten off the tiles; the bottles in the corner.
		const float VanityHalf = VanityWidth * 0.5f + 2.f;
		Out.Add(FBox2D(FVector2D(VanityY - VanityHalf, 38.f), FVector2D(VanityY + VanityHalf, 118.f)));
		const float MirrorHalfU = MirrorWidth * 0.5f + MirrorSide + 6.f;
		Out.Add(FBox2D(FVector2D(VanityY - MirrorHalfU, MirrorZ - MirrorHeight * 0.5f - 6.f), FVector2D(VanityY + MirrorHalfU, MirrorZ + MirrorHeight * 0.5f + 6.f)));
		const float ShowerU = NorthY() + 55.f;
		Out.Add(FBox2D(FVector2D(ShowerU - 18.f, TrayTop + 95.f), FVector2D(ShowerU + 18.f, TrayTop + 216.f)));
		Out.Add(FBox2D(FVector2D(NorthY() + ScreenWidth - 8.f, TrayTop + 194.f), FVector2D(NorthY() + ScreenWidth + 8.f, TrayTop + 210.f)));
		Out.Add(FBox2D(FVector2D(HamperSeat().Y - 26.f, 0.f), FVector2D(HamperSeat().Y + 26.f, 52.f)));
		Out.Add(FBox2D(FVector2D(NorthY(), 0.f), FVector2D(NorthY() + 34.f, 30.f)));
		break;
	}
	case EWall::North:
		// The screen's channel and the bottles in the corner.
		Out.Add(FBox2D(FVector2D(WestX() + TrayWidth - 10.f, 0.f), FVector2D(WestX() + TrayWidth + 6.f, TrayTop + 206.f)));
		Out.Add(FBox2D(FVector2D(WestX(), 0.f), FVector2D(WestX() + 34.f, 30.f)));
		break;
	case EWall::East:
	{
		// The cistern on its brackets, its pipe, its chain and lever; the roll holder; the rail's feet.
		const FVector WC = ToiletSeat();
		Out.Add(FBox2D(FVector2D(WC.Y - 32.f, 0.f), FVector2D(WC.Y + 36.f, BathCisternTop + 4.f)));
		Out.Add(FBox2D(FVector2D(WC.Y - 92.f, 44.f), FVector2D(WC.Y - 62.f, 82.f)));
		Out.Add(FBox2D(FVector2D(RailSeat().Y - RailWidth * 0.5f - 4.f, 0.f), FVector2D(RailSeat().Y + RailWidth * 0.5f + 4.f, 14.f)));
		break;
	}
	case EWall::South:
		// The sconces over the bath.
		for (const float Side : { -1.f, 1.f })
		{
			const float U = BathSeat().X + Side * SconceSpread;
			Out.Add(FBox2D(FVector2D(U - 12.f, SconceZ - 14.f), FVector2D(U + 12.f, SconceZ + 34.f)));
		}
		break;
	}
	return Out;
}

bool ABathroomActor::WallDecalHitsSomething(EWall Wall, float U, float Z, const FVector2D& Size, float Roll) const
{
	// The door (the 10-05 rule: a decal reaching a doorway smears down its reveal and over the leaf),
	// and anything on the wall inside a decal's reach.
	const FVector2D Half = BathRolledHalf(Size, Roll);
	const FBox2D Footprint(FVector2D(U, Z) - Half, FVector2D(U, Z) + Half);
	for (const FBox2D& Opening : Openings(Wall, /*bWithArchitrave*/ true))
	{
		if (Footprint.Intersect(Opening.ExpandBy(15.f)))
		{
			return true;
		}
	}
	for (const FBox2D& KeepOut : WallKeepOuts(Wall))
	{
		if (Footprint.Intersect(KeepOut.ExpandBy(5.f)))
		{
			return true;
		}
	}
	return false;
}

bool ABathroomActor::FloorDecalReachesDoor(const FVector2D& Centre, const FVector2D& Size, float Roll) const
{
	// The leaf sweeps a quarter disc into the room from its hinge on the west jamb; the sill is the
	// opening's width across the wall. Both by the decal's rolled footprint, not its centre.
	const FVector2D Half = BathRolledHalf(Size, Roll);
	const FBox2D Footprint(Centre - Half, Centre + Half);
	const FBox2D Sill(FVector2D(Setup.DoorX - Setup.DoorHalf - 10.f, NorthY() - 25.f), FVector2D(Setup.DoorX + Setup.DoorHalf + 10.f, NorthY() + 10.f));
	if (Footprint.Intersect(Sill))
	{
		return true;
	}
	const FVector2D Hinge(Setup.HingeX, NorthY());
	const float Reach = Setup.DoorLeafWidth + 12.f;
	if (Footprint.Max.X < Hinge.X - 10.f || Footprint.Max.Y < Hinge.Y)
	{
		return false;
	}
	const FVector2D Closest(FMath::Clamp(Hinge.X, Footprint.Min.X, Footprint.Max.X), FMath::Clamp(Hinge.Y, Footprint.Min.Y, Footprint.Max.Y));
	return FVector2D::Distance(Closest, Hinge) < Reach;
}

void ABathroomActor::Trim(FRoomBuilder& Build, const TCHAR* Name, EWall Wall, float U, float Z, float Length, bool bVertical, float Proud, UMaterialInterface* Mat)
{
	// The trim models are a metre along X with their profile in YZ, the wall at y = 0 and the room at
	// +Y. Laid along a wall, X runs along it and Z stays up; stood up, X runs up the wall.
	const FVector Normal = WallNormal(Wall);
	const FVector Up(0.f, 0.f, 1.f);
	const FRotator Rotation = bVertical
		? FRotationMatrix::MakeFromXY(Up, Normal).Rotator()
		: FRotationMatrix::MakeFromXY(Normal ^ Up, Normal).Rotator();
	if (UStaticMeshComponent* Mesh = Place(Build, Name, WallPoint(Wall, U, Z, Proud), Rotation, { { TEXT("Glaze"), Mat }, { TEXT("GlazeBlack"), Mat } }))
	{
		Mesh->SetRelativeScale3D(FVector(Length / 100.f, 1.f, 1.f));
	}
}

// ---------------------------------------------------------------------------------------------
// The shell and its finish.
// ---------------------------------------------------------------------------------------------

void ABathroomActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float Z0 = -20.f;
	const float Z1 = RoomHeight + 10.f;

	// Solid, colliding and never seen. The north wall is the corridor's south wall up to the
	// corridor's height, the east wall the girl's room's west wall; this builds the west and south
	// walls, the strip of north wall over the corridor's ceiling, the ceiling and a subfloor. No
	// opening anywhere but the door: there is no window.
	Build.Box(FVector(WestX() - T * 0.5f, MidY(), (Z0 + Z1) * 0.5f), FRotator::ZeroRotator, FVector(T, RoomDepth + T * 2.f, Z1 - Z0), MatShell);
	Build.Box(FVector(MidX(), SouthY() + T * 0.5f, (Z0 + Z1) * 0.5f), FRotator::ZeroRotator, FVector(RoomWidth + T * 2.f, T, Z1 - Z0), MatShell);
	const float StripZ0 = Setup.CorridorHeight - 5.f;
	Build.Box(FVector(MidX(), NorthY() - T * 0.5f, (StripZ0 + Z1) * 0.5f), FRotator::ZeroRotator, FVector(RoomWidth + T * 2.f, T, Z1 - StripZ0), MatShell);
	Build.Box(FVector(MidX(), MidY(), RoomHeight + 5.f), FRotator::ZeroRotator, FVector(RoomWidth + T * 2.f, RoomDepth + T * 2.f, 10.f), MatShell);
	Build.Box(FVector(MidX(), MidY(), -8.f), FRotator::ZeroRotator, FVector(RoomWidth, RoomDepth, 6.f), MatShell);
}

void ABathroomActor::BuildFloor(FRoomBuilder& Build)
{
	// The hexagon mosaic wall to wall: one box, its top at zero, with an instance of its own whose
	// repeats are written out for each side, since the mosaic's repeat is not square (54 x 51.96).
	// It measures linear (0.442, 0.417, 0.352); this lands its white at about 0.19 and its black
	// near nothing, under a dust that takes the glaze's shine off.
	const FVector2D Tiling(RoomWidth / MosaicW, RoomDepth / MosaicH);
	MatFloor = BathOwnInstance(this, RoomSurfaces::BathMosaic, FLinearColor(0.32f, 0.315f, 0.31f), 1.4f, Tiling);
	if (!MatFloor)
	{
		MatFloor = Build.Surface(RoomSurfaces::BathMosaic, FLinearColor(0.32f, 0.315f, 0.31f), 1.4f);
	}
	Build.Box(FVector(MidX(), MidY(), -2.f), FRotator::ZeroRotator, FVector(RoomWidth, RoomDepth, 4.f), MatFloor);
}

void ABathroomActor::BuildWalls(FRoomBuilder& Build)
{
	// Tiles to the cap (to over the head in the shower), bottle-green paint over them to the
	// ceiling: one sheet per run of each, cut round the door, with UVs in centimetres so the courses
	// and the paint run on across every cut.
	for (const EWall Wall : { EWall::North, EWall::South, EWall::East, EWall::West })
	{
		const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
		const FVector Along = bAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
		for (const FVector& Run : TileRuns(Wall))
		{
			const TArray<FBox2D> Tiles = RoomWalls::CutAround(Openings(Wall, /*bWithArchitrave*/ true), Run.X, Run.Y, 0.f, Run.Z);
			BathFacade(this, RoomRoot, Tiles, WallPoint(Wall, 0.f, 0.f, TileProud), Along, WallNormal(Wall), TileSheetCm, MatTiles);
			const TArray<FBox2D> Paint = RoomWalls::CutAround(Openings(Wall, /*bWithArchitrave*/ false), Run.X, Run.Y, Run.Z, RoomHeight);
			BathFacade(this, RoomRoot, Paint, WallPoint(Wall, 0.f, 0.f, 0.3f), Along, WallNormal(Wall), RoomSurfaces::Paint.TexelSizeCm, MatWall);
		}
	}

	// The door: its reveal lined the depth of the wall, and an ivory architrave on this side, flat
	// and broad, which the tiles and their trim are cut to (Openings).
	const float T = Setup.WallThickness;
	const float RevealY = NorthY() - T * 0.5f;
	for (const float Side : { -1.f, 1.f })
	{
		Build.Box(FVector(Setup.DoorX + Side * (Setup.DoorHalf - 0.6f), RevealY, Setup.DoorHeight * 0.5f), FRotator::ZeroRotator,
			FVector(1.2f, T, Setup.DoorHeight), MatTrimBox, false);
		const FVector At = WallPoint(EWall::North, Setup.DoorX + Side * (Setup.DoorHalf + DoorCasing * 0.5f), Setup.DoorHeight * 0.5f, 1.1f);
		Build.Box(At, FRotator::ZeroRotator, FVector(DoorCasing, 2.2f, Setup.DoorHeight), MatTrimBox, false);
	}
	Build.Box(FVector(Setup.DoorX, RevealY, Setup.DoorHeight - 0.6f), FRotator::ZeroRotator, FVector(Setup.DoorHalf * 2.f, T, 1.2f), MatTrimBox, false);
	Build.Box(WallPoint(EWall::North, Setup.DoorX, Setup.DoorHeight + DoorCasing * 0.5f, 1.1f), FRotator::ZeroRotator,
		FVector(Setup.DoorHalf * 2.f + DoorCasing * 2.f, 2.2f, DoorCasing), MatTrimBox, false);
}

void ABathroomActor::BuildTileTrim(FRoomBuilder& Build)
{
	// The glazed cap along the top of every tiled run, the black liner across them all at PencilZ,
	// the black cove skirting at the foot (none in the shower, whose tray stands there). All cut
	// round the door's architrave and laid in pieces of a metre or under.
	UMaterialInterface* Glaze = F(TEXT("Glaze"));
	UMaterialInterface* Black = F(TEXT("GlazeBlack"));
	auto Lay = [&](const TCHAR* Name, EWall Wall, float U0, float U1, float Z, float Height, float Proud, UMaterialInterface* Mat)
	{
		for (const FBox2D& Piece : RoomWalls::CutAround(Openings(Wall, /*bWithArchitrave*/ true), U0, U1, Z, Z + Height))
		{
			if (!FMath::IsNearlyEqual(Piece.Min.Y, Z, 0.5f) || !FMath::IsNearlyEqual(Piece.Max.Y, Z + Height, 0.5f) || Piece.GetSize().X < 1.f)
			{
				continue;
			}
			const int32 Count = FMath::Max(1, FMath::CeilToInt(Piece.GetSize().X / 100.f));
			const float Length = Piece.GetSize().X / Count;
			for (int32 i = 0; i < Count; ++i)
			{
				Trim(Build, Name, Wall, Piece.Min.X + Length * (i + 0.5f), Z, Length, false, Proud, Mat);
			}
		}
	};

	const float ScreenEnd = WestX() + TrayWidth + 5.f;
	const float TrayEnd = NorthY() + TrayDepth;
	for (const EWall Wall : { EWall::North, EWall::South, EWall::East, EWall::West })
	{
		const bool bAlongX = Wall == EWall::North || Wall == EWall::South;
		const float U0 = bAlongX ? WestX() : NorthY();
		const float U1 = bAlongX ? EastX() : SouthY();
		for (const FVector& Run : TileRuns(Wall))
		{
			Lay(BathProps::TileCap, Wall, Run.X, Run.Y, Run.Z, CapHeight, 0.f, Glaze);
		}
		Lay(BathProps::TilePencil, Wall, U0, U1, PencilZ, 2.4f, TileProud, Black);
		// The skirting stops at the shower's tray, on the two walls it stands against.
		const float SkirtU0 = Wall == EWall::North ? ScreenEnd : (Wall == EWall::West ? TrayEnd : U0);
		Lay(BathProps::TileSkirt, Wall, SkirtU0, U1, 0.f, 13.f, TileProud, Black);
	}

	// Where the shower's tall tiles step down to the rest, a cap stood up the edge of them.
	const float Rise = ShowerTileTop - TileTop - CapHeight;
	const float RiseZ = TileTop + CapHeight + Rise * 0.5f;
	// The profile of a stood-up strip lies towards Up ^ Normal: -X on the north wall, +Y on the west.
	// Either way it has to lie over the tall tiles' last five, so it goes on the edge's line on the
	// north wall and five short of it on the west.
	Trim(Build, BathProps::TileCap, EWall::North, ScreenEnd, RiseZ, Rise, true, 0.f, Glaze);
	Trim(Build, BathProps::TileCap, EWall::West, TrayEnd - CapHeight, RiseZ, Rise, true, 0.f, Glaze);
}

void ABathroomActor::BuildCeiling(FRoomBuilder& Build)
{
	const float C = RoomHeight;
	Build.Mark(FVector(MidX(), MidY(), C - 1.f), FRotator(0.f, 0.f, 180.f), FVector2D(RoomWidth, RoomDepth), MatCeiling);

	// The library's plaster cornice, painted, along all four walls.
	struct FCorniceRun { FVector Start; FVector Dir; float Length; float Yaw; };
	const FCorniceRun Cornices[] = {
		{ FVector(WestX(), NorthY(), C), FVector(1.f, 0.f, 0.f), RoomWidth, 0.f },
		{ FVector(EastX(), SouthY(), C), FVector(-1.f, 0.f, 0.f), RoomWidth, 180.f },
		{ FVector(WestX(), SouthY(), C), FVector(0.f, -1.f, 0.f), RoomDepth, -90.f },
		{ FVector(EastX(), NorthY(), C), FVector(0.f, 1.f, 0.f), RoomDepth, 90.f },
	};
	for (const FCorniceRun& Each : Cornices)
	{
		const int32 Pieces = FMath::Max(1, FMath::CeilToInt(Each.Length / 100.f));
		const float Piece = Each.Length / Pieces;
		for (int32 i = 0; i < Pieces; ++i)
		{
			if (UStaticMeshComponent* Mesh = Place(Build, BathProps::Cornice, Each.Start + Each.Dir * (Piece * (i + 0.5f)), FRotator(0.f, Each.Yaw, 0.f), {}))
			{
				Mesh->SetRelativeScale3D(FVector(Piece / 100.f, 1.f, 1.f));
			}
		}
	}

	// Webs in the four upper corners, under the cornice.
	const FVector2D Corners[4] = { FVector2D(EastX(), NorthY()), FVector2D(EastX(), SouthY()), FVector2D(WestX(), NorthY()), FVector2D(WestX(), SouthY()) };
	for (const FVector2D& Corner : Corners)
	{
		const float SX = Corner.X < MidX() ? 1.f : -1.f;
		const float SY = Corner.Y < MidY() ? 1.f : -1.f;
		for (int32 i = 0; i < 2; ++i)
		{
			const float In = 34.f + i * 16.f;
			UStaticMeshComponent* Web = Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SX * In, Corner.Y + SY * In, C - BathCorniceDrop - 8.f - i * 6.f),
				FRotator(0.f, SX * SY > 0.f ? 45.f : -45.f, 180.f), FVector(In * 1.5f, In * 1.5f, 1.f), MatWeb, false);
			BathNoDecals(Web);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// The bath.
// ---------------------------------------------------------------------------------------------

void ABathroomActor::BuildBath(FRoomBuilder& Build)
{
	// The roll-top bath, long axis along X, its back thirty off the south wall; the filler at its
	// east end, turned so the spout reaches back over the end (a yaw of 90 turns the model's +Y to -X).
	const FVector Bath = BathSeat();
	Place(Build, BathProps::Bath, Bath, FRotator::ZeroRotator, {});
	Place(Build, BathProps::Filler, FillerSeat(), FRotator(0.f, 90.f, 0.f), {});
	Blocker(Build, Bath + FVector(0.f, 0.f, 34.f), FVector(BathLength, BathWidth, 68.f), 0.f);
	Blocker(Build, FillerSeat() + FVector(0.f, 0.f, 50.f), FVector(10.f, 18.f, 100.f), 0.f);

	// The rack across it, a third of the way from the head, on the rolls.
	Place(Build, BathProps::Caddy, Bath + FVector(-30.f, 0.f, BathRim), FRotator(0.f, 90.f, 0.f), {});

	// A towel over the side that faces the room, one end in the water. Its model hangs over a rail
	// along X, the outside its +Y: turned 180, the outside is the room. The roll's middle stands
	// 38.6 off the bath's long axis at this point along it (the plan's superellipse), its top at the rim.
	const float TowelX = Bath.X + 34.f;
	Place(Build, BathProps::TowelBath, FVector(TowelX, Bath.Y - 38.6f, BathRim - BathTowelRoll - 0.5f), FRotator(0.f, 180.f, 0.f), {});

	// A mat in front of it, the rose cotton faded to nearly nothing.
	// Rucked where a foot pushed it: at a quarter of a centimetre of rumple it read as a flat square
	// painted on the mosaic.
	Build.Cloth(FVector(Bath.X - 6.f, Bath.Y - BathWidth * 0.5f - 34.f, 0.6f), FRotator(0.f, 3.f, 0.f), FVector2D(84.f, 54.f), 1.4f, 0.f, 7101, MatMat, 34.f, /*bWorn*/ false);
}

// ---------------------------------------------------------------------------------------------
// The shower.
// ---------------------------------------------------------------------------------------------

void ABathroomActor::BuildShower(FRoomBuilder& Build)
{
	// A slate tray in the north-west corner, four high; the screen standing on it along its east
	// edge from the north wall, the gap to the south end left open to walk in; the riser and the rose
	// on the west wall; the drain along the west wall's foot.
	const float X0 = WestX();
	const float Y0 = NorthY();
	Build.Box(FVector(X0 + TrayWidth * 0.5f, Y0 + TrayDepth * 0.5f, TrayTop * 0.5f), FRotator::ZeroRotator, FVector(TrayWidth, TrayDepth, TrayTop), MatSlate);

	const float ScreenX = X0 + TrayWidth - 2.f;
	const FVector ScreenAt(ScreenX, Y0 + ScreenWidth * 0.5f, TrayTop);
	// The screen's model runs along its local X: a yaw of 90 lays it along Y.
	Place(Build, BathProps::Screen, ScreenAt, FRotator(0.f, 90.f, 0.f), { { TEXT("Glass"), MatScreenGlass } });
	Blocker(Build, ScreenAt + FVector(0.f, 0.f, 100.f), FVector(4.f, ScreenWidth, 200.f), 0.f);
	// Its channel on the north wall, and the stay from its free end's top back to the west wall.
	Build.Box(FVector(ScreenX, Y0 + TileProud + 1.5f, TrayTop + 100.f), FRotator::ZeroRotator, FVector(5.f, 3.f, 200.f), F(TEXT("BlackMetal")), false);
	const float StayY = Y0 + ScreenWidth - 1.5f;
	Build.Cyl(FVector((X0 + ScreenX) * 0.5f, StayY, TrayTop + 202.f), FRotator(90.f, 0.f, 0.f), FVector(1.2f, 1.2f, ScreenX - X0), F(TEXT("BlackMetal")), false);

	const float ShowerU = Y0 + 55.f;
	Place(Build, BathProps::ShowerSet, WallPoint(EWall::West, ShowerU, TrayTop, TileProud), FRotator(0.f, FacingYaw(EWall::West), 0.f), {});
	Place(Build, BathProps::Drain, FVector(X0 + TileProud + 5.f, Y0 + 60.f, TrayTop + 0.02f), FRotator(0.f, 90.f, 0.f), {});
	Place(Build, BathProps::ShowerBottles, FVector(X0 + 17.f, Y0 + 15.f, TrayTop), FRotator(0.f, 40.f, 0.f), {});

	// A web from the rose's arm to the corner, and one across the stay.
	UStaticMeshComponent* Web = Build.Add(FRoomShapes::Plane(), FVector(X0 + 22.f, Y0 + 30.f, TrayTop + 208.f), FRotator(0.f, 40.f, 90.f), FVector(40.f, 26.f, 1.f), MatWeb, false);
	BathNoDecals(Web);
	Web = Build.Add(FRoomShapes::Plane(), FVector(X0 + 18.f, StayY - 8.f, TrayTop + 196.f), FRotator(0.f, 10.f, 90.f), FVector(30.f, 18.f, 1.f), MatWeb, false);
	BathNoDecals(Web);
}

// ---------------------------------------------------------------------------------------------
// The vanity and the mirror.
// ---------------------------------------------------------------------------------------------

void ABathroomActor::BuildVanity(FRoomBuilder& Build)
{
	// Floating, its back on the west wall's tiles: facing the room is a yaw of -90.
	const FVector Seat = VanitySeat();
	const FRotator Facing(0.f, FacingYaw(EWall::West), 0.f);
	Place(Build, BathProps::Vanity, Seat, Facing, {});
	Blocker(Build, Seat + FVector(0.f, 0.f, 50.f), FVector(VanityDepth, VanityWidth, 100.f), 0.f);

	// Her soap dish at the one end (the clue things are BuildClues'), and dust on the marble either
	// side of the basin. The vanity's frame: local +X is world -Y, local +Y world +X.
	auto OnTop = [&](float LocalX, float LocalY) { return Seat + Facing.RotateVector(FVector(LocalX, LocalY, VanityTop)); };
	Place(Build, BathProps::SoapDish, OnTop(-40.f, 12.f), FRotator(0.f, Facing.Yaw + 8.f, 0.f), {});
	Dust(Build, OnTop(-43.f, 0.f), FVector2D(30.f, 46.f), Facing.Yaw, 0.45f);
	Dust(Build, OnTop(43.f, 0.f), FVector2D(30.f, 46.f), Facing.Yaw, 0.45f);

	// The web under it, from the cabinet's underside to the wall.
	UStaticMeshComponent* Web = Build.Add(FRoomShapes::Plane(), Seat + FVector(-8.f, 30.f, 26.f), FRotator(0.f, 0.f, 70.f), FVector(28.f, 40.f, 1.f), MatWeb, false);
	BathNoDecals(Web);
}

void ABathroomActor::BuildMirror(FRoomBuilder& Build)
{
	// The mirror over the vanity, hung clear of the tile cap; its bulbs in their sockets down both
	// sides. Most of them are smashed: the collar of glass round the cap and the filament's stem,
	// one gone altogether; the pieces on the floor under them. Which is which is written out, so the
	// mirror is the same every time; how each broken one sits in its socket is drawn.
	const FRotator Facing(0.f, FacingYaw(EWall::West), 0.f);
	const FVector Centre = WallPoint(EWall::West, VanityY, MirrorZ, MirrorProud);
	Place(Build, BathProps::Mirror, Centre, Facing, {});

	enum class EBulb : uint8 { Whole, Broken, Gone };
	const EBulb States[10] = {
		EBulb::Broken, EBulb::Whole, EBulb::Broken, EBulb::Broken, EBulb::Gone,
		EBulb::Whole, EBulb::Broken, EBulb::Broken, EBulb::Whole, EBulb::Broken,
	};
	for (int32 i = 0; i < 10; ++i)
	{
		// Every bulb draws its spin, whatever it is (09-29).
		const float Spin = Random.FRandRange(0.f, 360.f);
		const float Side = i < 5 ? -1.f : 1.f;
		const FVector Local(Side * BulbX, SocketY, BathBulbZs[i % 5]);
		const FVector At = Centre + Facing.RotateVector(Local);
		// The bulb's glass runs along its local +Y, out of the frame: its spin is a pitch.
		const FRotator Turn(Spin, Facing.Yaw, 0.f);
		switch (States[i])
		{
		case EBulb::Whole:  Place(Build, BathProps::Bulb, At, Turn, {}); break;
		case EBulb::Broken: Place(Build, BathProps::BulbBroken, At, Turn, {}); break;
		default: break;
		}
	}

	// The silvering gone at the edges in black blooms, and a crack across from the lower corner:
	// projected into the glass from six in front of the frame (a decal reaches nine either way: the
	// glass and the frame's face, and not the tiles behind them).
	const FRotator Into(0.f, FacingYaw(EWall::West) - 90.f, 0.f);
	const FVector Glass = WallPoint(EWall::West, VanityY, MirrorZ, MirrorProud + MirrorDepth + 6.f);
	struct FSpot { float U; float Z; float Size; };
	const FSpot Spots[] = { { -30.f, -42.f, 22.f }, { 31.f, -38.f, 18.f }, { -33.f, 36.f, 16.f }, { 28.f, 44.f, 20.f }, { -12.f, -46.f, 14.f } };
	for (const FSpot& Spot : Spots)
	{
		const float Roll = Random.FRandRange(0.f, 360.f);
		Build.Stain(RoomSurfaces::Damp, Glass + FVector(0.f, -Spot.U, Spot.Z), FRotator(Into.Pitch, Into.Yaw, Roll), FVector2D(Spot.Size, Spot.Size * 0.8f),
			FLinearColor(0.03f, 0.03f, 0.025f), 0.7f, 1.3f);
	}
	// Small enough, turned as it is, to stay inside the bulbs either side (BulbX).
	Build.Crack(Glass + FVector(0.f, -10.f, -30.f), FRotator(Into.Pitch, Into.Yaw, 35.f), FVector2D(46.f, 30.f), 0.9f, 34.f);
	// And a film of the room's dust and steam over the whole glass.
	Build.Stain(RoomSurfaces::Paint, Glass, Into, FVector2D(MirrorWidth - 4.f, MirrorHeight - 4.f), BathFilm, 0.35f, 0.5f);

	// The pieces: on the floor in front of the vanity, and a few on its marble.
	Place(Build, BathProps::BulbShards, FVector(WestX() + 70.f, VanityY - 26.f, 0.f), FRotator(0.f, 64.f, 0.f), {});
	Place(Build, BathProps::BulbShards, FVector(WestX() + 58.f, VanityY + 38.f, 0.f), FRotator(0.f, -22.f, 0.f), {});
}

// ---------------------------------------------------------------------------------------------
// The lamps.
// ---------------------------------------------------------------------------------------------

void ABathroomActor::BuildLamps(FRoomBuilder& Build)
{
	// The pendant in the middle of the ceiling, its globe gone to a ragged collar in the gallery and
	// the rest of it on the floor under it, turned a little so it lies across the room's lines.
	const FVector Rose = PendantAt();
	Place(Build, BathProps::Pendant, Rose - FVector(0.f, 0.f, 1.f), FRotator(0.f, 20.f, 0.f), {});
	Place(Build, BathProps::PendantShards, FVector(Rose.X + 6.f, Rose.Y - 4.f, 0.f), FRotator(0.f, 15.f, 0.f), {});
	// A web from the gallery to the rod.
	UStaticMeshComponent* Web = Build.Add(FRoomShapes::Plane(), Rose - FVector(0.f, 0.f, 46.f), FRotator(0.f, 30.f, 90.f), FVector(14.f, 34.f, 1.f), MatWeb, false);
	BathNoDecals(Web);

	// The two sconces over the bath on the south wall: the west one whole, the east one with its shade
	// broken back to the gallery and its bulb smashed.
	for (const float Side : { -1.f, 1.f })
	{
		const FVector At = WallPoint(EWall::South, BathSeat().X + Side * SconceSpread, SconceZ, 0.3f);
		Place(Build, Side < 0.f ? BathProps::Sconce : BathProps::SconceBroken, At, FRotator(0.f, FacingYaw(EWall::South), 0.f), {});
	}
}

// ---------------------------------------------------------------------------------------------
// The WC.
// ---------------------------------------------------------------------------------------------

void ABathroomActor::BuildToilet(FRoomBuilder& Build)
{
	// On the east wall, facing the room: a yaw of 90 turns the model's +Y to -X. Its pipe, cistern
	// and chain come with it; the roll holder on the wall a step north of it.
	const FVector WC = ToiletSeat();
	const FRotator Facing(0.f, FacingYaw(EWall::East), 0.f);
	Place(Build, BathProps::Toilet, WC, Facing, {});
	Blocker(Build, WC + Facing.RotateVector(FVector(0.f, 4.f, 22.f)), FVector(40.f, 64.f, 44.f), Facing.Yaw);
	Place(Build, BathProps::RollHolder, WallPoint(EWall::East, WC.Y - 70.f, 72.f, TileProud), Facing, {});

	// Dust on the cistern's lid, a web from the cistern to the wall's corner over it.
	Dust(Build, FVector(EastX() - 11.f, WC.Y, BathCisternTop), FVector2D(46.f, 20.f), Facing.Yaw, 0.55f);
	UStaticMeshComponent* Web = Build.Add(FRoomShapes::Plane(), FVector(EastX() - 14.f, WC.Y + 40.f, BathCisternTop + 30.f), FRotator(0.f, 20.f, 90.f), FVector(34.f, 50.f, 1.f), MatWeb, false);
	BathNoDecals(Web);
}

// ---------------------------------------------------------------------------------------------
// The rest of the furniture.
// ---------------------------------------------------------------------------------------------

void ABathroomActor::BuildFurniture(FRoomBuilder& Build)
{
	// The towel rail on the east wall north of the WC, along the wall (yaw 90 lays its bars along Y),
	// a bath towel over the top bar and a hand towel over the middle one.
	const FVector Rail = RailSeat();
	Place(Build, BathProps::TowelRail, Rail, FRotator(0.f, 90.f, 0.f), {});
	Blocker(Build, Rail + FVector(0.f, 0.f, 50.f), FVector(30.f, RailWidth + 6.f, 100.f), 0.f);
	Place(Build, BathProps::TowelBig, Rail + FVector(0.f, 4.f, BathRailBars[2]), FRotator(0.f, 90.f, 0.f), {});
	Place(Build, BathProps::TowelHand, Rail + FVector(0.f, -14.f, BathRailBars[1]), FRotator(0.f, 90.f, 0.f), {});

	// The bentwood chair by the bath's west end, turned to it, a towel folded on its seat.
	const FVector Chair = ChairSeatAt();
	const float ChairYaw = -90.f + 14.f;
	Place(Build, BathProps::Chair, Chair, FRotator(0.f, ChairYaw, 0.f), {});
	Place(Build, BathProps::TowelFolded, Chair + FVector(0.f, 0.f, ChairSeat + 0.4f), FRotator(0.f, ChairYaw + 8.f, 0.f), {});
	Blocker(Build, Chair + FVector(0.f, 0.f, 45.f), FVector(46.f, 46.f, 90.f), ChairYaw);

	// The fern by the door, dead on its stand; the hamper between the shower and the vanity.
	Place(Build, BathProps::Fern, FernSeat(), FRotator(0.f, 25.f, 0.f), {});
	Blocker(Build, FernSeat() + FVector(0.f, 0.f, 50.f), FVector(36.f, 36.f, 100.f), 0.f);
	// wicker_basket_02 comes in a pale cane that the lantern turned to a peach-coloured pot a step from
	// it: darkened to old wicker.
	FRoomShapes::TintSlots(Build.PropSeated(RoomProps::WickerBasket, HamperSeat(), FRotator(0.f, 18.f, 0.f), 46.f, false), FLinearColor(0.42f, 0.34f, 0.26f));
	Blocker(Build, HamperSeat() + FVector(0.f, 0.f, 24.f), FVector(44.f, 44.f, 48.f), 18.f);

	// The tiles that came off the west wall south of the vanity, on the floor under the gap.
	Place(Build, BathProps::TilePieces, FVector(WestX() + 26.f, FallenTilesY + 4.f, 0.f), FRotator(0.f, 12.f, 0.f), {});
}

// ---------------------------------------------------------------------------------------------
// Decay.
// ---------------------------------------------------------------------------------------------

void ABathroomActor::BuildDecay(FRoomBuilder& Build)
{
	// A bathroom shut for years without a window: the damp has had nowhere to go. Black mould in the
	// shower's corner and along the ceiling over the bath, the paint blistered and run with water from
	// the cornice down, the grout gone dark, tiles cracked and a patch of them fallen, rust run down
	// the wall under the cistern, a brown tide-mark in the bath. Every number is drawn into locals
	// before its test (09-29); every wall decal is tested against the door and what stands at the
	// wall (WallDecalHitsSomething), the lists placed to pass, the test the guard.
	enum class EKind : uint8 { Grime, Water, Mould, Crack, Rust, Gone };
	struct FMark { EWall Wall; float U; float Z; FVector2D Size; EKind Kind; float Opacity; };
	const float W = WestX();
	const float E = EastX();
	const float N = NorthY();
	const float S = SouthY();
	const FMark Marks[] = {
		// Black mould in the shower's corner, up the grout of both walls, and out across the ceiling line.
		{ EWall::West, N + 14.f, 160.f, FVector2D(46.f, 120.f), EKind::Mould, 0.55f },
		{ EWall::North, W + 14.f, 150.f, FVector2D(44.f, 110.f), EKind::Mould, 0.5f },
		{ EWall::West, N + 80.f, 60.f, FVector2D(30.f, 50.f), EKind::Mould, 0.35f },
		{ EWall::North, W + 70.f, 248.f, FVector2D(70.f, 40.f), EKind::Mould, 0.4f },
		// Over the bath, where the steam went up the south wall for years.
		{ EWall::South, W + 210.f, 250.f, FVector2D(110.f, 60.f), EKind::Mould, 0.4f },
		{ EWall::South, E - 150.f, 255.f, FVector2D(90.f, 50.f), EKind::Mould, 0.35f },
		// Low in the corners, where the damp rises.
		{ EWall::East, S - 20.f, 40.f, FVector2D(40.f, 50.f), EKind::Mould, 0.35f },
		{ EWall::South, W + 20.f, 40.f, FVector2D(40.f, 50.f), EKind::Mould, 0.35f },
		// Water down the paint from the cornice, and grime in the paint.
		{ EWall::North, E - 70.f, 220.f, FVector2D(28.f, 140.f), EKind::Water, 0.32f },
		{ EWall::East, N + 110.f, 215.f, FVector2D(26.f, 140.f), EKind::Water, 0.3f },
		{ EWall::West, S - 40.f, 210.f, FVector2D(30.f, 130.f), EKind::Water, 0.3f },
		{ EWall::South, W + 110.f, 225.f, FVector2D(28.f, 120.f), EKind::Water, 0.3f },
		{ EWall::North, W + 220.f, 240.f, FVector2D(100.f, 90.f), EKind::Grime, 0.24f },
		{ EWall::East, N + 250.f, 250.f, FVector2D(110.f, 100.f), EKind::Grime, 0.22f },
		{ EWall::West, N + 200.f, 255.f, FVector2D(100.f, 90.f), EKind::Grime, 0.22f },
		// Grime in the tiles' lower courses, where the floor's wet met the wall.
		{ EWall::South, W + 330.f, 30.f, FVector2D(120.f, 40.f), EKind::Grime, 0.3f },
		{ EWall::East, N + 160.f, 34.f, FVector2D(60.f, 40.f), EKind::Grime, 0.3f },
		{ EWall::North, E - 70.f, 36.f, FVector2D(70.f, 40.f), EKind::Grime, 0.3f },
		// Cracks through the tiles.
		{ EWall::South, W + 150.f, 90.f, FVector2D(60.f, 40.f), EKind::Crack, 0.85f },
		{ EWall::East, N + 70.f, 110.f, FVector2D(50.f, 50.f), EKind::Crack, 0.8f },
		{ EWall::West, FallenTilesY + 34.f, 74.f, FVector2D(46.f, 40.f), EKind::Crack, 0.85f },
		{ EWall::North, W + 300.f, 70.f, FVector2D(50.f, 36.f), EKind::Crack, 0.8f },
		// Rust run down the wall from the cistern's brackets.
		{ EWall::East, ToiletSeat().Y - 16.f, 150.f, FVector2D(14.f, 70.f), EKind::Rust, 0.5f },
		{ EWall::East, ToiletSeat().Y + 16.f, 156.f, FVector2D(12.f, 60.f), EKind::Rust, 0.45f },
		// The patch on the west wall where the tiles came away: the adhesive and the plaster under them.
		{ EWall::West, FallenTilesY, FallenTilesZ, FVector2D(36.f, 26.f), EKind::Gone, 0.9f },
	};
	for (const FMark& Mark : Marks)
	{
		// Water runs straight down, give or take; the rest turns any way.
		const float Jitter = Random.FRandRange(-1.f, 1.f);
		const bool bDown = Mark.Kind == EKind::Water || Mark.Kind == EKind::Rust;
		const float Roll = bDown ? Jitter * 5.f : (Mark.Kind == EKind::Gone ? Jitter * 3.f : (Jitter + 1.f) * 180.f);
		// The rust runs from the brackets, which are inside the cistern's keep-out: they skip the test.
		if (Mark.Kind != EKind::Rust && Mark.Kind != EKind::Gone && WallDecalHitsSomething(Mark.Wall, Mark.U, Mark.Z, Mark.Size, Roll))
		{
			continue;
		}
		const FRotator Into(0.f, FacingYaw(Mark.Wall) - 90.f, Roll);
		const FVector At = WallPoint(Mark.Wall, Mark.U, Mark.Z, 2.f);
		switch (Mark.Kind)
		{
		case EKind::Water:
			Build.Stain(RoomSurfaces::Paint, At, Into, Mark.Size, BathWater, Mark.Opacity, 0.7f);
			break;
		case EKind::Mould:
			Build.Stain(RoomSurfaces::Damp, At, Into, Mark.Size, BathMould, Mark.Opacity, 1.4f);
			break;
		case EKind::Crack:
			Build.Crack(At, Into, Mark.Size, Mark.Opacity, 30.f);
			break;
		case EKind::Rust:
			Build.Stain(RoomSurfaces::Paint, At, Into, Mark.Size, BathRust, Mark.Opacity, 0.6f);
			break;
		case EKind::Gone:
			Build.Stain(RoomSurfaces::Substrate, At, Into, Mark.Size, FLinearColor(0.42f, 0.38f, 0.32f), Mark.Opacity, 0.9f);
			break;
		default:
			Build.Stain(RoomSurfaces::Paint, At, Into, Mark.Size, BathFilm, Mark.Opacity, 0.5f);
			break;
		}
	}

	// The ceiling: mould spread over the shower and the bath, a water ring round the rose.
	struct FCeil { FVector2D At; FVector2D Size; bool bMould; float Opacity; };
	const FCeil Ceiling[] = {
		{ FVector2D(W + 70.f, N + 70.f), FVector2D(150.f, 130.f), true, 0.55f },
		{ FVector2D(BathSeat().X, S - 60.f), FVector2D(220.f, 110.f), true, 0.45f },
		{ FVector2D(PendantAt().X + 10.f, PendantAt().Y - 6.f), FVector2D(110.f, 100.f), false, 0.4f },
		{ FVector2D(E - 90.f, N + 120.f), FVector2D(120.f, 90.f), false, 0.3f },
	};
	for (const FCeil& Patch : Ceiling)
	{
		const float Roll = Random.FRandRange(0.f, 360.f);
		const FVector At(Patch.At.X, Patch.At.Y, RoomHeight - 3.f);
		if (Patch.bMould)
		{
			Build.Stain(RoomSurfaces::Damp, At, FRotator(90.f, 0.f, Roll), Patch.Size, BathMould, Patch.Opacity, 1.1f);
		}
		else
		{
			Build.Stain(RoomSurfaces::Paint, At, FRotator(90.f, 0.f, Roll), Patch.Size, BathWater, Patch.Opacity, 0.7f);
		}
	}

	// The floor: a film of dust in clouds, darker rings where water stood round the bath's feet and
	// the pan's foot, rust round the filler's flanges, the shower's run-off across the mosaic. Sizes
	// are (X, Y); each is tested against the door's sweep and sill by its rolled footprint.
	const FVector Bath = BathSeat();
	struct FFloor { FVector2D At; FVector2D Size; FLinearColor Tint; float Opacity; bool bDamp; };
	const FFloor Floor[] = {
		{ FVector2D(MidX() + 40.f, MidY() - 60.f), FVector2D(220.f, 160.f), BathFilm, 0.35f, false },
		{ FVector2D(W + 180.f, N + 200.f), FVector2D(160.f, 140.f), BathFilm, 0.3f, false },
		{ FVector2D(E - 150.f, S - 150.f), FVector2D(160.f, 130.f), BathFilm, 0.3f, false },
		{ FVector2D(Bath.X - 64.f, Bath.Y - 28.f), FVector2D(26.f, 22.f), BathWater, 0.45f, true },
		{ FVector2D(Bath.X + 64.f, Bath.Y - 28.f), FVector2D(26.f, 22.f), BathWater, 0.45f, true },
		{ FVector2D(Bath.X + 10.f, Bath.Y), FVector2D(140.f, 70.f), BathWater, 0.3f, true },
		{ FVector2D(FillerSeat().X, FillerSeat().Y), FVector2D(28.f, 30.f), BathRust, 0.4f, false },
		{ FVector2D(ToiletSeat().X - 4.f, ToiletSeat().Y), FVector2D(46.f, 54.f), BathWater, 0.4f, true },
		{ FVector2D(W + TrayWidth + 26.f, N + 90.f), FVector2D(70.f, 46.f), BathWater, 0.35f, true },
	};
	for (const FFloor& Patch : Floor)
	{
		const float Roll = Random.FRandRange(0.f, 360.f);
		if (FloorDecalReachesDoor(Patch.At, Patch.Size, Roll))
		{
			continue;
		}
		// Aimed down, a decal lays its first size along world Y and its second along X (09-27).
		Build.Stain(Patch.bDamp ? RoomSurfaces::Damp : RoomSurfaces::Paint, FVector(Patch.At.X, Patch.At.Y, BathFloorDecalZ), FRotator(-90.f, 0.f, Roll),
			FVector2D(Patch.Size.Y, Patch.Size.X), Patch.Tint, Patch.Opacity, Patch.bDamp ? 1.1f : 0.6f);
	}

	// In the bath: the tide-mark the water left as it went down, a brown ring round the enamel, and
	// the streak under the spout. Aimed down into it from just over the water: a decal reaches nine,
	// so the ring lies on the enamel from the water to nine over it.
	Build.Stain(RoomSurfaces::Damp, FVector(Bath.X, Bath.Y, BathWaterZ + 4.f), FRotator(-90.f, 0.f, 0.f), FVector2D(BathWidth - 18.f, BathLength - 26.f),
		FLinearColor(0.22f, 0.14f, 0.06f), 0.55f, 0.6f);
	Build.Stain(RoomSurfaces::Paint, FVector(FillerSeat().X - FillerReach - 2.f, Bath.Y, 40.f), FRotator(-90.f, 0.f, 0.f), FVector2D(14.f, 20.f),
		BathRust, 0.6f, 0.8f);
}

// ---------------------------------------------------------------------------------------------
// Clues.
// ---------------------------------------------------------------------------------------------

AClueActor* ABathroomActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
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

void ABathroomActor::BuildClues()
{
	// Set dressing only, as everywhere in the house until the game is built end to end: no text, no
	// prompt (the 09-30 decision). Three toothbrushes in a glass — his, hers, and a small pink one —
	// his razor and brush on their stand, and her rubber duck floating in the inch of water left in
	// the bath.
	const FVector Seat = VanitySeat();
	const FRotator Facing(0.f, FacingYaw(EWall::West), 0.f);
	auto OnTop = [&](float LocalX, float LocalY) { return Seat + Facing.RotateVector(FVector(LocalX, LocalY, VanityTop)); };
	if (AClueActor* Clue = SpawnClue(OnTop(40.f, -12.f), FRotator(0.f, Facing.Yaw + 20.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(BathProps::ToothGlass, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false), {});
	}
	if (AClueActor* Clue = SpawnClue(OnTop(-44.f, -14.f), FRotator(0.f, Facing.Yaw - 10.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(BathProps::ShavingSet, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false), {});
	}
	if (AClueActor* Clue = SpawnClue(OnTop(46.f, 10.f), FRotator(0.f, Facing.Yaw + 75.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(BathProps::Bottles, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false), {});
	}
	// The duck, sitting low in the water at the foot of the bath, turned to look at the door.
	const FVector Bath = BathSeat();
	if (AClueActor* Clue = SpawnClue(FVector(Bath.X + 40.f, Bath.Y - 8.f, BathWaterZ - 1.6f), FRotator(0.f, -120.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(BathProps::Duck, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false), {});
	}
}
