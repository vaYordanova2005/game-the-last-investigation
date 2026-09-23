#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "HallDoorActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;

/** How one of the corridor's doors is left, handed over by the corridor that spawns it. */
struct FHallDoorSetup
{
	float Width = 100.f;
	float Height = 212.f;
	/** Degrees the leaf already stands open into its room. Zero is shut. */
	float AjarYaw = 0.f;
	/** Seed for the rot, splits and fittings, so every door on the landing is a different door. */
	int32 Seed = 1;
	/** Multiplies the wood tint: some doors kept more of their finish than others. */
	FLinearColor WoodTint = FLinearColor(0.255f, 0.279f, 0.295f);
	/** Four panels or six. Two patterns on one landing is what a house that grew over time has. */
	bool bSixPanel = false;
	/** A fist-sized hole rotted through the bottom panel. */
	bool bRotHole = false;
	/** What the detective is told when he tries it. */
	FString Prompt;
};

/**
 * A bedroom door on the upstairs corridor. None of them open: the locked ones are locked, and the
 * ones standing ajar will not go any further, because the corridor is a place to be walked down,
 * not a set of rooms. What is behind an ajar door is a black gap a hand wide, which is worth more
 * than any room that could be built behind it.
 *
 * Convention: the actor's local +X is the corridor side of the leaf. The leaf is built out along
 * local +Y from the hinge, and a positive swing turns it away from the corridor, into its room.
 */
UCLASS()
class AHallDoorActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AHallDoorActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FHallDoorSetup& InSetup);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Interact(AActor* Interactor) override;
	virtual FText GetInteractPrompt(const AActor* Interactor) const override;

private:
	void BuildLeaf();

	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<USceneComponent> HingeRoot;

	/** What turns. Never the root — see ADoorActor::Swing for why. */
	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<USceneComponent> Swing;

	FHallDoorSetup Setup;
	FRandomStream Random;

	/** A tried door gives a little and settles back: the only answer any of these doors gives. */
	float RattleTime = -1.f;
};
