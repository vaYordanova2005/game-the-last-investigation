#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RoomHUD.generated.h"

class SInteractionPrompt;

/** Hosts the interaction-prompt Slate widget, following the same viewport-widget pattern as the main menu. */
UCLASS()
class ARoomHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TSharedPtr<SInteractionPrompt> PromptWidget;
};
