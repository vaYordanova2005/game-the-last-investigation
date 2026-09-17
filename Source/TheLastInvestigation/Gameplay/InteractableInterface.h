#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractableInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/** Anything the player can focus with the interaction line trace and press E on. */
class IInteractableInterface
{
	GENERATED_BODY()

public:
	virtual void Interact(AActor* Interactor) = 0;

	/** Prompt line shown in the HUD while this is the focused target, e.g. "[E] Open door". */
	virtual FText GetInteractPrompt() const = 0;
};
