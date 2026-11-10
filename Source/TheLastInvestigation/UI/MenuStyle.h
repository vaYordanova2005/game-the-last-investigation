#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"

// Placeholder UI style: plain black and off-white until the menu gets its own
// design. Shared by the main menu and the in-game interaction prompt.
namespace MenuStyle
{
	// ── colors (sRGB) ──
	FLinearColor Background(); // the menu's backdrop
	FLinearColor Panel();      // the prompt's plate, alpha applied by the caller
	FLinearColor TextColor();
	FLinearColor TextHover();
	FLinearColor TextDisabled();

	// ── fonts ──
	FSlateFontInfo TitleFont();
	FSlateFontInfo ButtonFont();
}
