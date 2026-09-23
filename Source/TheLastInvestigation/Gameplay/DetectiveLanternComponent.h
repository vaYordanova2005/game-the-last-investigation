#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DetectiveLanternComponent.generated.h"

class UPointLightComponent;

/**
 * Drives the flicker on the detective's lantern light. The light itself is created and attached
 * by the owning character (standard scene-component-in-constructor pattern) and handed in via
 * Initialize() — this component only owns the guttering-flame animation.
 */
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class UDetectiveLanternComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDetectiveLanternComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void Initialize(UPointLightComponent* InLight);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> LightComponent;

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
