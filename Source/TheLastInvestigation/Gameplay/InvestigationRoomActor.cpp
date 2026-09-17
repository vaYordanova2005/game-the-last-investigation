#include "InvestigationRoomActor.h"
#include "DoorActor.h"
#include "KeyPickupActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

AInvestigationRoomActor::AInvestigationRoomActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
	SetRootComponent(RoomRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeMeshFinder.Object;

	FogComponent = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("FogComponent"));
	FogComponent->SetupAttachment(RoomRoot);
	FogComponent->FogDensity = 0.04f;
	FogComponent->FogHeightFalloff = 0.2f;
	FogComponent->FogInscatteringLuminance = FLinearColor(0.02f, 0.03f, 0.025f);
	FogComponent->SetRelativeLocation(FVector(0.f, 0.f, -5.f));

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(RoomRoot);
	PostProcess->bUnbound = true;
	PostProcess->Settings.bOverride_VignetteIntensity = true;
	PostProcess->Settings.VignetteIntensity = 0.65f;
	PostProcess->Settings.bOverride_FilmGrainIntensity = true;
	PostProcess->Settings.FilmGrainIntensity = 0.3f;

	LightningLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LightningLight"));
	LightningLight->SetupAttachment(RoomRoot);
	LightningLight->SetIntensityUnits(ELightUnits::Candelas);
	LightningLight->SetIntensity(0.f);
	LightningLight->SetLightColor(FLinearColor(0.75f, 0.8f, 1.f)); // cold blue-white flash
	LightningLight->SetAttenuationRadius(1600.f);
	LightningLight->SetMobility(EComponentMobility::Movable);
	LightningLight->SetCastShadows(false);

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
	// BasicShapeMaterial ships near-white, which reads as a blown-out void under any lighting.
	// It exposes a "Color" parameter, so a dynamic instance can tint every surface to a dusty
	// grey-brown in code — no Material Editor graph needed.
	if (!CubeMesh || Surfaces.Num() == 0)
	{
		return;
	}

	UMaterialInterface* BaseMaterial = Surfaces[0]->GetMaterial(0);
	if (!BaseMaterial)
	{
		return;
	}

	UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (!Tint)
	{
		return;
	}

	Tint->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.125f, 0.112f, 0.095f));
	Tint->SetScalarParameterValue(TEXT("Roughness"), 0.95f);

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
	const float DoorOpeningWidth = 110.f;
	const float DoorOpeningHeight = 210.f;
	const float NorthSideWidth = WidthHalf - DoorOpeningWidth * 0.5f;
	AddSlab(TEXT("NorthWallLeft"), FVector(-WidthHalf + NorthSideWidth * 0.5f, -DepthHalf, RoomHeight * 0.5f), FVector(NorthSideWidth, WallThickness, RoomHeight));
	AddSlab(TEXT("NorthWallRight"), FVector(WidthHalf - NorthSideWidth * 0.5f, -DepthHalf, RoomHeight * 0.5f), FVector(NorthSideWidth, WallThickness, RoomHeight));
	AddSlab(TEXT("NorthWallLintel"), FVector(0.f, -DepthHalf, (DoorOpeningHeight + RoomHeight) * 0.5f), FVector(DoorOpeningWidth, WallThickness, RoomHeight - DoorOpeningHeight));

	// South wall: solid.
	AddSlab(TEXT("SouthWall"), FVector(0.f, DepthHalf, RoomHeight * 0.5f), FVector(RoomWidth, WallThickness, RoomHeight));

	// West wall: solid.
	AddSlab(TEXT("WestWall"), FVector(-WidthHalf, 0.f, RoomHeight * 0.5f), FVector(WallThickness, RoomDepth, RoomHeight));

	// East wall: boarded window opening.
	const float WindowWidth = 160.f;
	const float WindowSillHeight = 90.f;
	const float WindowTopHeight = 200.f;
	const float EastEndDepth = DepthHalf - WindowWidth * 0.5f;
	AddSlab(TEXT("EastWallFront"), FVector(WidthHalf, -DepthHalf + EastEndDepth * 0.5f, RoomHeight * 0.5f), FVector(WallThickness, EastEndDepth, RoomHeight));
	AddSlab(TEXT("EastWallBack"), FVector(WidthHalf, DepthHalf - EastEndDepth * 0.5f, RoomHeight * 0.5f), FVector(WallThickness, EastEndDepth, RoomHeight));
	AddSlab(TEXT("EastWallBelowSill"), FVector(WidthHalf, 0.f, WindowSillHeight * 0.5f), FVector(WallThickness, WindowWidth, WindowSillHeight));
	AddSlab(TEXT("EastWallAboveWindow"), FVector(WidthHalf, 0.f, (WindowTopHeight + RoomHeight) * 0.5f), FVector(WallThickness, WindowWidth, RoomHeight - WindowTopHeight));

	// Boarded-up planks across the window gap, with vertical gaps for lightning to shine through.
	const float BoardZs[3] = { WindowSillHeight + 14.f, (WindowSillHeight + WindowTopHeight) * 0.5f, WindowTopHeight - 14.f };
	for (int32 i = 0; i < 3; ++i)
	{
		AddSlab(FString::Printf(TEXT("WindowBoard_%d"), i), FVector(WidthHalf + WallThickness * 0.5f + 3.f, 0.f, BoardZs[i]), FVector(4.f, WindowWidth + 12.f, 16.f));
	}

	// The flash light sits just outside the window gap.
	LightningLight->SetRelativeLocation(FVector(WidthHalf + 150.f, 0.f, (WindowSillHeight + WindowTopHeight) * 0.5f));
}

void AInvestigationRoomActor::BeginPlay()
{
	Super::BeginPlay();

	ApplySurfaceMaterial();

	const float WidthHalf = RoomWidth * 0.5f;
	const float DepthHalf = RoomDepth * 0.5f;
	const float DoorOpeningWidth = 110.f;
	const float WindowSillHeight = 90.f;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Hinge sits at the left edge of the doorway; door faces into the opening at spawn rotation.
	// The leaf's swing direction is a cosmetic detail — flip ADoorActor's open-yaw sign if it
	// visually swings into the wall instead of into the room once you can see it in-editor.
	const FVector DoorHingeLocation = GetActorLocation() + FVector(-DoorOpeningWidth * 0.5f, -DepthHalf, 0.f);
	const FRotator DoorRotation(0.f, -90.f, 0.f);
	Door = GetWorld()->SpawnActor<ADoorActor>(ADoorActor::StaticClass(), DoorHingeLocation, DoorRotation, SpawnParams);

	const FVector KeyLocation = GetActorLocation() + FVector(WidthHalf - 15.f, -40.f, WindowSillHeight + 4.f);
	Key = GetWorld()->SpawnActor<AKeyPickupActor>(AKeyPickupActor::StaticClass(), KeyLocation, FRotator::ZeroRotator, SpawnParams);
}

void AInvestigationRoomActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (FlashTimeRemaining > 0.f)
	{
		FlashTimeRemaining -= DeltaTime;
		if (FlashTimeRemaining <= 0.f)
		{
			LightningLight->SetIntensity(0.f);
			TimeUntilNextFlash = FMath::FRandRange(4.f, 12.f);
		}
		return;
	}

	TimeUntilNextFlash -= DeltaTime;
	if (TimeUntilNextFlash <= 0.f)
	{
		LightningLight->SetIntensity(FMath::FRandRange(2200.f, 3600.f));
		FlashTimeRemaining = FMath::FRandRange(0.08f, 0.15f);
		// PlayStormThunder() would fire here once thunder audio is sourced (freesound.org, with permission).
	}
}
