#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "ClueActor.generated.h"

class USceneComponent;

/**
 * A piece of environmental storytelling the detective can examine: a photograph, a letter, the
 * stopped clock, the scratched wall. It carries no mesh of its own — the room dressing builds its
 * body out of primitives under RootScene and then calls Configure() — so one class covers every
 * clue in the room without a Blueprint per object.
 *
 * Deliberately has no outline, glow or floating icon. The brief asks for clues that blend into
 * the room, so the only feedback is the interaction prompt that appears once the player has
 * already chosen to look closely at the right thing.
 */
UCLASS()
class AClueActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AClueActor();

	/** ShortName is the focus prompt ("Examine the photograph"); Description is what examining reveals. */
	void Configure(const FText& InShortName, const FText& InDescription);

	USceneComponent* GetRootScene() const { return RootScene; }

	virtual void Interact(AActor* Interactor) override;
	virtual FText GetInteractPrompt(const AActor* Interactor) const override;

	bool WasExamined() const { return bExamined; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Clue")
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY(EditAnywhere, Category = "Clue")
	FText ShortName;

	UPROPERTY(EditAnywhere, Category = "Clue")
	FText Description;

	bool bExamined = false;
};
