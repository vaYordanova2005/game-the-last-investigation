#include "SMainMenu.h"
#include "SFogBackdrop.h"
#include "SKineticTitle.h"
#include "SMenuButton.h"
#include "MenuStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"

void SMainMenu::Construct(const FArguments& InArgs)
{
	TSharedPtr<SWidget> NewGameEntry;
	TSharedPtr<SWidget> OptionsEntry;
	TSharedPtr<SWidget> QuitEntry;

	ChildSlot
	[
		SNew(SOverlay)

		+ SOverlay::Slot()
		[
			SNew(SFogBackdrop)
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SKineticTitle)
				.Lines({ TEXT("THE LAST"), TEXT("INVESTIGATION") })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 18.f, 0.f, 0.f)
			[
				SAssignNew(SubtitleText, STextBlock)
				.Text(FText::FromString(MenuStyle::Tracked(TEXT("A PSYCHOLOGICAL MYSTERY"))))
				.Font(MenuStyle::SubtitleFont())
				.ColorAndOpacity(FSlateColor(MenuStyle::Gold()))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSpacer).Size(FVector2D(0.f, 64.f))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SAssignNew(NewGameEntry, SMenuButton)
				.Label(NSLOCTEXT("MainMenu", "NewGame", "New Game"))
				.OnClicked(InArgs._OnNewGameClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 12.f, 0.f, 0.f)
			[
				SAssignNew(OptionsEntry, SMenuButton)
				.Label(NSLOCTEXT("MainMenu", "Options", "Options"))
				.IsEnabled(false)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 12.f, 0.f, 0.f)
			[
				SAssignNew(QuitEntry, SMenuButton)
				.Label(NSLOCTEXT("MainMenu", "Quit", "Quit"))
				.OnClicked(InArgs._OnQuitClicked)
			]
		]
	];

	EntryWidgets = { NewGameEntry, OptionsEntry, QuitEntry };

	// the OS arrow is replaced by the dot-and-ring drawn in OnPaint
	SetCursor(EMouseCursor::None);

	if (SubtitleText.IsValid())
	{
		SubtitleText->SetRenderOpacity(0.f);
	}
	for (const TSharedPtr<SWidget>& Entry : EntryWidgets)
	{
		if (Entry.IsValid())
		{
			Entry->SetRenderOpacity(0.f);
		}
	}
}

void SMainMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	ElapsedTime += InDeltaTime;

	if (SubtitleText.IsValid())
	{
		const float Local = FMath::Clamp((ElapsedTime - 1.2f) / 0.9f, 0.f, 1.f);
		const float Eased = 1.f - FMath::Pow(1.f - Local, 4.f);
		SubtitleText->SetRenderOpacity(Eased);
		SubtitleText->SetRenderTransform(TOptional<FSlateRenderTransform>(
			FSlateRenderTransform(FVector2f(0.f, (1.f - Eased) * 10.f))));
	}

	// the entries rise in one after another, the band-in cascade from the web design
	for (int32 i = 0; i < EntryWidgets.Num(); ++i)
	{
		const TSharedPtr<SWidget>& Entry = EntryWidgets[i];
		if (!Entry.IsValid())
		{
			continue;
		}

		const float Local = FMath::Clamp((ElapsedTime - EntriesDelay - i * EntryStagger) / EntryDuration, 0.f, 1.f);
		const float Eased = 1.f - FMath::Pow(1.f - Local, 4.f);

		Entry->SetRenderOpacity(Eased);
		Entry->SetRenderTransform(TOptional<FSlateRenderTransform>(
			FSlateRenderTransform(FVector2f(0.f, (1.f - Eased) * 22.f))));
	}

	// ── custom cursor ──
	CursorPos = AllottedGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	// lerp(0.16) in fx.js — the ring lags a beat behind the dot
	RingPos = FMath::Lerp(RingPos, CursorPos, FMath::Clamp(InDeltaTime * 11.f, 0.f, 1.f));

	// the ring swells over anything interactive, as .cursor-hot did
	bool bOverEntry = false;
	for (const TSharedPtr<SWidget>& Entry : EntryWidgets)
	{
		if (Entry.IsValid() && Entry->IsHovered())
		{
			bOverEntry = true;
			break;
		}
	}
	RingSwell = FMath::FInterpTo(RingSwell, bOverEntry ? 1.f : 0.f, InDeltaTime, 9.f);
}

int32 SMainMenu::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 ContentLayer = SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const int32 CursorLayer = ContentLayer + 1;
	const FLinearColor Gold = MenuStyle::Gold();
	const FLinearColor GoldLite = MenuStyle::GoldLite();

	// ── the dot, snapped to the pointer ──
	const float DotSize = FMath::Lerp(9.f, 6.f, RingSwell);
	const FVector2D DotExtent(DotSize, DotSize);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		CursorLayer,
		AllottedGeometry.ToPaintGeometry(DotExtent, FSlateLayoutTransform(CursorPos - DotExtent * 0.5f)),
		MenuStyle::SoftCircleBrush(),
		ESlateDrawEffect::None,
		GoldLite);

	// ── the trailing ring ──
	const float Radius = FMath::Lerp(19.f, 34.f, RingSwell);
	constexpr int32 Segments = 40;
	TArray<FVector2D> Circle;
	Circle.Reserve(Segments + 1);
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float Angle = (2.f * PI * i) / Segments;
		Circle.Add(RingPos + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		CursorLayer,
		AllottedGeometry.ToPaintGeometry(),
		Circle,
		ESlateDrawEffect::None,
		FLinearColor(Gold.R, Gold.G, Gold.B, FMath::Lerp(0.75f, 0.95f, RingSwell)),
		/*bAntialias*/ true,
		1.5f);

	return CursorLayer;
}
