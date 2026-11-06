#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "KeyPickupActor.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

/**
 * The room key: a brass mortice key lying on the window sill, which sets bHasRoomKey on the
 * character and removes itself when picked up.
 *
 * The actor's origin is the middle of the key's underside: it is placed on the surface it lies on.
 */
UCLASS()
class AKeyPickupActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AKeyPickupActor();

	virtual void BeginPlay() override;
	virtual void Interact(AActor* Interactor) override;
	virtual FText GetInteractPrompt(const AActor* Interactor) const override;

private:
	/** What the interaction trace finds: a hand's breadth round the key, invisible, blocking only Visibility. */
	UPROPERTY(VisibleAnywhere, Category = "Key")
	TObjectPtr<UBoxComponent> HitVolume;

	/** Tools/make_bedroom.py's bedroom_key, or a small box if the art pipeline has not been run. */
	UPROPERTY(VisibleAnywhere, Category = "Key")
	TObjectPtr<UStaticMeshComponent> KeyMesh;
};
