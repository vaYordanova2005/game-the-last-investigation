#include "DetectiveLanternComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

UDetectiveLanternComponent::UDetectiveLanternComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDetectiveLanternComponent::Initialize(UPointLightComponent* InLight, UStaticMeshComponent* InFlame, UMaterialInstanceDynamic* InFlameMaterial,
	UPointLightComponent* InNearLight, float InNearIntensity)
{
	NearLightComponent = InNearLight;
	NearIntensity = InNearIntensity;
	LightComponent = InLight;
	FlameComponent = InFlame;
	FlameMaterial = InFlameMaterial;
	if (LightComponent)
	{
		LightComponent->SetIntensity(BaseIntensity);
	}
}

void UDetectiveLanternComponent::BeginPlay()
{
	Super::BeginPlay();
	FlickerSeed = FMath::FRand() * 1000.f;
}

void UDetectiveLanternComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!LightComponent)
	{
		return;
	}

	ElapsedTime += DeltaTime;

	// A slow perlin drift plus a faster jitter reads as a guttering flame rather than a strobing bulb.
	const float Drift = FMath::PerlinNoise1D(ElapsedTime * FlickerSpeed * 0.35f + FlickerSeed);
	const float Jitter = FMath::PerlinNoise1D(ElapsedTime * FlickerSpeed + FlickerSeed * 2.f);
	const float Flicker = Drift * 0.7f + Jitter * 0.3f;

	LightComponent->SetIntensity(BaseIntensity + Flicker * FlickerAmplitude);

	// The flame is the light's shape: taller and brighter as it flares, short and dim as it
	// gutters, and it never stops licking at the top (the fast jitter alone, on its height).
	const float Level = (BaseIntensity + Flicker * FlickerAmplitude) / FMath::Max(BaseIntensity, 1.f);
	if (FlameComponent)
	{
		const float Lick = FMath::PerlinNoise1D(ElapsedTime * FlickerSpeed * 2.3f + FlickerSeed * 3.f);
		const float Width = FMath::Lerp(0.92f, 1.06f, FMath::Clamp(Level - 0.5f, 0.f, 1.f));
		FlameComponent->SetRelativeScale3D(FVector(Width, Width, FMath::Max(0.55f, Level + Lick * 0.18f)));
	}
	if (NearLightComponent)
	{
		NearLightComponent->SetIntensity(NearIntensity * Level);
	}
	if (FlameMaterial)
	{
		FlameMaterial->SetScalarParameterValue(TEXT("Intensity"), FlameGlow * Level);
	}
}
