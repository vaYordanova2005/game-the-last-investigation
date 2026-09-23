#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

/** A small centred bottom-of-screen prompt — "[E] Open door", "It's locked." — shown while an interactable is focused. */
class SInteractionPrompt : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInteractionPrompt) {}
		SLATE_ATTRIBUTE(FText, PromptText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<STextBlock> PromptTextBlock;
};
