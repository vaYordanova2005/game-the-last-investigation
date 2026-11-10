#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

// The main menu, as a bare placeholder: the title and its entries on black.
// The final design is still to be made.
class SMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMainMenu) {}
		SLATE_EVENT(FSimpleDelegate, OnNewGameClicked)
		SLATE_EVENT(FSimpleDelegate, OnQuitClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** One text entry; a null delegate makes it a disabled placeholder. */
	TSharedRef<SWidget> MakeEntry(const FText& Label, FSimpleDelegate OnClicked);
};
