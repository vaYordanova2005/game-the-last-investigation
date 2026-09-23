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

	/**
	 * Prompt line shown in the HUD while this is the focused target, e.g. "[E] Open door".
	 *
	 * Takes the interactor for the same reason Interact does: what the prompt should say can
	 * depend on what the player is carrying. The door is locked until he has the key, and a
	 * prompt that still reads "locked" once the key is in his pocket tells him the wrong thing.
	 */
	virtual FText GetInteractPrompt(const AActor* Interactor) const = 0;
};
