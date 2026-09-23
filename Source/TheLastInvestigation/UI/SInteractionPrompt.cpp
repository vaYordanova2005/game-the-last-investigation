#include "SInteractionPrompt.h"
#include "MenuStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

void SInteractionPrompt::Construct(const FArguments& InArgs)
{
	TAttribute<FText> PromptText = InArgs._PromptText;

	// The whole widget is a readout, and a readout must never be a mouse target. The SBox fills
	// the viewport and by default swallows every click that lands on it. Room01 has no UI input
	// so nothing shows today, but the first pause or menu screen drawn under this would find its
	// buttons dead.
	SetVisibility(EVisibility::HitTestInvisible);

	ChildSlot
	[
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.f, 0.f, 0.f, 90.f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(MenuStyle::InkDeep().R, MenuStyle::InkDeep().G, MenuStyle::InkDeep().B, 0.72f))
			.Padding(FMargin(18.f, 10.f))
			.Visibility_Lambda([PromptText]()
			{
				return PromptText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
			})
			[
				SAssignNew(PromptTextBlock, STextBlock)
				.Text(PromptText)
				.Font(MenuStyle::ButtonFont())
				.ColorAndOpacity(MenuStyle::GoldLite())
			]
		]
	];
}
