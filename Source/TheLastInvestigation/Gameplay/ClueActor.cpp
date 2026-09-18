#include "ClueActor.h"
#include "Components/SceneComponent.h"

AClueActor::AClueActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);
	RootScene->SetMobility(EComponentMobility::Movable);
}

void AClueActor::Configure(const FText& InShortName, const FText& InDescription)
{
	ShortName = InShortName;
	Description = InDescription;
}

void AClueActor::Interact(AActor* Interactor)
{
	// Examining is a one-way flip: once the detective has read the thing, the prompt keeps showing
	// what he found instead of inviting him to read it again.
	bExamined = true;
}

FText AClueActor::GetInteractPrompt() const
{
	if (bExamined && !Description.IsEmpty())
	{
		return Description;
	}
	return FText::Format(FText::FromString(TEXT("[E] {0}")), ShortName);
}
