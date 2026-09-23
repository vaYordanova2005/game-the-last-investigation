#include "InvestigationRoomActor.h"
#include "DoorActor.h"
#include "KeyPickupActor.h"
#include "StormWindowActor.h"
#include "RoomDressingActor.h"
#include "CorridorActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Scene.h"
#include "Engine/World.h"

AInvestigationRoomActor::AInvestigationRoomActor()
{
	PrimaryActorTick.bCanEverTick = false; // the storm owns the lightning now; nothing here animates

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeMeshFinder.Object;

	FogComponent = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("FogComponent"));
	FogComponent->SetupAttachment(RoomRoot);
	FogComponent->FogDensity = 0.055f;
	FogComponent->FogHeightFalloff = 0.15f;
	FogComponent->FogInscatteringLuminance = FLinearColor(0.012f, 0.016f, 0.022f); // cold, unlit air
	FogComponent->SetRelativeLocation(FVector(0.f, 0.f, -5.f));

	// Volumetric fog is what turns the lantern into a visible cone of light and lets each
	// lightning flash come through the window as a shaft rather than a flat wash on the wall.
	FogComponent->bEnableVolumetricFog = true;
	FogComponent->VolumetricFogScatteringDistribution = 0.35f; // mildly forward-scattering, like dusty air
	FogComponent->VolumetricFogAlbedo = FColor(180, 176, 168);
	FogComponent->VolumetricFogExtinctionScale = 2.2f;
	FogComponent->VolumetricFogDistance = 3500.f;

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(RoomRoot);
	PostProcess->bUnbound = true;

	FPostProcessSettings& PP = PostProcess->Settings;

	// Manual exposure. Auto-exposure is actively hostile to this scene: it would brighten the dark
	// corners back up the moment the player looked into one, which is precisely the tension the
	// brief is built on, and it would then blow out the whole frame on every lightning flash.
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;

	// In manual mode the exposure comes from the camera settings, so they are set explicitly
	// rather than left at their daylight defaults and then dragged into range with a huge EV
	// bias. Fast film, a slow shutter and a wide aperture — how you would actually photograph a
	// room lit by one flame. EV100 works out around 3.6, which puts a lantern-lit wall a little
	// under middle grey: dark, but readable. Together with AStormWindowActor::StormAmbientLux these
	// are the only two numbers that decide how dark the room is — turn those, not the materials.
	PP.bOverride_CameraISO = true;
	PP.CameraISO = 800.f;
	PP.bOverride_CameraShutterSpeed = true;
	PP.CameraShutterSpeed = 30.f; // 1/30s
	PP.bOverride_DepthOfFieldFstop = true;
	PP.DepthOfFieldFstop = 1.8f;
	// The trim on top of the camera settings. The photographed surfaces sit around 0.2 albedo and
	// the only light is a 200cd flame, so without a positive trim the frame is genuinely black —
	// not atmospheric, black. This is the one dial to turn if the room reads too dark or too light.
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 0.3f;

	// Lumen, explicitly, so the room does not depend on a project-setting default. The single
	// bounce off a lantern-lit floorboard is most of what keeps the darkness readable instead of
	// pure black.
	PP.bOverride_DynamicGlobalIlluminationMethod = true;
	PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	PP.bOverride_ReflectionMethod = true;
	PP.ReflectionMethod = EReflectionMethod::Lumen;
	PP.bOverride_LumenFinalGatherQuality = true;
	PP.LumenFinalGatherQuality = 2.f;

	// Colour grade: cold, desaturated shadows against the lantern's warm highlights. The split is
	// the whole visual idea of the room, so it is graded in rather than left to the light colours.
	PP.bOverride_ColorSaturation = true;
	PP.ColorSaturation = FVector4(0.78f, 0.78f, 0.78f, 1.f);
	PP.bOverride_ColorContrast = true;
	PP.ColorContrast = FVector4(1.12f, 1.12f, 1.14f, 1.f);
	PP.bOverride_ColorGainShadows = true;
	PP.ColorGainShadows = FVector4(0.82f, 0.92f, 1.15f, 1.f); // shadows drift blue
	PP.bOverride_ColorGainHighlights = true;
	PP.ColorGainHighlights = FVector4(1.08f, 1.f, 0.9f, 1.f);  // highlights drift to lantern-warm

	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.5f;
	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = 0.7f; // a touch of lens dispersion; cinematic, not a headache
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = 0.35f;
	PP.bOverride_VignetteIntensity = true;
	// Heavy enough to close the frame down, light enough that the locked door — which lives in the
	// right-hand corner of the waking shot — is not swallowed by it.
	PP.VignetteIntensity = 0.42f;
	PP.bOverride_AmbientOcclusionIntensity = true;
	PP.AmbientOcclusionIntensity = 0.65f;
	PP.bOverride_AmbientOcclusionRadius = true;
	PP.AmbientOcclusionRadius = 90.f;

	BuildRoom();
}

UStaticMeshComponent* AInvestigationRoomActor::AddSlab(const FString& Name, const FVector& Center, const FVector& Size)
{
	UStaticMeshComponent* Slab = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
	Slab->SetupAttachment(RoomRoot);
	Slab->SetStaticMesh(CubeMesh);
	Slab->SetRelativeLocation(Center);
	Slab->SetRelativeScale3D(Size / 100.f); // basic-shape cube is 100uu per side
	Slab->SetMobility(EComponentMobility::Movable); // avoids requiring a lightmap/lighting build
	Slab->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Slab->SetCollisionResponseToAllChannels(ECR_Block);
	Surfaces.Add(Slab);
	return Slab;
}

void AInvestigationRoomActor::ApplySurfaceMaterial()
{
	// The base must be loaded by name, NOT taken from the cube's own material slot. /Engine/
	// BasicShapes/Cube ships with a WorldGridMaterial instance assigned — the 1m checkerboard —
	// and tinting that just gives a tinted checkerboard on every wall in the room. BasicShapeMaterial
	// is the plain lit one, and it exposes "Color" and "Roughness" so a dynamic instance can dress
	// it in code without a Material Editor graph.
	if (!CubeMesh || Surfaces.Num() == 0)
	{
		return;
	}

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("Room01: BasicShapeMaterial failed to load — shell keeps the engine grid material"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("Room01: shell base material = %s, %d slabs"), *BaseMaterial->GetName(), Surfaces.Num());

	UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (!Tint)
	{
		return;
	}

	Tint->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.085f, 0.077f, 0.066f));
	Tint->SetScalarParameterValue(TEXT("Roughness"), 0.97f);

	for (UStaticMeshComponent* Slab : Surfaces)
	{
		if (Slab)
		{
			Slab->SetMaterial(0, Tint);
		}
	}

	// The floor slab is not part of the shell as far as the eye is concerned.
	//
	// Everywhere else this material is hidden — the walls are behind the plaster panels, the
	// ceiling is behind the ceiling boards — so its value only has to be plausible for the reveals
	// of the two openings. The floor is different: the boards are laid over it with a centimetre
	// and a half between each one, five per cent of them are missing outright, and every one of
	// those gaps is a window onto this slab. At the shell value that read as a pale, flat, utterly
	// untextured plank lying among the real ones, brighter than the boards in every channel and
	// thirty times brighter in blue — the grey boards on the floor were this, seen through the
	// gaps, not the boards themselves.
	//
	// What the gaps show now is a second course of real boards, laid by ARoomDressingActor (see
	// the subfloor in BuildFloor), so this slab should never be on screen at all — which it was,
	// until it was lowered out from over the subfloor (see BuildRoom). It stays dark anyway,
	// because the one thing it must not do again is be paler than the floor above it.
	if (FloorSlab)
	{
		UMaterialInstanceDynamic* Underfloor = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (Underfloor)
		{
			Underfloor->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.012f, 0.008f, 0.005f));
			Underfloor->SetScalarParameterValue(TEXT("Roughness"), 1.f);
			FloorSlab->SetMaterial(0, Underfloor);
		}
	}
}

void AInvestigationRoomActor::BuildRoom()
{
	const float WidthHalf = RoomWidth * 0.5f;
	const float DepthHalf = RoomDepth * 0.5f;

	// Well below the floor, not flush with it. At a top of 0 this slab swallowed everything the
	// dressing lays under the boards — the subfloor (-7..-1) and the collapse's void and joists
	// (down to -38) — so every gap and the hole under the hook showed this flat plate instead. It
	// is only a backstop now: the boards, the subfloor and the bottom of the collapse carry the
	// player.
	FloorSlab = AddSlab(TEXT("Floor"), FVector(0.f, 0.f, -45.f), FVector(RoomWidth, RoomDepth, 10.f));
	AddSlab(TEXT("Ceiling"), FVector(0.f, 0.f, RoomHeight + 5.f), FVector(RoomWidth, RoomDepth, 10.f));

	// North wall: solid. It is the wall the detective wakes up against, so it is only ever seen
	// out of the corner of the eye — the furniture stands along it precisely for that reason.
	AddSlab(TEXT("NorthWall"), FVector(0.f, -DepthHalf, RoomHeight * 0.5f), FVector(RoomWidth, WallThickness, RoomHeight));

	// South wall: the door opening, set towards the window end of the wall so that both openings
	// sit in the same view — the door is the thing the player is trying to reach, and it should be
	// visible, locked, from the moment they open their eyes.
	const float SouthLeftWidth = WidthHalf + DoorOpeningCenterX - DoorOpeningWidth * 0.5f;
	const float SouthRightWidth = WidthHalf - DoorOpeningCenterX - DoorOpeningWidth * 0.5f;
	AddSlab(TEXT("SouthWallLeft"), FVector(-WidthHalf + SouthLeftWidth * 0.5f, DepthHalf, RoomHeight * 0.5f), FVector(SouthLeftWidth, WallThickness, RoomHeight));
	AddSlab(TEXT("SouthWallRight"), FVector(WidthHalf - SouthRightWidth * 0.5f, DepthHalf, RoomHeight * 0.5f), FVector(SouthRightWidth, WallThickness, RoomHeight));
	AddSlab(TEXT("SouthWallLintel"), FVector(DoorOpeningCenterX, DepthHalf, (DoorOpeningHeight + RoomHeight) * 0.5f), FVector(DoorOpeningWidth, WallThickness, RoomHeight - DoorOpeningHeight));

	// West wall: solid.
	AddSlab(TEXT("WestWall"), FVector(-WidthHalf, 0.f, RoomHeight * 0.5f), FVector(WallThickness, RoomDepth, RoomHeight));

	// East wall: one large window. Left open rather than boarded — the storm is the room's second
	// light source and its only view, so the player has to be able to see straight out into it.
	const float EastEndDepth = DepthHalf - WindowOpeningWidth * 0.5f;
	AddSlab(TEXT("EastWallFront"), FVector(WidthHalf, -DepthHalf + EastEndDepth * 0.5f, RoomHeight * 0.5f), FVector(WallThickness, EastEndDepth, RoomHeight));
	AddSlab(TEXT("EastWallBack"), FVector(WidthHalf, DepthHalf - EastEndDepth * 0.5f, RoomHeight * 0.5f), FVector(WallThickness, EastEndDepth, RoomHeight));
	AddSlab(TEXT("EastWallBelowSill"), FVector(WidthHalf, 0.f, WindowSillHeight * 0.5f), FVector(WallThickness, WindowOpeningWidth, WindowSillHeight));
	AddSlab(TEXT("EastWallAboveWindow"), FVector(WidthHalf, 0.f, (WindowTopHeight + RoomHeight) * 0.5f), FVector(WallThickness, WindowOpeningWidth, RoomHeight - WindowTopHeight));
}

void AInvestigationRoomActor::SpawnOccupants()
{
	const float WidthHalf = RoomWidth * 0.5f;
	const float DepthHalf = RoomDepth * 0.5f;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Hinge at the window end of the doorway, so the leaf runs away from the player's eye-line and
	// the lock side is what they see. The door's own geometry is built out along its local +Y, and
	// a 90 degree yaw maps that onto world -X, across the opening.
	//
	// Hung at the room side of the opening, not at the far face of it. The wall is twenty
	// centimetres thick, and a leaf at the far face sits at the back of a twenty-centimetre shadow
	// box: the lantern lights the reveal and the door behind it stays dark, which is most of why a
	// shut door read as an open doorway. Flush with the inner face, it takes the same light the
	// wall beside it takes.
	const FVector DoorHingeLocation = GetActorLocation() + FVector(DoorOpeningCenterX + DoorOpeningWidth * 0.5f, DepthHalf - WallThickness + 2.6f, 0.f);
	const FRotator DoorRotation(0.f, 90.f, 0.f);
	Door = GetWorld()->SpawnActor<ADoorActor>(ADoorActor::StaticClass(), DoorHingeLocation, DoorRotation, SpawnParams);

	// The key is on the window sill — the one surface in the room the storm lights for free, so a
	// player who walks to the window to look out finds it without ever being told to.
	// Lying on the sill, not floating a finger's width over it: the mesh is a 9 by 2 by 2 cm bar
	// whose origin is its own centre, so the offset that rests it on the boards is its half
	// height. At seven it hung six centimetres clear of the sill with its shadow under it.
	const FVector KeyLocation = GetActorLocation() + FVector(WidthHalf - 26.f, -48.f, WindowSillHeight + 1.f);
	Key = GetWorld()->SpawnActor<AKeyPickupActor>(AKeyPickupActor::StaticClass(), KeyLocation, FRotator(0.f, 24.f, 0.f), SpawnParams);

	// Storm and dressing are spawned deferred so Configure() lands before their BeginPlay builds
	// anything — both size their geometry from the shell they are given.
	const FTransform StormTransform(FRotator::ZeroRotator, GetActorLocation() + FVector(WidthHalf, 0.f, 0.f));
	Storm = GetWorld()->SpawnActorDeferred<AStormWindowActor>(AStormWindowActor::StaticClass(), StormTransform, this);
	if (Storm)
	{
		FStormWindowSetup StormSetup;
		StormSetup.OpeningWidth = WindowOpeningWidth;
		StormSetup.SillHeight = WindowSillHeight;
		StormSetup.TopHeight = WindowTopHeight;
		StormSetup.WallThickness = WallThickness;
		Storm->Configure(StormSetup);
		Storm->FinishSpawning(StormTransform);
	}

	const FTransform DressingTransform(FRotator::ZeroRotator, GetActorLocation());
	Dressing = GetWorld()->SpawnActorDeferred<ARoomDressingActor>(ARoomDressingActor::StaticClass(), DressingTransform, this);
	if (Dressing)
	{
		FRoomDressingSetup DressingSetup;
		DressingSetup.Width = RoomWidth;
		DressingSetup.Depth = RoomDepth;
		DressingSetup.Height = RoomHeight;
		DressingSetup.WallThickness = WallThickness;
		DressingSetup.DoorOpeningWidth = DoorOpeningWidth;
		DressingSetup.DoorOpeningCenterX = DoorOpeningCenterX;
		DressingSetup.WakeSpot = FVector2D(GetWakeLocation());
		DressingSetup.WindowOpeningWidth = WindowOpeningWidth;
		DressingSetup.WindowSillHeight = WindowSillHeight;
		DressingSetup.WindowTopHeight = WindowTopHeight;
		Dressing->Configure(DressingSetup);
		Dressing->SetStorm(Storm);
		Dressing->FinishSpawning(DressingTransform);
	}

	// The corridor outside the door, in the room's own frame, looking out through its own window
	// at this room's storm.
	const FTransform CorridorTransform(FRotator::ZeroRotator, GetActorLocation());
	Corridor = GetWorld()->SpawnActorDeferred<ACorridorActor>(ACorridorActor::StaticClass(), CorridorTransform, this);
	if (Corridor)
	{
		FCorridorSetup CorridorSetup;
		CorridorSetup.RoomHalfWidth = WidthHalf;
		CorridorSetup.RoomHalfDepth = DepthHalf;
		CorridorSetup.WallThickness = WallThickness;
		CorridorSetup.Height = RoomHeight;
		CorridorSetup.StartDoorCenterX = DoorOpeningCenterX;
		CorridorSetup.StartDoorWidth = DoorOpeningWidth;
		CorridorSetup.StartDoorHeight = DoorOpeningHeight;
		Corridor->Configure(CorridorSetup, Storm);
		Corridor->FinishSpawning(CorridorTransform);
	}
}

void AInvestigationRoomActor::BeginPlay()
{
	Super::BeginPlay();

	ApplySurfaceMaterial();
	SpawnOccupants();
}
