#include "RoomHUD.h"
#include "../UI/SInteractionPrompt.h"
#include "DetectiveCharacter.h"
#include "InteractionComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"

void ARoomHUD::BeginPlay()
{
	Super::BeginPlay();

	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	TWeakObjectPtr<ADetectiveCharacter> WeakCharacter = Cast<ADetectiveCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));

	SAssignNew(PromptWidget, SInteractionPrompt)
		.PromptText_Lambda([WeakCharacter]() -> FText
		{
			if (const ADetectiveCharacter* Character = WeakCharacter.Get())
			{
				if (const UInteractionComponent* Interaction = Character->FindComponentByClass<UInteractionComponent>())
				{
					return Interaction->GetCurrentPromptText();
				}
			}
			return FText::GetEmpty();
		});

	GEngine->GameViewport->AddViewportWidgetContent(PromptWidget.ToSharedRef());
}

void ARoomHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PromptWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(PromptWidget.ToSharedRef());
	}
	PromptWidget.Reset();

	Super::EndPlay(EndPlayReason);
}
