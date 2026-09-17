#include "SKineticTitle.h"
#include "MenuStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSpacer.h"
#include "Framework/Application/SlateApplication.h"

void SKineticTitle::Construct(const FArguments& InArgs)
{
	// .22em of tracking, as in the web wordmark
	const FSlateFontInfo Font = MenuStyle::TitleFont();
	const float Tracking = Font.Size * 0.11f;

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
	int32 GlobalIndex = 0;

	for (const FString& Line : InArgs._Lines)
	{
		TSharedRef<SHorizontalBox> LineBox = SNew(SHorizontalBox);

		for (int32 i = 0; i < Line.Len(); ++i)
		{
			const TCHAR Ch = Line[i];

			if (Ch == TEXT(' '))
			{
				LineBox->AddSlot().AutoWidth()
				[
					SNew(SSpacer).Size(FVector2D(Font.Size * 0.42f, 0.f))
				];
				++GlobalIndex;
				continue;
			}

			const FText CharText = FText::FromString(FString::Chr(Ch));

			FTitleChar Entry;
			Entry.Delay = 0.2f + GlobalIndex * CharStagger;
			Entry.Seed = FMath::FRandRange(0.f, 100.f);

			TSharedPtr<SOverlay> CharRoot;
			SAssignNew(CharRoot, SOverlay);

			// ── extruded side, deepest copy first ──
			// Slate can't put a gradient inside a glyph, so the web design's
			// cream-to-bronze face is built instead out of stacked copies:
			// darkening ones pushed down for the side of the letter, and a pale
			// one peeking out above for the lit top edge.
			const float Depth = Font.Size * 0.105f;
			for (int32 Layer = ExtrudeLayers; Layer >= 1; --Layer)
			{
				const float Ratio = static_cast<float>(Layer) / ExtrudeLayers;
				const FLinearColor SideColor = FMath::Lerp(
					MenuStyle::GoldDark(),
					FLinearColor(0.015f, 0.02f, 0.014f, 1.f),
					Ratio);

				TSharedPtr<STextBlock> SideCopy;
				CharRoot->AddSlot()
				[
					SAssignNew(SideCopy, STextBlock)
					.Text(CharText)
					.Font(Font)
					.ColorAndOpacity(FSlateColor(SideColor))
				];
				SideCopy->SetRenderTransform(TOptional<FSlateRenderTransform>(
					FSlateRenderTransform(FVector2f(0.f, Depth * Ratio))));
			}

			// ── lit top edge ──
			{
				TSharedPtr<STextBlock> Highlight;
				CharRoot->AddSlot()
				[
					SAssignNew(Highlight, STextBlock)
					.Text(CharText)
					.Font(Font)
					.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.97f, 0.87f)))
				];
				Highlight->SetRenderTransform(TOptional<FSlateRenderTransform>(
					FSlateRenderTransform(FVector2f(0.f, -Font.Size * 0.028f))));
			}

			CharRoot->AddSlot()
			[
				SAssignNew(Entry.GhostBlue, STextBlock)
				.Text(CharText)
				.Font(Font)
				.ColorAndOpacity(FSlateColor(MenuStyle::GlitchBlue()))
			];

			CharRoot->AddSlot()
			[
				SAssignNew(Entry.GhostRed, STextBlock)
				.Text(CharText)
				.Font(Font)
				.ColorAndOpacity(FSlateColor(MenuStyle::GlitchRed()))
			];

			CharRoot->AddSlot()
			[
				SAssignNew(Entry.Main, STextBlock)
				.Text(CharText)
				.Font(Font)
				.ColorAndOpacity(FSlateColor(MenuStyle::Gold()))
			];

			Entry.Root = CharRoot;

			// letters scale about their own centre, not their top-left corner
			CharRoot->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			CharRoot->SetRenderOpacity(0.f);
			Entry.GhostBlue->SetRenderOpacity(0.f);
			Entry.GhostRed->SetRenderOpacity(0.f);

			LineBox->AddSlot()
				.AutoWidth()
				.Padding(Tracking * 0.5f, 0.f)
			[
				CharRoot.ToSharedRef()
			];

			Chars.Add(Entry);
			++GlobalIndex;
		}

		Root->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
		[
			LineBox
		];
	}

	ChildSlot
	[
		Root
	];
}

void SKineticTitle::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	ElapsedTime += InDeltaTime;

	const FVector2D CursorAbs = FSlateApplication::Get().GetCursorPos();
	const float Scale = FMath::Max(AllottedGeometry.Scale, KINDA_SMALL_NUMBER);

	for (FTitleChar& Entry : Chars)
	{
		if (!Entry.Root.IsValid())
		{
			continue;
		}

		// ── intro: rise and un-squash, the stand-in for rotateX(-78deg) ──
		const float Local = FMath::Clamp((ElapsedTime - Entry.Delay) / CharDuration, 0.f, 1.f);
		// ease-out cubic-bezier(.16, 1, .3, 1) is near enough to a quartic ease-out
		const float Eased = 1.f - FMath::Pow(1.f - Local, 4.f);

		const float RiseY = (1.f - Eased) * MenuStyle::TitleFont().Size * 0.5f;
		const float SquashY = FMath::Lerp(0.21f, 1.f, Eased); // cos(78deg) ~= 0.21

		// ── glitch: fires only while the pointer is actually over the letter ──
		const FGeometry& CharGeom = Entry.Root->GetTickSpaceGeometry();
		const bool bHovered = Eased >= 1.f
			&& CharGeom.GetLayoutBoundingRect()
				.ExtendBy(FMargin(HoverPadding * Scale))
				.ContainsPoint(CursorAbs);

		// snaps on, eases off — so brushing past still reads as a flicker
		Entry.GlitchAlpha = bHovered
			? 1.f
			: FMath::FInterpTo(Entry.GlitchAlpha, 0.f, InDeltaTime, 7.f);

		// the letter shakes in place; it does not travel
		FVector2D Jitter = FVector2D::ZeroVector;
		if (Entry.GlitchAlpha > 0.01f)
		{
			const float Step = FMath::Floor(ElapsedTime * JitterHz);
			const float Amp = Entry.GlitchAlpha * 3.f;
			Jitter.X = (FMath::Frac(FMath::Sin(Step * 12.9898f + Entry.Seed) * 43758.5453f) - 0.5f) * 2.f * Amp;
			Jitter.Y = (FMath::Frac(FMath::Sin(Step * 78.233f + Entry.Seed) * 43758.5453f) - 0.5f) * 2.f * Amp;
		}

		Entry.Root->SetRenderOpacity(Eased);
		Entry.Root->SetRenderTransform(TOptional<FSlateRenderTransform>(
			FSlateRenderTransform(
				FScale2D(1.f, SquashY),
				FVector2f(static_cast<float>(Jitter.X), static_cast<float>(Jitter.Y + RiseY)))));

		if (Entry.Main.IsValid())
		{
			// only a small lift toward the pale gold — the colour should come
			// from the ghosts splitting, not from the letter blowing out white
			Entry.Main->SetColorAndOpacity(FSlateColor(
				FMath::Lerp(MenuStyle::Gold(), MenuStyle::GoldLite(), Entry.GlitchAlpha * 0.45f)));
		}

		if (Entry.GhostBlue.IsValid() && Entry.GhostRed.IsValid())
		{
			if (Entry.GlitchAlpha > 0.01f)
			{
				const float Step = FMath::Floor(ElapsedTime * JitterHz);
				const float Dir = FMath::Frac(Step * 0.5f) < 0.5f ? -1.f : 1.f;
				const float Offset = Entry.GlitchAlpha * MenuStyle::TitleFont().Size * 0.07f * Dir;

				Entry.GhostBlue->SetRenderOpacity(Entry.GlitchAlpha);
				Entry.GhostRed->SetRenderOpacity(Entry.GlitchAlpha);
				Entry.GhostBlue->SetRenderTransform(TOptional<FSlateRenderTransform>(
					FSlateRenderTransform(FVector2f(-Offset, 0.f))));
				Entry.GhostRed->SetRenderTransform(TOptional<FSlateRenderTransform>(
					FSlateRenderTransform(FVector2f(Offset, 0.f))));
			}
			else
			{
				Entry.GhostBlue->SetRenderOpacity(0.f);
				Entry.GhostRed->SetRenderOpacity(0.f);
			}
		}
	}
}
