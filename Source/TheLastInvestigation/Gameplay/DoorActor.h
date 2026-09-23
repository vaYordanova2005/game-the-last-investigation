#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "DoorActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;

/** The one locked door standing between the player and the rest of the house. */
UCLASS()
class ADoorActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	ADoorActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Interact(AActor* Interactor) override;
	virtual FText GetInteractPrompt(const AActor* Interactor) const override;

	UPROPERTY(EditAnywhere, Category = "Door")
	bool bIsLocked = true;

private:
	/** Planks, hinges and lock, built at BeginPlay from primitives and attached to the swinging leaf. */
	void BuildDoorDetail();

	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<USceneComponent> HingeRoot;

	/**
	 * What actually swings. HingeRoot is the actor's root, so its *relative* rotation is the
	 * actor's rotation in the world — the ninety degrees that turn the door into the doorway —
	 * and driving that to a target of zero does not close the door, it rotates the whole door out
	 * of the wall. Swing starts at zero and is the only thing Tick is allowed to touch.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<USceneComponent> Swing;

	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorLeaf;

	bool bIsOpen = false;
	float TargetYaw = 0.f;

	/** Fixed seed, so the door's rot, splits and rust streaks are the same door every session. */
	FRandomStream Random;
};
