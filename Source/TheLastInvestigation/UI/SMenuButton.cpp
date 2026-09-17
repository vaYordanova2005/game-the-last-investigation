#include "SMenuButton.h"
#include "MenuStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SMenuButton::Construct(const FArguments& InArgs)
{
	OnClicked = InArgs._OnClicked;
	bEnabledState = InArgs._IsEnabled;

	ChildSlot
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SBox)
		.MinDesiredWidth(300.f)
		.HeightOverride(58.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(LabelText, STextBlock)
			.Text(InArgs._Label)
			.Font(MenuStyle::ButtonFont())
			.ColorAndOpacity(FSlateColor(bEnabledState ? MenuStyle::TextColor() : MenuStyle::MutedColor()))
		]
	];

	// the menu draws its own cursor — never hand the OS pointer back
	SetCursor(EMouseCursor::None);
}

void SMenuButton::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (bEnabledState)
	{
		bHovered = true;
	}
}

void SMenuButton::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bHovered = false;
	bPressed = false;
}

FReply SMenuButton::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D Size = MyGeometry.GetLocalSize();
	if (Size.X > 0.f && Size.Y > 0.f)
	{
		LocalCursor.X = FMath::Clamp(Local.X / Size.X, 0.f, 1.f);
		LocalCursor.Y = FMath::Clamp(Local.Y / Size.Y, 0.f, 1.f);
	}
	return FReply::Unhandled();
}

FReply SMenuButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEnabledState || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMenuButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	bPressed = false;

	// only counts as a click if the pointer is still inside
	const bool bInside = MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition());
	if (bInside)
	{
		OnClicked.ExecuteIfBound();
	}

	return FReply::Handled().ReleaseMouseCapture();
}

void SMenuButton::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const float Target = bHovered ? 1.f : 0.f;
	HoverAlpha = FMath::FInterpTo(HoverAlpha, Target, InDeltaTime, 9.f);

	// a restrained take on the web design's magnetic buttons — it leans toward
	// the cursor rather than chasing it
	const FVector2D Pull = (LocalCursor - FVector2D(0.5f, 0.5f)) * FVector2D(10.f, 5.f) * HoverAlpha;
	MagneticOffset = FMath::Lerp(MagneticOffset, Pull, FMath::Clamp(InDeltaTime * 8.f, 0.f, 1.f));

	if (LabelText.IsValid())
	{
		const FLinearColor Resting = bEnabledState ? MenuStyle::TextColor() : MenuStyle::MutedColor();
		LabelText->SetColorAndOpacity(FSlateColor(FMath::Lerp(Resting, MenuStyle::GoldLite(), HoverAlpha)));

		// the label drifts with the pull, a touch further than the slab does
		LabelText->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(
			FVector2f(static_cast<float>(MagneticOffset.X * 1.4f), static_cast<float>(MagneticOffset.Y * 1.4f)))));
	}
}

int32 SMenuButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* Fill = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FVector2D Size = AllottedGeometry.GetLocalSize();

	// the slab tilts very slightly toward the cursor: a shear reads as perspective
	// at this scale, and Slate has no true 3D transform to give us rotateY
	const float TiltX = (LocalCursor.X - 0.5f) * 0.05f * HoverAlpha;
	const float TiltY = (LocalCursor.Y - 0.5f) * 0.03f * HoverAlpha;
	const FSlateRenderTransform Tilt(
		FMatrix2x2(1.f, TiltY, TiltX, 1.f),
		FVector2f(static_cast<float>(MagneticOffset.X), static_cast<float>(MagneticOffset.Y)));

	const FGeometry Tilted = AllottedGeometry.MakeChild(Size, FSlateLayoutTransform(), Tilt, FVector2D(0.5f, 0.5f));

	// ── glass slab ──
	const FLinearColor Base = MenuStyle::InkDeep();
	const float FillAlpha = bEnabledState ? FMath::Lerp(0.55f, 0.82f, HoverAlpha) : 0.4f;
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		Tilted.ToPaintGeometry(),
		Fill,
		ESlateDrawEffect::None,
		FLinearColor(Base.R, Base.G, Base.B, FillAlpha));

	// ── gold glare pooling under the cursor, as the cards did ──
	if (HoverAlpha > 0.01f && bEnabledState)
	{
		const FLinearColor Gold = MenuStyle::Gold();
		constexpr int32 GlareBands = 5;
		for (int32 i = GlareBands; i >= 1; --i)
		{
			const float Ratio = static_cast<float>(i) / GlareBands;
			const FVector2D BandSize(Size.X * Ratio, Size.Y * Ratio);
			const FVector2D BandPos(
				LocalCursor.X * Size.X - BandSize.X * 0.5f,
				LocalCursor.Y * Size.Y - BandSize.Y * 0.5f);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				Tilted.ToPaintGeometry(BandSize, FSlateLayoutTransform(BandPos)),
				Fill,
				ESlateDrawEffect::None,
				FLinearColor(Gold.R, Gold.G, Gold.B, 0.05f * HoverAlpha));
		}
	}

	// ── gold rule around the edge ──
	const FLinearColor BorderColor = bEnabledState
		? FMath::Lerp(
			FLinearColor(MenuStyle::Gold().R, MenuStyle::Gold().G, MenuStyle::Gold().B, 0.22f),
			FLinearColor(MenuStyle::Gold().R, MenuStyle::Gold().G, MenuStyle::Gold().B, 0.95f),
			HoverAlpha)
		: FLinearColor(MenuStyle::Gold().R, MenuStyle::Gold().G, MenuStyle::Gold().B, 0.1f);

	const TArray<FVector2D> Outline = {
		FVector2D(0.f, 0.f),
		FVector2D(Size.X, 0.f),
		FVector2D(Size.X, Size.Y),
		FVector2D(0.f, Size.Y),
		FVector2D(0.f, 0.f)
	};

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 2,
		Tilted.ToPaintGeometry(),
		Outline,
		ESlateDrawEffect::None,
		BorderColor,
		/*bAntialias*/ true,
		FMath::Lerp(1.f, 2.f, HoverAlpha));

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 3, InWidgetStyle, bParentEnabled);
}
