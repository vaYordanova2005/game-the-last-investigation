#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

// A menu entry drawn by hand rather than skinned from an existing SButton style:
// a translucent glass slab over the smoke, a gold rule that lights up on hover,
// and the 3D-tilt / glare treatment the web design gave its cards.
class SMenuButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuButton)
		: _IsEnabled(true)
	{}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(bool, IsEnabled)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedPtr<STextBlock> LabelText;
	FSimpleDelegate OnClicked;

	bool bEnabledState = true;
	bool bHovered = false;
	bool bPressed = false;

	/** Eased 0..1 hover weight — everything visual is driven off this. */
	float HoverAlpha = 0.f;
	/** Pointer position within the button, 0..1 on each axis. */
	FVector2D LocalCursor = FVector2D(0.5f, 0.5f);
	FVector2D MagneticOffset = FVector2D::ZeroVector;
};
