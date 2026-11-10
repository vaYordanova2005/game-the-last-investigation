#include "SMainMenu.h"
#include "MenuStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

void SMainMenu::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(MenuStyle::Background())
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 0.f, 0.f, 64.f)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("MainMenu", "Title", "THE LAST INVESTIGATION"))
				.Font(MenuStyle::TitleFont())
				.ColorAndOpacity(MenuStyle::TextColor())
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				MakeEntry(NSLOCTEXT("MainMenu", "NewGame", "New Game"), InArgs._OnNewGameClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 12.f, 0.f, 0.f)
			[
				MakeEntry(NSLOCTEXT("MainMenu", "Options", "Options"), FSimpleDelegate())
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 12.f, 0.f, 0.f)
			[
				MakeEntry(NSLOCTEXT("MainMenu", "Quit", "Quit"), InArgs._OnQuitClicked)
			]
		]
	];
}

TSharedRef<SWidget> SMainMenu::MakeEntry(const FText& Label, FSimpleDelegate OnClicked)
{
	const bool bEnabled = OnClicked.IsBound();
	TSharedRef<SButton> Button = SNew(SButton)
		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
		.IsEnabled(bEnabled)
		.OnClicked_Lambda([OnClicked]()
		{
			OnClicked.ExecuteIfBound();
			return FReply::Handled();
		});

	TWeakPtr<SButton> WeakButton = Button;
	Button->SetContent(
		SNew(STextBlock)
		.Text(Label)
		.Font(MenuStyle::ButtonFont())
		.ColorAndOpacity_Lambda([WeakButton, bEnabled]()
		{
			if (!bEnabled)
			{
				return FSlateColor(MenuStyle::TextDisabled());
			}
			const TSharedPtr<SButton> Pinned = WeakButton.Pin();
			return FSlateColor(Pinned.IsValid() && Pinned->IsHovered() ? MenuStyle::TextHover() : MenuStyle::TextColor());
		}));

	return Button;
}
