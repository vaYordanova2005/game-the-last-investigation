#include "InteractionComponent.h"
#include "InteractableInterface.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	FocusedActor = nullptr;

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	const FVector Start = PC->PlayerCameraManager->GetCameraLocation();
	const FVector End = Start + PC->PlayerCameraManager->GetCameraRotation().Vector() * TraceDistance;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		if (Hit.GetActor() && Hit.GetActor()->Implements<UInteractableInterface>())
		{
			FocusedActor = Hit.GetActor();
		}
	}
}

void UInteractionComponent::InteractWithFocusedTarget()
{
	if (AActor* Target = FocusedActor.Get())
	{
		if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(Target))
		{
			Interactable->Interact(GetOwner());
		}
	}
}

FText UInteractionComponent::GetCurrentPromptText() const
{
	if (const AActor* Target = FocusedActor.Get())
	{
		if (const IInteractableInterface* Interactable = Cast<const IInteractableInterface>(Target))
		{
			return Interactable->GetInteractPrompt(GetOwner());
		}
	}
	return FText::GetEmpty();
}
