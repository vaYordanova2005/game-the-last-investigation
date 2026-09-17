#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"

// Palette ported from fx.css :root / the fx.js smoke shader.
// The ink-green + gold scheme is kept as-is: the gold reads as the detective's
// lantern light, and the moss/jade greens give the smoke its depth.
namespace MenuStyle
{
	// ── smoke stops ──
	// NOTE: these are final display values, exactly as the GLSL wrote them to the
	// framebuffer — not linear-space colors. They must be stored to the texture
	// without a gamma encode or the whole backdrop washes out to pale gray.
	FLinearColor SmokeDeep(); // vec3(0.024, 0.052, 0.040)
	FLinearColor SmokeMoss(); // vec3(0.070, 0.215, 0.148)
	FLinearColor SmokeJade(); // vec3(0.200, 0.520, 0.360)
	FLinearColor SmokeGold(); // vec3(0.830, 0.660, 0.330)

	// ── sRGB UI colors ──
	FLinearColor Ink();       // #0a1310
	FLinearColor InkDeep();   // #060c09
	FLinearColor Gold();      // #d4a853
	FLinearColor GoldLite();  // #ffeab0
	FLinearColor GoldDark();  // #8a6420 — bottom of the title gradient, used as outline

	// glitch ghosts — the chromatic split that fires under the pointer
	FLinearColor GlitchBlue();
	FLinearColor GlitchRed();

	// ── text ──
	FLinearColor TextColor();  // rgba(238,244,232,.88)
	FLinearColor MutedColor(); // rgba(214,226,205,.6)

	// ── fonts ──
	FSlateFontInfo TitleFont();
	FSlateFontInfo ButtonFont();
	FSlateFontInfo SubtitleFont();

	/** Slate has no letter-spacing, so tracked text is spelled out with gaps. */
	FString Tracked(const FString& In);

	/** A soft radial sprite — the lantern's light pool and the cursor dot. */
	const FSlateBrush* SoftCircleBrush();
}
