#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

// The main menu: the kinetic wordmark and its entries over the smoke backdrop.
// The palette, the smoke, the per-letter type and the card treatment on the
// buttons all come from the Pythia web design — see the CLAUDE.md decision log
// for what was carried over, adapted, or left behind.
class SMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMainMenu) {}
		SLATE_EVENT(FSimpleDelegate, OnNewGameClicked)
		SLATE_EVENT(FSimpleDelegate, OnQuitClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TSharedPtr<STextBlock> SubtitleText;
	TArray<TSharedPtr<SWidget>> EntryWidgets;

	float ElapsedTime = 0.f;

	/** The custom cursor: the dot snaps to the pointer, the ring trails behind. */
	FVector2D CursorPos = FVector2D::ZeroVector;
	FVector2D RingPos = FVector2D::ZeroVector;
	float RingSwell = 0.f;

	/** The entries cascade in once the wordmark has finished landing. */
	static constexpr float EntriesDelay = 1.5f;
	static constexpr float EntryStagger = 0.11f;
	static constexpr float EntryDuration = 0.7f;
};
