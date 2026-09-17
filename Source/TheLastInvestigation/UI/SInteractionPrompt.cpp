#include "SInteractionPrompt.h"
#include "MenuStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

void SInteractionPrompt::Construct(const FArguments& InArgs)
{
	TAttribute<FText> PromptText = InArgs._PromptText;

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
				return PromptText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
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
