#include "DetectiveLanternComponent.h"
#include "Components/PointLightComponent.h"

UDetectiveLanternComponent::UDetectiveLanternComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDetectiveLanternComponent::Initialize(UPointLightComponent* InLight)
{
	LightComponent = InLight;
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
}
