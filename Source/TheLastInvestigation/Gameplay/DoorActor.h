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
	virtual FText GetInteractPrompt() const override;

	UPROPERTY(EditAnywhere, Category = "Door")
	bool bIsLocked = true;

private:
	/** Planks, hinges and lock, built at BeginPlay from primitives and attached to the swinging leaf. */
	void BuildDoorDetail();

	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<USceneComponent> HingeRoot;

	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorLeaf;

	bool bIsOpen = false;
	float TargetYaw = 0.f;
};
