#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

// The kinetic wordmark from fx.js, rebuilt in Slate: the title is split into
// one widget per character so each can be animated on its own —
//   · staggered intro (rise + un-squash, standing in for the web's rotateX)
//   · the .char:hover glitch: the letter jitters in place and splits into
//     green/red ghosts while the pointer is over it
// The letters hold their position otherwise; the web design's cursor repulsion
// was tried and cut — see the CLAUDE.md decision log.
class SKineticTitle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKineticTitle) {}
		/** Each entry becomes its own line of characters. */
		SLATE_ARGUMENT(TArray<FString>, Lines)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	struct FTitleChar
	{
		TSharedPtr<SWidget> Root;
		TSharedPtr<STextBlock> Main;
		TSharedPtr<STextBlock> GhostBlue;
		TSharedPtr<STextBlock> GhostRed;
		float Delay = 0.f;
		float Seed = 0.f;
		float GlitchAlpha = 0.f;
	};

	/** Copies stacked behind each letter to extrude it — the engraved-metal look. */
	static constexpr int32 ExtrudeLayers = 6;

	TArray<FTitleChar> Chars;
	float ElapsedTime = 0.f;

	static constexpr float CharStagger = 0.055f;
	static constexpr float CharDuration = 0.9f;
	/** How far outside its own box a letter still counts as hovered. */
	static constexpr float HoverPadding = 6.f;
	/** steps(2, end) in the web keyframes — the jitter snaps rather than slides. */
	static constexpr float JitterHz = 15.f;
};
