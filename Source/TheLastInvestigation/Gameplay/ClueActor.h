#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClueActor.generated.h"

class USceneComponent;

/**
 * An object that will be one of the investigation's clues: a photograph, a letter, the stopped
 * clock, the scratched wall. It carries no mesh of its own — the room builds its body out of
 * primitives and props under RootScene — so one class covers every such object without a
 * Blueprint per object.
 *
 * For now it is set dressing and nothing else: no prompt, no text, nothing to examine. The clue
 * writing was taken out while the house is being designed, and comes back once the game is built
 * end to end. Keeping these objects as their own actors marks where the clues are to go.
 */
UCLASS()
class AClueActor : public AActor
{
	GENERATED_BODY()

public:
	AClueActor();

	USceneComponent* GetRootScene() const { return RootScene; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Clue")
	TObjectPtr<USceneComponent> RootScene;
};
