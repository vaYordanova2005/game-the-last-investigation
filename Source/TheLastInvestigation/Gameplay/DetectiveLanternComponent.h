#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DetectiveLanternComponent.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Drives the flicker on the detective's lantern light. The light itself is created and attached
 * by the owning character (standard scene-component-in-constructor pattern) and handed in via
 * Initialize(). This component only owns the guttering-flame animation: the light, and the
 * candle flame inside the globe, which stretches, shrinks and dims with it.
 */
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class UDetectiveLanternComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDetectiveLanternComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void Initialize(UPointLightComponent* InLight, UStaticMeshComponent* InFlame = nullptr, UMaterialInstanceDynamic* InFlameMaterial = nullptr,
		UPointLightComponent* InNearLight = nullptr, float InNearIntensity = 0.f);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> LightComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FlameComponent;

	/** The flame's light on the lantern and the hand (see ADetectiveCharacter::LanternNearLight), flickered with the rest. */
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> NearLightComponent;

	float NearIntensity = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlameMaterial;

	/** The flame's emissive at an even burn; it follows the light's flicker. */
	UPROPERTY(EditAnywhere, Category = "Lantern")
	float FlameGlow = 40.f;

	// Candelas, matching the light's intensity units. Balanced against the room's ~0.12 albedo
	// surfaces: bright enough to read a wall a few metres off, far short of the blown-out white
	// that four-figure values produce.
	UPROPERTY(EditAnywhere, Category = "Lantern")
	float BaseIntensity = 260.f;

	UPROPERTY(EditAnywhere, Category = "Lantern")
	float FlickerAmplitude = 40.f;

	UPROPERTY(EditAnywhere, Category = "Lantern")
	float FlickerSpeed = 9.f;

	float FlickerSeed = 0.f;
	float ElapsedTime = 0.f;
};
