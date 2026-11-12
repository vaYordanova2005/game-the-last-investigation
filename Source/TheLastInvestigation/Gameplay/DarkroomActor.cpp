#include "DarkroomActor.h"
#include "CellarActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "DustMotesComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Algo/Find.h"

namespace
{
	/** The rectangles of the darkroom_paper atlas (Tools/make_darkroom_art.py: RECTS), in pixels. */
	struct FDarkroomRect
	{
		const TCHAR* Name;
		float X, Y, W, H;
	};
	const FDarkroomRect DarkroomRects[] = {
		{ TEXT("wedding"), 0.f, 0.f, 256.f, 320.f },
		{ TEXT("family"), 272.f, 0.f, 320.f, 256.f },
		{ TEXT("violin"), 608.f, 0.f, 256.f, 320.f },
		{ TEXT("house"), 880.f, 0.f, 320.f, 256.f },
		{ TEXT("dark_a"), 1216.f, 0.f, 256.f, 320.f },
		{ TEXT("dark_b"), 1488.f, 0.f, 320.f, 256.f },
		{ TEXT("road"), 0.f, 336.f, 320.f, 256.f },
		{ TEXT("contact"), 336.f, 336.f, 512.f, 640.f },
		{ TEXT("negs"), 864.f, 336.f, 1024.f, 128.f },
	};

	/** The generated models (Tools/make_darkroom.py; the shelf unit, wall shelf, stool and peg are the laundry's). */
	namespace DarkroomProps
	{
		const TCHAR* WetBench = TEXT("darkroom_wet_bench");
		const TCHAR* DryBench = TEXT("darkroom_dry_bench");
		const TCHAR* Enlarger = TEXT("darkroom_enlarger");
		const TCHAR* Pendant = TEXT("darkroom_pendant");
		const TCHAR* Cabinet = TEXT("darkroom_camera_cabinet");
		const TCHAR* CabinetDoor = TEXT("darkroom_cabinet_door");
		const TCHAR* CamBox = TEXT("darkroom_cam_box");
		const TCHAR* CamFolding = TEXT("darkroom_cam_folding");
		const TCHAR* CamTlr = TEXT("darkroom_cam_tlr");
		const TCHAR* CamRangefinder = TEXT("darkroom_cam_rangefinder");
		const TCHAR* CamSlr = TEXT("darkroom_cam_slr");
		const TCHAR* CamDslr = TEXT("darkroom_cam_dslr");
		const TCHAR* CamCompact = TEXT("darkroom_cam_compact");
		const TCHAR* SpareLens = TEXT("darkroom_spare_lens");
		const TCHAR* FilmCan = TEXT("darkroom_film_can");
		const TCHAR* FilmPot = TEXT("darkroom_film_pot");
		const TCHAR* FilmBox = TEXT("darkroom_film_box");
		const TCHAR* CineReel = TEXT("darkroom_cine_reel");
		const TCHAR* DevTank = TEXT("darkroom_dev_tank");
		const TCHAR* Tray = TEXT("darkroom_tray");
		const TCHAR* BottleDev = TEXT("darkroom_bottle_dev");
		const TCHAR* BottleStop = TEXT("darkroom_bottle_stop");
		const TCHAR* BottleFix = TEXT("darkroom_bottle_fix");
		const TCHAR* Carboy = TEXT("darkroom_carboy");
		const TCHAR* Jug = TEXT("darkroom_jug");
		const TCHAR* Timer = TEXT("darkroom_timer");
		const TCHAR* Tongs = TEXT("darkroom_tongs");
		const TCHAR* NegBox = TEXT("darkroom_neg_box");
		const TCHAR* PaperBox = TEXT("darkroom_paper_box");
		const TCHAR* Bin = TEXT("darkroom_bin");
		const TCHAR* Shelving = TEXT("laundry_shelving");
		const TCHAR* WallShelf = TEXT("laundry_wall_shelf");
		const TCHAR* Stool = TEXT("laundry_stool");
		const TCHAR* Peg = TEXT("laundry_peg");
	}

	// Photographed surfaces (Poly Haven, CC0), so nothing in the room is a flat colour that the
	// player looks at for long: the benches are weathered planks, the metal is scratched plate.
	/** Scratched metal plate: measured linear (0.091, 0.068, 0.049) (the study's); tinted to black crackle paint and to steel. */
	const FRoomSurface DarkroomMetal{ TEXT("metal_plate_02"), 100.f };
	const FLinearColor DarkroomBlackTint(0.20f, 0.22f, 0.26f);
	const FLinearColor DarkroomSteelTint(1.5f, 1.7f, 2.0f);
	/** Red leather: measured (0.084, 0.029, 0.014) (the study's); tinted to the black leatherette of old cameras. */
	const FRoomSurface DarkroomLeather{ TEXT("leather_red_02"), 40.f };
	const FLinearColor DarkroomLeatherTint(0.22f, 0.62f, 1.2f);
	/** The dust on every top, the house's colour. */
	const FLinearColor DarkroomDust(0.42f, 0.40f, 0.36f);
	/** The safelight's colour: a deep, saturated red (the user's call: the whole room red). */
	const FLinearColor DarkroomSafeColour(1.f, 0.012f, 0.004f);
	constexpr float DarkroomSafeCandelas = 900.f;

	FVector2D DarkroomFloorDecalHalfExtent(const FVector2D& Size, float Roll)
	{
		// Aimed straight down, a decal lays its first size along Y and its second along X (09-27).
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		return FVector2D(Cos * Size.Y + Sin * Size.X, Cos * Size.X + Sin * Size.Y) * 0.5f;
	}

	/** The drying line's height at X: a cotton line sags between its two eye bolts. */
	float DarkroomLineAt(float X, float HalfSpan, float Top)
	{
		const float T = X / HalfSpan;
		return Top - 7.f * (1.f - T * T);
	}
}

ADarkroomActor::ADarkroomActor()
{
	PrimaryActorTick.bCanEverTick = true;

	DarkroomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DarkroomRoot"));
	SetRootComponent(DarkroomRoot);
	DarkroomRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
	DustMotes->ConfigureVolume(FVector(HalfX - 20.f, HalfY - 20.f, Height * 0.46f), FVector(0.f, 0.f, Height * 0.5f));
}

void ADarkroomActor::BeginPlay()
{
	Super::BeginPlay();

	FlickerRandom.Initialize(7311);
	FRoomBuilder Build(this, DarkroomRoot);
	CacheMaterials(Build);
	BuildCeiling(Build);
	BuildWetBench(Build);
	BuildSafelight(Build);
	BuildDryBench(Build);
	BuildWestWall(Build);
	BuildCameraCabinet(Build);
	BuildDryingLine(Build);
	BuildDoorWall(Build);
	BuildWallDamp(Build);
	BuildFloor(Build);
	BuildDust(Build);
	BuildCobwebs(Build);
}

void ADarkroomActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!SafeLight)
	{
		return;
	}
	// An old bulb behind a filter: steady, with now and then a dip as the filament sags, and once in
	// a long while a stutter. Nothing a player would call a flicker on purpose.
	FlickerTimer -= DeltaSeconds;
	if (FlickerTimer <= 0.f)
	{
		const float Roll = FlickerRandom.FRand();
		FlickerTarget = Roll < 0.04f ? 0.25f : Roll < 0.2f ? FlickerRandom.FRandRange(0.75f, 0.9f) : FlickerRandom.FRandRange(0.95f, 1.f);
		FlickerTimer = Roll < 0.04f ? 0.06f : FlickerRandom.FRandRange(0.3f, 2.5f);
	}
	FlickerValue = FMath::FInterpTo(FlickerValue, FlickerTarget, DeltaSeconds, 18.f);
	SafeLight->SetIntensity(DarkroomSafeCandelas * FlickerValue);
	if (MatSafeGlass)
	{
		MatSafeGlass->SetScalarParameterValue(TEXT("Intensity"), 6.f * FlickerValue);
	}
}

// ---------------------------------------------------------------------------------------------
// Helpers (the laundry's).
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
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

UStaticMeshComponent* ADarkroomActor::Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
	std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	Dress(Mesh, Slots);
	return Mesh;
}

void ADarkroomActor::Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw)
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

UStaticMeshComponent* ADarkroomActor::Paper(FRoomBuilder& Build, const TCHAR* Rect, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H)
{
	// One instance per rectangle, straight from the asset and never registered with the builder, so
	// Add() does not re-tile it to the plane's size (the wine cellar's paper).
	const FName Key(Rect);
	TObjectPtr<UMaterialInstanceDynamic>* Found = PaperMats.Find(Key);
	UMaterialInstanceDynamic* Mat = Found ? Found->Get() : nullptr;
	if (!Found)
	{
		const FDarkroomRect* R = Algo::FindByPredicate(DarkroomRects, [&](const FDarkroomRect& Each) { return FCString::Strcmp(Each.Name, Rect) == 0; });
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_darkroom_paper.MI_darkroom_paper"));
		Mat = (Parent && R) ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Mat)
		{
			// Glossy prints at about a fifth of the bake: anything paler burns out under the lantern.
			Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.22f, 0.21f, 0.19f));
			Mat->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(R->W / 2048.f, R->H / 2048.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(R->X / 2048.f, R->Y / 2048.f, 0.f, 1.f));
		}
		PaperMats.Add(Key, Mat);
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

void ADarkroomActor::Web(FRoomBuilder& Build, const FVector& Centre, const FRotator& Rotation, const FVector2D& Size)
{
	if (UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Rotation, FVector(Size.X, Size.Y, 1.f), MatWeb, false))
	{
		Sheet->SetCastShadow(false);
	}
}

AClueActor* ADarkroomActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
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

void ADarkroomActor::KeepWallDecalsOff(const UPrimitiveComponent* Part)
{
	if (!Part || !Part->IsRegistered())
	{
		return;
	}
	// Translated and never turned: the room box is the world box moved. A wall decal is put two
	// centimetres off the brick and reaches nine either way from there.
	const FBox Box = Part->Bounds.GetBox().ShiftBy(-GetActorLocation());
	const float Reach = 12.f;
	const FVector2D AlongX(Box.Min.X, Box.Max.X);
	const FVector2D AlongY(Box.Min.Y, Box.Max.Y);
	const FVector2D Up(Box.Min.Z, Box.Max.Z);
	if (Box.Min.Y < -HalfY + Reach) { WallKeepOuts.Add({ EWall::North, AlongX, Up }); }
	if (Box.Max.Y > HalfY - Reach) { WallKeepOuts.Add({ EWall::South, AlongX, Up }); }
	if (Box.Max.X > HalfX - Reach) { WallKeepOuts.Add({ EWall::East, AlongY, Up }); }
	if (Box.Min.X < -HalfX + Reach) { WallKeepOuts.Add({ EWall::West, AlongY, Up }); }
}

bool ADarkroomActor::WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfAlong, float HalfUp) const
{
	// The doorway and its timber lining (the cellar's 10-05 rule).
	const float OpeningMargin = 15.f;
	if (Wall == EWall::South && FMath::Abs(U - DoorX) < DoorHalf + HalfAlong + OpeningMargin && Z - HalfUp < DoorHeight + OpeningMargin)
	{
		return true;
	}
	const float Margin = 5.f;
	for (const FWallKeepOut& Keep : WallKeepOuts)
	{
		if (Keep.Wall == Wall
			&& U + HalfAlong + Margin > Keep.U.X && U - HalfAlong - Margin < Keep.U.Y
			&& Z + HalfUp + Margin > Keep.Z.X && Z - HalfUp - Margin < Keep.Z.Y)
		{
			return true;
		}
	}
	return false;
}

bool ADarkroomActor::FloorDecalReachesDoor(const FVector2D& Point, const FVector2D& HalfExtent)
{
	const float Margin = 15.f;
	// The doorway's step and lining.
	if (FMath::Abs(Point.X - DoorX) < DoorHalf + HalfExtent.X + Margin && Point.Y + HalfExtent.Y > HalfY - Margin)
	{
		return true;
	}
	// The leaf's sweep, as ACellarActor::SpawnDoor hangs it on a north room: hinged at the east jamb
	// on the corridor's side, the leaf along -X when shut, swinging north into the room.
	const FVector2D Hinge(DoorX + DoorHalf - ACellarActor::DoorHingeInset, (Depth + WallThickness) * 0.5f - ACellarActor::DoorHingeProud);
	const float Leaf = DoorHalf * 2.f - ACellarActor::DoorLeafClearance;
	const float Overswing = Leaf * FMath::Sin(FMath::DegreesToRadians(FMath::Max(ACellarActor::DoorOpenYaw - 90.f, 0.f)));
	return FVector2D::Distance(Point, Hinge) < Leaf + HalfExtent.Size() + Margin
		&& Point.X - HalfExtent.X < Hinge.X + Overswing + Margin;
}

// ---------------------------------------------------------------------------------------------
// Materials: photographs wherever the player looks for long, flat colours only on the small things.
// ---------------------------------------------------------------------------------------------

UMaterialInstanceDynamic* ADarkroomActor::Sheet(FRoomBuilder& Build, const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale)
{
	// For a model's own UVs, already in repeats of the slot's texel size: tiling at one.
	UMaterialInstanceDynamic* Mat = Build.Surface(Set, Tint, RoughnessScale);
	if (Mat)
	{
		Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
		Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	return Mat;
}

void ADarkroomActor::CacheMaterials(FRoomBuilder& Build)
{
	// The benches: weathered planks, stained dark round the sink where the chemicals ran for years.
	MatBench = Sheet(Build, RoomSurfaces::RoughWood, FLinearColor(0.34f, 0.30f, 0.26f));
	MatShelfWood = Sheet(Build, RoomSurfaces::RoughWood, FLinearColor(0.40f, 0.37f, 0.34f));
	MatJoist = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.20f, 0.19f, 0.18f));
	// The sink's lead lining and the bin: the scratched plate gone dull grey.
	MatLead = Sheet(Build, DarkroomMetal, FLinearColor(0.85f, 1.0f, 1.2f), 1.3f);
	// The enlarger and the safelight in black crackle paint; the chrome gone to dull steel.
	MatBlackSteel = Sheet(Build, DarkroomMetal, DarkroomBlackTint, 1.1f);
	MatSteel = Sheet(Build, DarkroomMetal, DarkroomSteelTint, 0.7f);
	MatIron = Sheet(Build, RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.704f));
	// The trays' enamel: the cracked-plaster photograph's crazing read as chipped enamel (the laundry sink's).
	MatEnamel = Sheet(Build, RoomSurfaces::Plaster, FLinearColor(0.26f, 0.25f, 0.23f), 0.8f);
	MatRim = Build.Flat(FLinearColor(0.010f, 0.016f, 0.040f), 0.5f);
	// What is left in the trays: developer gone brown, a skin of crystals on the fixer. Wet, so glossy.
	MatLiquid = Build.Flat(FLinearColor(0.030f, 0.020f, 0.008f), 0.12f);
	MatLens = Build.Flat(FLinearColor(0.010f, 0.012f, 0.014f), 0.15f);
	MatPaper = Build.Flat(FLinearColor(0.11f, 0.10f, 0.09f), 0.8f);
	// The backs of the prints on the line: plain paper, a shade warmer than the picture side.
	MatPrintBack = Build.Flat(FLinearColor(0.045f, 0.042f, 0.036f), 0.75f);
	MatCard = Build.Flat(FLinearColor(0.085f, 0.068f, 0.048f), 0.9f);
	MatYellow = Build.Flat(FLinearColor(0.16f, 0.10f, 0.012f), 0.7f);
	MatAmber = Build.Flat(FLinearColor(0.020f, 0.008f, 0.003f), 0.25f);
	MatCap = Build.Flat(FLinearColor(0.012f, 0.012f, 0.012f), 0.6f);
	MatRubber = Build.Flat(FLinearColor(0.040f, 0.010f, 0.008f), 0.8f);
	MatBellows = Build.Flat(FLinearColor(0.012f, 0.011f, 0.010f), 0.85f);
	MatCord = Build.Flat(FLinearColor(0.014f, 0.012f, 0.011f), 0.8f);
	MatShadow = Build.Flat(FLinearColor(0.008f, 0.007f, 0.006f), 1.f);
	MatLine = Build.Flat(FLinearColor(0.10f, 0.09f, 0.075f), 0.95f);
	// The safelight's filter: unlit, so it reads as a lamp that is on; Tick breathes it with the bulb.
	MatSafeGlass = Build.Emissive(DarkroomSafeColour, 6.f);
	MatLeather = Sheet(Build, DarkroomLeather, DarkroomLeatherTint, 1.1f);
	MatPlastic = Build.Flat(FLinearColor(0.014f, 0.014f, 0.015f), 0.6f);
	MatSilver = Build.Flat(FLinearColor(0.28f, 0.28f, 0.29f), 0.35f, 1.f);
	MatFilm = Build.Flat(FLinearColor(0.06f, 0.025f, 0.008f), 0.3f);
	// Glass at the 10-08 values: nearly not there, or it is milk under the lantern.
	MatCaseGlass = Build.Glass(FLinearColor(0.035f, 0.035f, 0.035f), 0.06f, 0.08f);
	// The cabinet: the dark oak of the library (black_oak_veneer, rough), not cellar planks: he kept his cameras well.
	MatCabinet = Sheet(Build, RoomSurfaces::DarkOak, FLinearColor(0.16f, 0.12f, 0.095f), 2.2f);
	if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_darkroom_paper.MI_darkroom_paper")))
	{
		// The bottles' labels and the timer's dial on the models' own UVs, which address the atlas.
		MatLabel = UMaterialInstanceDynamic::Create(Parent, this);
		MatLabel->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.24f, 0.22f, 0.19f));
		MatLabel->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
		MatLabel->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
		MatLabel->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	else
	{
		MatLabel = Build.Flat(FLinearColor(0.15f, 0.13f, 0.10f), 0.9f);
	}
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
}

// ---------------------------------------------------------------------------------------------
// The ceiling's joists: the cellar's shell is a slab of boards; a cellar shows its joists.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::BuildCeiling(FRoomBuilder& Build)
{
	for (float X = -HalfX + 30.f; X < HalfX - 10.f; X += 52.f)
	{
		Build.Box(FVector(X, 0.f, Height - 9.f), FRotator::ZeroRotator, FVector(10.f, HalfY * 2.f, 18.f), MatJoist, /*bBlockingCollision*/ false);
	}
}

// ---------------------------------------------------------------------------------------------
// The wet bench: the sink, the three trays, the chemicals, the carboys.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::BuildWetBench(FRoomBuilder& Build)
{
	const FVector Bench = WetBench();
	KeepWallDecalsOff(Place(Build, DarkroomProps::WetBench, Bench, FRotator::ZeroRotator,
		{ { TEXT("Wood"), MatBench }, { TEXT("Lead"), MatLead }, { TEXT("Shadow"), MatShadow }, { TEXT("Chrome"), MatSteel } }));
	Blocker(Build, Bench + FVector(0.f, 0.f, WetTop * 0.5f), FVector(WetLength, WetDepth, WetTop));

	// Developer, stop and fixer, left to right as they are worked: on the sink's floor, the tongs laid
	// across between the first two, a jug at the far end.
	const float TrayY = Bench.Y + 3.f;
	const float TrayXs[3] = { Bench.X - 78.f, Bench.X - 36.f, Bench.X + 6.f };
	for (int32 i = 0; i < 3; ++i)
	{
		Place(Build, DarkroomProps::Tray, FVector(TrayXs[i], TrayY, SinkFloor), FRotator(0.f, i == 1 ? 3.f : -2.f + i, 0.f),
			{ { TEXT("Enamel"), MatEnamel }, { TEXT("Rim"), MatRim }, { TEXT("Liquid"), MatLiquid } });
	}
	Place(Build, DarkroomProps::Tongs, FVector((TrayXs[0] + TrayXs[1]) * 0.5f, TrayY + 2.f, SinkFloor + 6.4f), FRotator(0.f, 70.f, 0.f),
		{ { TEXT("Wood"), MatBench }, { TEXT("Rubber"), MatRubber } });
	Place(Build, DarkroomProps::Jug, FVector(Bench.X + 62.f, TrayY - 6.f, SinkFloor), FRotator(0.f, 140.f, 0.f),
		{ { TEXT("Chrome"), MatSteel }, { TEXT("Liquid"), MatLiquid } });

	// The print in the fixer: pulled out of the developer too soon and never taken out. A wet road
	// at night, two lamps. Set dressing only (the 09-30 decision): a clue marker, no text.
	const FVector RoadAt(TrayXs[2], TrayY, SinkFloor + 2.42f);
	Paper(Build, TEXT("road"), RoadAt, FVector::UpVector, FVector(-0.06f, -1.f, 0.f), 20.f, 16.f);
	SpawnClue(RoadAt, FRotator::ZeroRotator);

	// The chemicals on the shelf over the bench, and a box of paper.
	const FVector Shelf = WallShelf();
	KeepWallDecalsOff(Place(Build, DarkroomProps::WallShelf, Shelf, FRotator::ZeroRotator, { { TEXT("Wood"), MatShelfWood }, { TEXT("Iron"), MatIron } }));
	const float ShelfY = Shelf.Y + WallShelfDepth * 0.5f;
	const TCHAR* Bottles[5] = { DarkroomProps::BottleDev, DarkroomProps::BottleStop, DarkroomProps::BottleFix, DarkroomProps::BottleFix, DarkroomProps::BottleDev };
	const float BottleXs[5] = { -62.f, -48.f, -34.f, -18.f, 6.f };
	const float BottleYaws[5] = { 6.f, -10.f, 4.f, 30.f, -18.f };
	for (int32 i = 0; i < 5; ++i)
	{
		KeepWallDecalsOff(Place(Build, Bottles[i], FVector(Shelf.X + BottleXs[i], ShelfY + (i == 3 ? 3.f : 0.f), Shelf.Z), FRotator(0.f, BottleYaws[i], 0.f),
			{ { TEXT("Bottle"), MatAmber }, { TEXT("Cap"), MatCap }, { TEXT("Label"), MatLabel } }));
	}
	KeepWallDecalsOff(Place(Build, DarkroomProps::PaperBox, FVector(Shelf.X + 44.f, ShelfY, Shelf.Z), FRotator(0.f, -4.f, 0.f),
		{ { TEXT("Yellow"), MatYellow }, { TEXT("Black"), MatCap } }));

	// Stock solution in carboys on the slats under the bench.
	for (const float X : { Bench.X - 70.f, Bench.X + 30.f })
	{
		Place(Build, DarkroomProps::Carboy, FVector(X, Bench.Y + 2.f, 18.5f), FRotator(0.f, X * 3.f, 0.f), { { TEXT("Bottle"), MatAmber }, { TEXT("Cap"), MatCap } });
	}
}

// ---------------------------------------------------------------------------------------------
// The safelight: a red lamp on the wall over the sink, still burning.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::BuildSafelight(FRoomBuilder& Build)
{
	// Hung from the middle joist on its flex, above the line and above a man's head.
	const FVector Rose = Pendant();
	Place(Build, DarkroomProps::Pendant, Rose, FRotator::ZeroRotator,
		{ { TEXT("Black"), MatBlackSteel }, { TEXT("SafeGlass"), MatSafeGlass }, { TEXT("Cord"), MatCord } });

	// The bulb in the bowl. Saturated red and strong enough to fill the room: the lantern's gold is
	// the only other colour down here. Shadowed, so it stays inside these walls; it reaches the
	// corridor only through the crack the door stands open by.
	SafeLight = NewObject<UPointLightComponent>(this, TEXT("SafeLight"));
	if (SafeLight)
	{
		SafeLight->SetMobility(EComponentMobility::Movable);
		SafeLight->AttachToComponent(DarkroomRoot, FAttachmentTransformRules::KeepRelativeTransform);
		SafeLight->SetRelativeLocation(Rose - FVector(0.f, 0.f, PendantDrop + 3.f));
		SafeLight->SetIntensityUnits(ELightUnits::Candelas);
		SafeLight->SetIntensity(DarkroomSafeCandelas);
		SafeLight->SetLightColor(DarkroomSafeColour);
		SafeLight->SetAttenuationRadius(900.f);
		SafeLight->SetSourceRadius(9.f);
		SafeLight->SetCastShadows(true);
		SafeLight->SetVolumetricScatteringIntensity(1.4f);
		SafeLight->RegisterComponent();
		AddInstanceComponent(SafeLight);
	}
}

// ---------------------------------------------------------------------------------------------
// The camera cabinet: his cameras from the oldest to the newest, and his film.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::BuildCameraCabinet(FRoomBuilder& Build)
{
	const FVector At = Cabinet();
	KeepWallDecalsOff(Place(Build, DarkroomProps::Cabinet, At, FRotator(0.f, 180.f, 0.f),
		{ { TEXT("Wood"), MatCabinet }, { TEXT("Glass"), MatCaseGlass }, { TEXT("Chrome"), MatSteel } }));
	Blocker(Build, At + FVector(0.f, 0.f, CabinetHeight * 0.5f), FVector(CabinetWidth, CabinetDepth, CabinetHeight));
	// The right glazed door (as he stood in front of it) open, hung on its hinge at the cabinet's
	// end nearer the room's door. Opened by a negative yaw in the cabinet's frame.
	const float HalfW = CabinetWidth * 0.5f;
	const float HalfD = CabinetDepth * 0.5f;
	Place(Build, DarkroomProps::CabinetDoor, InCabinet(FVector(HalfW - 2.f, HalfD - 2.1f, CabinetShelves[0])), FRotator(0.f, 180.f - 78.f, 0.f),
		{ { TEXT("Wood"), MatCabinet }, { TEXT("Glass"), MatCaseGlass }, { TEXT("Chrome"), MatSteel } });

	// Each thing in the cabinet's frame: X along it (-X behind the shut door), Y out of it, Z its shelf.
	struct FItem { const TCHAR* Model; float X; float Y; int32 Shelf; float Yaw; float Lift; };
	const FItem Items[] = {
		// The bottom of the glazed case: the old cameras.
		{ DarkroomProps::CamBox, -38.f, -4.f, 0, 8.f, 0.f },
		{ DarkroomProps::CamFolding, -14.f, -6.f, 0, -6.f, 0.f },
		{ DarkroomProps::CamTlr, 12.f, -3.f, 0, 12.f, 0.f },
		{ DarkroomProps::CamRangefinder, 36.f, -1.f, 0, -14.f, 0.f },
		// The middle shelf: the later ones.
		{ DarkroomProps::CamSlr, -34.f, -3.f, 1, 6.f, 0.f },
		{ DarkroomProps::SpareLens, -16.f, -6.f, 1, 0.f, 0.f },
		{ DarkroomProps::CamDslr, 8.f, -4.f, 1, -10.f, 0.f },
		{ DarkroomProps::CamCompact, 34.f, 0.f, 1, 18.f, 0.f },
		// The top shelf: film.
		{ DarkroomProps::FilmBox, -44.f, -2.f, 2, 4.f, 0.f },
		{ DarkroomProps::FilmBox, -39.f, -3.f, 2, -3.f, 0.f },
		{ DarkroomProps::FilmBox, -34.f, -1.f, 2, 9.f, 0.f },
		{ DarkroomProps::FilmBox, -41.f, -9.f, 2, 1.f, 0.f },
		{ DarkroomProps::FilmCan, -20.f, 2.f, 2, 70.f, 0.f },
		{ DarkroomProps::FilmCan, -18.f, -6.f, 2, 20.f, 0.f },
		{ DarkroomProps::FilmPot, -6.f, -2.f, 2, 0.f, 0.f },
		{ DarkroomProps::FilmPot, -1.f, -6.f, 2, 0.f, 0.f },
		{ DarkroomProps::FilmPot, 3.f, 0.f, 2, 0.f, 0.f },
		{ DarkroomProps::CineReel, 30.f, -4.f, 2, 0.f, 0.f },
		{ DarkroomProps::CineReel, 31.f, -3.f, 2, 37.f, 1.45f },
		{ DarkroomProps::CineReel, 29.f, -5.f, 2, 71.f, 2.9f },
	};
	for (const FItem& Item : Items)
	{
		Place(Build, Item.Model, InCabinet(FVector(Item.X, Item.Y, CabinetShelves[Item.Shelf] + Item.Lift)), FRotator(0.f, 180.f + Item.Yaw, 0.f),
			{ { TEXT("Leather"), MatLeather }, { TEXT("Black"), MatBlackSteel }, { TEXT("Chrome"), MatSteel }, { TEXT("Lens"), MatLens },
			  { TEXT("Bellows"), MatBellows }, { TEXT("Plastic"), MatPlastic }, { TEXT("Silver"), MatSilver }, { TEXT("Rubber"), MatRubber },
			  { TEXT("Label"), MatLabel }, { TEXT("Film"), MatFilm }, { TEXT("Yellow"), MatYellow } });
	}
	// Two cine reels in their cans on top of the cabinet, and a cassette fallen in front of it.
	Place(Build, DarkroomProps::CineReel, InCabinet(FVector(-20.f, -2.f, CabinetHeight + 5.f)), FRotator(0.f, 12.f, 0.f), { { TEXT("Silver"), MatSilver }, { TEXT("Film"), MatFilm } });
	Place(Build, DarkroomProps::CineReel, InCabinet(FVector(-19.f, -1.f, CabinetHeight + 6.45f)), FRotator(0.f, 50.f, 0.f), { { TEXT("Silver"), MatSilver }, { TEXT("Film"), MatFilm } });
	Place(Build, DarkroomProps::FilmCan, InCabinet(FVector(10.f, HalfD + 26.f, 0.f)), FRotator(0.f, 133.f, 0.f),
		{ { TEXT("Label"), MatLabel }, { TEXT("Black"), MatBlackSteel }, { TEXT("Film"), MatFilm } });
}

// ---------------------------------------------------------------------------------------------
// The dry bench: the enlarger, the timer, the paper; the stool.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::BuildDryBench(FRoomBuilder& Build)
{
	const FVector Bench = DryBench();
	// Its front faces the room (west): yaw 90 turns +Y to -X.
	KeepWallDecalsOff(Place(Build, DarkroomProps::DryBench, Bench, FRotator(0.f, 90.f, 0.f), { { TEXT("Wood"), MatBench }, { TEXT("Chrome"), MatSteel } }));
	Blocker(Build, Bench + FVector(0.f, 0.f, DryTop * 0.5f), FVector(DryDepth, DryLength, DryTop));

	KeepWallDecalsOff(Place(Build, DarkroomProps::Enlarger, FVector(Bench.X + 2.f, Bench.Y - 30.f, DryTop), FRotator(0.f, 90.f, 0.f),
		{ { TEXT("Wood"), MatBench }, { TEXT("Black"), MatBlackSteel }, { TEXT("Bellows"), MatBellows }, { TEXT("Chrome"), MatSteel },
		  { TEXT("Lens"), MatLens }, { TEXT("Cord"), MatCord }, { TEXT("Enamel"), MatEnamel } }));
	Place(Build, DarkroomProps::Timer, FVector(Bench.X + 10.f, Bench.Y + 26.f, DryTop), FRotator(0.f, 104.f, 0.f),
		{ { TEXT("Black"), MatBlackSteel }, { TEXT("Label"), MatLabel }, { TEXT("Rubber"), MatRubber }, { TEXT("Chrome"), MatSteel } });
	Place(Build, DarkroomProps::PaperBox, FVector(Bench.X - 6.f, Bench.Y - 64.f, DryTop), FRotator(0.f, 81.f, 0.f),
		{ { TEXT("Yellow"), MatYellow }, { TEXT("Black"), MatCap } });
	Place(Build, DarkroomProps::DevTank, FVector(Bench.X + 12.f, Bench.Y + 50.f, DryTop), FRotator(0.f, 30.f, 0.f), { { TEXT("Chrome"), MatSteel } });
	Place(Build, DarkroomProps::FilmCan, FVector(Bench.X - 20.f, Bench.Y + 30.f, DryTop), FRotator(0.f, 64.f, 0.f),
		{ { TEXT("Label"), MatLabel }, { TEXT("Black"), MatBlackSteel }, { TEXT("Film"), MatFilm } });
	// A test strip on the board, the last one he made: one of the dark prints, cut narrow.
	Paper(Build, TEXT("dark_a"), FVector(Bench.X - 8.f, Bench.Y - 26.f, DryTop + 4.3f), FVector::UpVector, FVector(1.f, 0.f, 0.f), 6.f, 18.f);

	Place(Build, DarkroomProps::Stool, FVector(Bench.X - 72.f, Bench.Y - 18.f, 0.f), FRotator(0.f, 20.f, 0.f), { { TEXT("Wood"), MatBench } });
	Blocker(Build, FVector(Bench.X - 72.f, Bench.Y - 18.f, 23.f), FVector(34.f, 34.f, 46.f));
}

// ---------------------------------------------------------------------------------------------
// The west wall: a shelf unit of negatives filed by year in shoeboxes.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::BuildWestWall(FRoomBuilder& Build)
{
	const FVector At = Shelving();
	// Its front faces east: yaw -90 turns +Y to +X.
	KeepWallDecalsOff(Place(Build, DarkroomProps::Shelving, At, FRotator(0.f, -90.f, 0.f), { { TEXT("Wood"), MatShelfWood }, { TEXT("Iron"), MatIron } }));
	Blocker(Build, At + FVector(0.f, 0.f, 95.f), FVector(ShelvesDepth, ShelvesWidth, 190.f));
	// Boards at 6, 50, 94, 138, 182 (their middles; 2.2 thick).
	struct FShelfBox { float Board; float Y; float Yaw; const TCHAR* Model; };
	const FShelfBox Boxes[] = {
		{ 50.f, -26.f, -88.f, DarkroomProps::NegBox }, { 50.f, 18.f, -94.f, DarkroomProps::NegBox },
		{ 94.f, -24.f, -91.f, DarkroomProps::NegBox }, { 94.f, 22.f, -86.f, DarkroomProps::NegBox },
		{ 138.f, -20.f, -92.f, DarkroomProps::NegBox },
		{ 138.f, 26.f, -80.f, DarkroomProps::PaperBox }, { 182.f, -10.f, -96.f, DarkroomProps::PaperBox },
	};
	for (const FShelfBox& Each : Boxes)
	{
		const bool bNeg = Each.Model == DarkroomProps::NegBox;
		Place(Build, Each.Model, FVector(At.X + 2.f, At.Y + Each.Y, Each.Board + 1.1f), FRotator(0.f, Each.Yaw, 0.f),
			bNeg ? std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>>{ { TEXT("Card"), MatCard }, { TEXT("Paper"), MatPaper } }
				 : std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>>{ { TEXT("Yellow"), MatYellow }, { TEXT("Black"), MatCap } });
	}
	// The last box is the one with nothing in its last divider: the year that was never filed.
	Place(Build, DarkroomProps::NegBox, FVector(At.X + 4.f, At.Y + 30.f, 6.f + 1.1f), FRotator(0.f, -70.f, 0.f), { { TEXT("Card"), MatCard }, { TEXT("Paper"), MatPaper } });
	// A strip of negatives on the floor in front, dropped.
	Paper(Build, TEXT("negs"), FVector(At.X + 48.f, At.Y + 40.f, 0.6f), FVector::UpVector, FVector(0.42f, 0.9f, 0.f).GetSafeNormal(), 24.f, 3.f);
}

// ---------------------------------------------------------------------------------------------
// The drying line: the prints pegged on it, facing the door.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::HangPrint(FRoomBuilder& Build, const TCHAR* Rect, float X, float W, float H, float Swing)
{
	const float LineZ0 = DarkroomLineAt(X, HalfX, LineZ);
	Place(Build, DarkroomProps::Peg, FVector(X, LineY, LineZ0 - 1.6f), FRotator(90.f, 90.f, 0.f), { { TEXT("Wood"), MatShelfWood }, { TEXT("Iron"), MatIron } });
	// Turned a few degrees about the peg, the way a print hangs once it has curled drying.
	const FVector Normal = FRotator(0.f, 90.f + Swing, 0.f).Vector();
	const FVector Centre(X, LineY, LineZ0 - 4.f - H * 0.5f);
	Paper(Build, Rect, Centre + Normal * 0.15f, Normal, FVector::UpVector, W, H);
	if (UStaticMeshComponent* Back = Build.Add(FRoomShapes::Plane(), Centre - Normal * 0.15f, FRotationMatrix::MakeFromZ(-Normal).Rotator(), FVector(W, H, 1.f), MatPrintBack, false))
	{
		Back->SetCastShadow(false);
		Back->SetReceivesDecals(false);
	}
}

void ADarkroomActor::BuildDryingLine(FRoomBuilder& Build)
{
	// A cotton line between two eye bolts, sagging a hand's width in the middle.
	const int32 Segments = 16;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float X0 = -HalfX + 2.f + (HalfX * 2.f - 4.f) * i / Segments;
		const float X1 = -HalfX + 2.f + (HalfX * 2.f - 4.f) * (i + 1) / Segments;
		const FVector A(X0, LineY, DarkroomLineAt(X0, HalfX, LineZ));
		const FVector B(X1, LineY, DarkroomLineAt(X1, HalfX, LineZ));
		if (UStaticMeshComponent* Part = Build.Cyl((A + B) * 0.5f, FRotationMatrix::MakeFromZ(B - A).Rotator(), FVector(0.4f, 0.4f, (B - A).Size()), MatLine, false))
		{
			Part->SetCastShadow(true);
		}
	}
	for (const float Side : { -1.f, 1.f })
	{
		Build.Cyl(FVector(Side * (HalfX - 2.f), LineY, LineZ), FRotator(0.f, 0.f, 90.f), FVector(1.2f, 1.2f, 5.f), MatIron, false);
	}

	// The prints, as he hung them: the family's pictures, and the ones that went wrong. Clear of the
	// eye's way through the middle of the room, and the dark ones where the red light falls.
	HangPrint(Build, TEXT("wedding"), -232.f, 16.f, 20.f, -6.f);
	HangPrint(Build, TEXT("dark_b"), -186.f, 20.f, 16.f, 4.f);
	HangPrint(Build, TEXT("family"), -128.f, 20.f, 16.f, -3.f);
	HangPrint(Build, TEXT("negs"), -86.f, 3.f, 24.f, 8.f);
	HangPrint(Build, TEXT("violin"), -34.f, 16.f, 20.f, 5.f);
	HangPrint(Build, TEXT("dark_a"), 28.f, 16.f, 20.f, -8.f);
	HangPrint(Build, TEXT("house"), 84.f, 20.f, 16.f, 2.f);
	HangPrint(Build, TEXT("negs"), 128.f, 3.f, 24.f, -5.f);
	HangPrint(Build, TEXT("dark_a"), 176.f, 16.f, 20.f, 7.f);
	// Two pegs with nothing on them, and one print fallen to the floor under the line, face down.
	for (const float X : { 224.f, 240.f })
	{
		Place(Build, DarkroomProps::Peg, FVector(X, LineY, DarkroomLineAt(X, HalfX, LineZ) - 1.6f), FRotator(90.f, 90.f, 0.f), { { TEXT("Wood"), MatShelfWood }, { TEXT("Iron"), MatIron } });
	}
	if (UStaticMeshComponent* Fallen = Build.Add(FRoomShapes::Plane(), FVector(214.f, LineY + 22.f, 0.5f), FRotator(0.f, 23.f, 0.f), FVector(20.f, 16.f, 1.f), MatPrintBack, false))
	{
		Fallen->SetCastShadow(false);
	}
	// The wedding and the violin are the family's own prints: markers for the clues to come.
	SpawnClue(FVector(-232.f, LineY, LineZ - 20.f), FRotator::ZeroRotator);
	SpawnClue(FVector(28.f, LineY, LineZ - 20.f), FRotator::ZeroRotator);
}

// ---------------------------------------------------------------------------------------------
// The door wall: a contact sheet pinned up east of the door, the bin under it.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::BuildDoorWall(FRoomBuilder& Build)
{
	const FVector Contact(112.f, HalfY - 1.2f, 150.f);
	if (UStaticMeshComponent* Sheet = Paper(Build, TEXT("contact"), Contact, FVector(0.f, -1.f, 0.f), FVector::UpVector, 20.f, 25.f))
	{
		KeepWallDecalsOff(Sheet);
	}
	// A pin at each top corner.
	for (const float Side : { -1.f, 1.f })
	{
		KeepWallDecalsOff(Build.Sph(Contact + FVector(Side * 8.5f, -0.6f, 11.f), 0.9f, MatIron));
	}
	if (UStaticMeshComponent* House = Paper(Build, TEXT("house"), FVector(150.f, HalfY - 1.2f, 146.f), FVector(0.f, -1.f, 0.f), FVector(0.05f, 0.f, 1.f), 20.f, 16.f))
	{
		KeepWallDecalsOff(House);
	}
	SpawnClue(Contact, FRotator::ZeroRotator);

	Place(Build, DarkroomProps::Bin, FVector(230.f, HalfY - 34.f, 0.f), FRotator(0.f, 200.f, 0.f), { { TEXT("Lead"), MatLead }, { TEXT("Paper"), MatPaper } });
	Blocker(Build, FVector(230.f, HalfY - 34.f, 17.f), FVector(30.f, 30.f, 34.f));
}

// ---------------------------------------------------------------------------------------------
// Damp on the brick, chemical stains on the flags, dust, cobwebs.
// ---------------------------------------------------------------------------------------------

void ADarkroomActor::BuildWallDamp(FRoomBuilder& Build)
{
	// Every decal tested against what stands against its wall, every number drawn first and in order,
	// the skipped ones' too (09-29).
	FRandomStream Random(6101);
	const FLinearColor Damp(0.40f, 0.34f, 0.27f);
	const FLinearColor Mould(0.10f, 0.12f, 0.08f);
	struct FFace { EWall Wall; FVector Normal; FVector Along; float Half; };
	const FFace Faces[] = {
		{ EWall::North, FVector(0.f, -1.f, 0.f), FVector(1.f, 0.f, 0.f), HalfX },
		{ EWall::South, FVector(0.f, 1.f, 0.f), FVector(1.f, 0.f, 0.f), HalfX },
		{ EWall::East, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), HalfY },
		{ EWall::West, FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), HalfY },
	};
	for (const FFace& Face : Faces)
	{
		const float Plane = (FMath::Abs(Face.Normal.X) > 0.5f ? HalfX : HalfY) - 2.f;
		const FRotator Aim = Face.Normal.Rotation();
		for (int32 i = 0; i < 5; ++i)
		{
			const float U = Random.FRandRange(-Face.Half + 30.f, Face.Half - 30.f);
			const float H = Random.FRandRange(40.f, 120.f);
			const float W = Random.FRandRange(70.f, 150.f);
			const float Opacity = Random.FRandRange(0.45f, 0.75f);
			const bool bMould = Random.FRand() < 0.3f;
			const bool bCrack = Random.FRand() < 0.4f;
			const float CrackW = Random.FRandRange(50.f, 120.f);
			const float CrackH = Random.FRandRange(70.f, 150.f);
			const float CrackZ = Random.FRandRange(90.f, 240.f);
			const float CrackOpacity = Random.FRandRange(0.5f, 0.85f);
			const float Sharpness = Random.FRandRange(16.f, 26.f);
			const float Z = H * 0.4f;
			// The decal turns freely about its normal only by these sizes; it is not rolled, so its
			// box is its reach.
			if (!WallDecalHitsSomething(Face.Wall, U, Z, W * 0.5f, H * 0.5f))
			{
				Build.Stain(RoomSurfaces::Damp, Face.Normal * Plane + Face.Along * U + FVector(0.f, 0.f, Z), Aim, FVector2D(W, H),
					bMould ? Mould : Damp * 0.8f, bMould ? Opacity * 0.7f : Opacity, 1.2f);
			}
			if (bCrack && !WallDecalHitsSomething(Face.Wall, U, CrackZ, CrackW * 0.5f, CrackH * 0.5f))
			{
				Build.Crack(Face.Normal * Plane + Face.Along * U + FVector(0.f, 0.f, CrackZ), Aim, FVector2D(CrackW, CrackH), CrackOpacity, Sharpness);
			}
		}
	}
	// Brown runs down the brick under the bench's tap end, where the splashback stopped short.
	if (!WallDecalHitsSomething(EWall::North, WetBench().X + WetLength * 0.5f + 26.f, 70.f, 14.f, 40.f))
	{
		Build.Stain(RoomSurfaces::Damp, FVector(WetBench().X + WetLength * 0.5f + 26.f, -HalfY + 2.f, 70.f), FRotator(0.f, -90.f, 0.f),
			FVector2D(28.f, 80.f), FLinearColor(0.20f, 0.13f, 0.06f), 0.7f, 1.2f);
	}
}

void ADarkroomActor::BuildFloor(FRoomBuilder& Build)
{
	FRandomStream Random(6102);
	const FLinearColor Chemical(0.16f, 0.10f, 0.045f);
	const FLinearColor Damp(0.30f, 0.26f, 0.21f);
	const FLinearColor Dirt(0.20f, 0.17f, 0.13f);
	auto FloorStain = [&](const FRoomSurface& Set, const FVector2D& At, const FVector2D& Size, float Roll, const FLinearColor& Tint, float Opacity, float Edge, float Rough)
	{
		if (!FloorDecalReachesDoor(At, DarkroomFloorDecalHalfExtent(Size, Roll)))
		{
			Build.Stain(Set, FVector(At.X, At.Y, 4.f), FRotator(-90.f, 0.f, Roll), Size, Tint, Opacity, Edge, Rough);
		}
	};
	// Developer splashed and dried in front of the sink, years of it: brown blooms with tide-marks.
	FloorStain(RoomSurfaces::Damp, FVector2D(WetBench().X - 30.f, WetBench().Y + 60.f), FVector2D(70.f, 150.f), 8.f, Chemical, 0.7f, 1.2f, 1.f);
	FloorStain(RoomSurfaces::Damp, FVector2D(WetBench().X + 40.f, WetBench().Y + 52.f), FVector2D(50.f, 70.f), 40.f, Chemical, 0.55f, 1.2f, 1.f);
	// Something that stood here and is still wet: the one damp spot, glossy, under the tap end.
	FloorStain(RoomSurfaces::Floorboards, FVector2D(WetBench().X + 100.f, WetBench().Y + 48.f), FVector2D(40.f, 56.f), 15.f, FLinearColor(0.12f, 0.12f, 0.12f), 0.75f, 1.2f, 0.1f);
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = Random.FRandRange(-HalfX + 40.f, HalfX - 40.f);
		const float Y = Random.FRandRange(-HalfY + 40.f, HalfY - 40.f);
		const float SizeX = Random.FRandRange(70.f, 150.f);
		const float SizeY = Random.FRandRange(50.f, 120.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float Opacity = Random.FRandRange(0.3f, 0.5f);
		FloorStain(RoomSurfaces::Damp, FVector2D(X, Y), FVector2D(SizeX, SizeY), Roll, Damp, Opacity, 1.3f, 1.f);
	}
	for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f) })
	{
		FloorStain(RoomSurfaces::Damp, FVector2D(Corner.X * (HalfX - 26.f), Corner.Y * (HalfY - 26.f)), FVector2D(90.f, 90.f), 45.f, Dirt, 0.65f, 1.2f, 1.f);
	}
}

void ADarkroomActor::BuildDust(FRoomBuilder& Build)
{
	auto Dust = [&](const FVector& Top, const FVector2D& Size, float Opacity)
	{
		Build.Stain(RoomSurfaces::Damp, Top + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, 0.f), Size, DarkroomDust, Opacity, 1.5f);
	};
	// Aimed down a decal lays its first size along Y (09-27): sizes are (along Y, along X).
	Dust(DryBench() + FVector(0.f, 0.f, DryTop), FVector2D(DryLength, DryDepth), 0.4f);
	Dust(WallShelf() + FVector(0.f, WallShelfDepth * 0.5f, 0.f), FVector2D(WallShelfDepth, WallShelfLength), 0.4f);
	Dust(Shelving() + FVector(0.f, 0.f, 183.f), FVector2D(ShelvesWidth, ShelvesDepth), 0.45f);
}

void ADarkroomActor::BuildCobwebs(FRoomBuilder& Build)
{
	auto Upright = [](float Yaw) { return FRotator(0.f, Yaw, 90.f); };
	for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f) })
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const float Inset = 22.f + i * 20.f;
			Web(Build, FVector(Corner.X * (HalfX - Inset), Corner.Y * (HalfY - Inset), Height - 22.f - i * 6.f),
				FRotator(0.f, Corner.X * Corner.Y > 0.f ? 45.f : -45.f, 180.f), FVector2D(Inset * 1.6f, Inset * 1.6f));
		}
	}
	// Between the safelight's flex and the wall, under the dry bench, between the line's end and the corner.
	Web(Build, Cabinet() + FVector(0.f, -2.f, CabinetHeight + 18.f), Upright(0.f), FVector2D(60.f, 26.f));
	Web(Build, DryBench() + FVector(0.f, -DryLength * 0.5f + 8.f, 40.f), Upright(0.f), FVector2D(DryDepth - 12.f, 30.f));
	Web(Build, FVector(HalfX - 12.f, LineY - 10.f, LineZ + 20.f), Upright(90.f), FVector2D(22.f, 40.f));
}
