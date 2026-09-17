#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Engine/Texture2D.h"

// Animated smoke backdrop — a CPU-side port of the domain-warped FBM "oracle
// smoke" WebGL shader from fx.js, rewritten as a small runtime-generated
// texture (no Material Editor graph needed). Keeps the original moss/jade
// greens and gold veins; the cursor glow reads as the lantern being held up.
class SFogBackdrop : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFogBackdrop) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFogBackdrop();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override;

private:
	struct FEmberParticle
	{
		FVector2D Pos = FVector2D::ZeroVector; // normalized: x 0..1, y 1(bottom)..0(top)
		float DriftX = 0.f;
		float Speed = 0.f;
		float Size = 3.f;
		float Seed = 0.f;
	};

	void RegenerateFogTexture();
	void RespawnEmber(FEmberParticle& Ember, bool bRandomizeHeight);

	static constexpr int32 TexWidth = 224;
	static constexpr int32 TexHeight = 126;
	static constexpr float RegenInterval = 1.f / 12.f;

	UTexture2D* FogTexture = nullptr;
	TSharedPtr<FSlateBrush> FogBrush;
	TArray<FColor> PixelScratch;

	TArray<FEmberParticle> Embers;

	float ElapsedTime = 0.f;
	float TimeSinceRegen = 0.f;
	FVector2D MouseNorm = FVector2D(0.5f, 0.5f);
};
