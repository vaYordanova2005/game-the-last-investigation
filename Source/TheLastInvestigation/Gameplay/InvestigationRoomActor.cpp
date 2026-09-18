#include "InvestigationRoomActor.h"
#include "DoorActor.h"
#include "KeyPickupActor.h"
#include "StormWindowActor.h"
#include "RoomDressingActor.h"
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
	// under middle grey: dark, but readable.
	PP.bOverride_CameraISO = true;
	PP.CameraISO = 800.f;
	PP.bOverride_CameraShutterSpeed = true;
	PP.CameraShutterSpeed = 30.f; // 1/30s
	PP.bOverride_DepthOfFieldFstop = true;
	PP.DepthOfFieldFstop = 1.8f;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 0.f; // trim knob — this is the dial to turn if the room reads too dark or too bright

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
	PP.ColorSaturation = FVector4(0.86f, 0.86f, 0.86f, 1.f);
	PP.bOverride_ColorContrast = true;
	PP.ColorContrast = FVector4(1.12f, 1.12f, 1.14f, 1.f);
	PP.bOverride_ColorGainShadows = true;
	PP.ColorGainShadows = FVector4(0.82f, 0.92f, 1.15f, 1.f); // shadows drift blue
	PP.bOverride_ColorGainHighlights = true;
	PP.ColorGainHighlights = FVector4(1.08f, 1.f, 0.9f, 1.f);  // highlights drift to lantern-warm

	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.5f;
	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = 1.4f; // a touch of lens dispersion; cinematic, not a headache
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = 0.35f;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.72f;
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
		return;
	}

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
}

void AInvestigationRoomActor::BuildRoom()
{
	const float WidthHalf = RoomWidth * 0.5f;
	const float DepthHalf = RoomDepth * 0.5f;

	AddSlab(TEXT("Floor"), FVector(0.f, 0.f, -5.f), FVector(RoomWidth, RoomDepth, 10.f));
	AddSlab(TEXT("Ceiling"), FVector(0.f, 0.f, RoomHeight + 5.f), FVector(RoomWidth, RoomDepth, 10.f));

	// North wall: door opening.
	const float NorthSideWidth = WidthHalf - DoorOpeningWidth * 0.5f;
	AddSlab(TEXT("NorthWallLeft"), FVector(-WidthHalf + NorthSideWidth * 0.5f, -DepthHalf, RoomHeight * 0.5f), FVector(NorthSideWidth, WallThickness, RoomHeight));
	AddSlab(TEXT("NorthWallRight"), FVector(WidthHalf - NorthSideWidth * 0.5f, -DepthHalf, RoomHeight * 0.5f), FVector(NorthSideWidth, WallThickness, RoomHeight));
	AddSlab(TEXT("NorthWallLintel"), FVector(0.f, -DepthHalf, (DoorOpeningHeight + RoomHeight) * 0.5f), FVector(DoorOpeningWidth, WallThickness, RoomHeight - DoorOpeningHeight));

	// South wall: solid.
	AddSlab(TEXT("SouthWall"), FVector(0.f, DepthHalf, RoomHeight * 0.5f), FVector(RoomWidth, WallThickness, RoomHeight));

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

	// Hinge sits at the left edge of the doorway; door faces into the opening at spawn rotation.
	const FVector DoorHingeLocation = GetActorLocation() + FVector(-DoorOpeningWidth * 0.5f, -DepthHalf, 0.f);
	const FRotator DoorRotation(0.f, -90.f, 0.f);
	Door = GetWorld()->SpawnActor<ADoorActor>(ADoorActor::StaticClass(), DoorHingeLocation, DoorRotation, SpawnParams);

	// The key is on the window sill — the one surface in the room the storm lights for free, so a
	// player who walks to the window to look out finds it without ever being told to.
	const FVector KeyLocation = GetActorLocation() + FVector(WidthHalf - 22.f, -60.f, WindowSillHeight + 8.f);
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
		DressingSetup.WindowOpeningWidth = WindowOpeningWidth;
		DressingSetup.WindowSillHeight = WindowSillHeight;
		DressingSetup.WindowTopHeight = WindowTopHeight;
		Dressing->Configure(DressingSetup);
		Dressing->SetStorm(Storm);
		Dressing->FinishSpawning(DressingTransform);
	}
}

void AInvestigationRoomActor::BeginPlay()
{
	Super::BeginPlay();

	ApplySurfaceMaterial();
	SpawnOccupants();
}
