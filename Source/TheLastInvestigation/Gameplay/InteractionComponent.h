#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

class IInteractableInterface;

/** Line-traces from the owning character's camera each tick to find whatever IInteractable the player is looking at. */
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Interacts with the currently focused target, if any. */
	void InteractWithFocusedTarget();

	/** Empty text if nothing is focused. */
	FText GetCurrentPromptText() const;

	UPROPERTY(EditAnywhere, Category = "Interaction")
	float TraceDistance = 220.f;

private:
	TWeakObjectPtr<AActor> FocusedActor;
};
