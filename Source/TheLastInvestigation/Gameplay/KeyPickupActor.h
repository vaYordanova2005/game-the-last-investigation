#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "KeyPickupActor.generated.h"

class UStaticMeshComponent;

/** The room key: a small mesh the player picks up, which sets bHasRoomKey on the character and removes itself. */
UCLASS()
class AKeyPickupActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AKeyPickupActor();

	virtual void Interact(AActor* Interactor) override;
	virtual FText GetInteractPrompt() const override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Key")
	TObjectPtr<UStaticMeshComponent> KeyMesh;
};
