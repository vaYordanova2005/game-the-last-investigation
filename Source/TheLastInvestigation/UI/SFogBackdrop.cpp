#include "SFogBackdrop.h"
#include "MenuStyle.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Async/ParallelFor.h"

namespace
{
	// the GLSL hash, kept so the grain stays deterministic across threads
	float Hash2(float X, float Y)
	{
		return FMath::Frac(FMath::Sin(X * 127.1f + Y * 311.7f) * 43758.5453f);
	}

	// FBM matching the GLSL recipe in fx.js: layered noise, each octave rotated
	// and scaled by the classic smoke matrix.
	float Fbm(FVector2D P)
	{
		float Value = 0.f;
		float Amp = 0.5f;
		for (int32 i = 0; i < 5; ++i)
		{
			const float N = FMath::PerlinNoise2D(P) * 0.5f + 0.5f; // -1..1 -> 0..1
			Value += Amp * N;
			P = FVector2D(1.6f * P.X + 1.2f * P.Y, -1.2f * P.X + 1.6f * P.Y);
			Amp *= 0.5f;
		}
		return Value;
	}
}

void SFogBackdrop::Construct(const FArguments& InArgs)
{
	FogTexture = UTexture2D::CreateTransient(TexWidth, TexHeight, PF_B8G8R8A8);
	FogTexture->Filter = TF_Bilinear;
	FogTexture->AddressX = TA_Clamp;
	FogTexture->AddressY = TA_Clamp;
	FogTexture->AddToRoot();
	FogTexture->UpdateResource();

	FogBrush = MakeShared<FSlateBrush>();
	FogBrush->SetResourceObject(FogTexture);
	FogBrush->ImageSize = FVector2D(TexWidth, TexHeight);
	FogBrush->DrawAs = ESlateBrushDrawType::Image;
	FogBrush->Tiling = ESlateBrushTileType::NoTile;

	PixelScratch.SetNumUninitialized(TexWidth * TexHeight);

	Embers.SetNum(22);
	for (FEmberParticle& Ember : Embers)
	{
		RespawnEmber(Ember, /*bRandomizeHeight*/ true);
	}

	RegenerateFogTexture();
}

SFogBackdrop::~SFogBackdrop()
{
	if (FogTexture)
	{
		FogTexture->RemoveFromRoot();
	}
}

void SFogBackdrop::RespawnEmber(FEmberParticle& Ember, bool bRandomizeHeight)
{
	Ember.Pos.X = FMath::FRand();
	Ember.Pos.Y = bRandomizeHeight ? FMath::FRandRange(-0.05f, 1.05f) : 1.05f;
	Ember.DriftX = FMath::FRandRange(-0.035f, 0.035f);
	Ember.Speed = FMath::FRandRange(0.045f, 0.115f);
	Ember.Size = FMath::FRandRange(2.f, 4.5f);
	Ember.Seed = FMath::FRandRange(0.f, 100.f);
}

void SFogBackdrop::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	ElapsedTime += InDeltaTime;
	TimeSinceRegen += InDeltaTime;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X > 0.f && LocalSize.Y > 0.f)
	{
		const FVector2D CursorLocal = AllottedGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
		// eased toward the cursor, like the lerp(0.06) smoothing in fx.js
		const FVector2D Target(
			FMath::Clamp(CursorLocal.X / LocalSize.X, 0.f, 1.f),
			FMath::Clamp(1.f - CursorLocal.Y / LocalSize.Y, 0.f, 1.f));
		MouseNorm = FMath::Lerp(MouseNorm, Target, FMath::Clamp(InDeltaTime * 3.5f, 0.f, 1.f));
	}

	for (FEmberParticle& Ember : Embers)
	{
		Ember.Pos.Y -= Ember.Speed * InDeltaTime;
		Ember.Pos.X += Ember.DriftX * InDeltaTime;
		if (Ember.Pos.Y < -0.05f)
		{
			RespawnEmber(Ember, /*bRandomizeHeight*/ false);
		}
	}

	if (TimeSinceRegen >= RegenInterval)
	{
		TimeSinceRegen = 0.f;
		RegenerateFogTexture();
	}
}

void SFogBackdrop::RegenerateFogTexture()
{
	if (!FogTexture)
	{
		return;
	}

	const float T = ElapsedTime * 0.055f;
	const FVector2D Mo = (MouseNorm - FVector2D(0.5f, 0.5f)) * FVector2D(1.4f, 0.9f);
	const float GrainSeed = FMath::Frac(ElapsedTime);

	const FLinearColor Deep = MenuStyle::SmokeDeep();
	const FLinearColor Moss = MenuStyle::SmokeMoss();
	const FLinearColor Jade = MenuStyle::SmokeJade();
	const FLinearColor Gold = MenuStyle::SmokeGold();

	FColor* Out = PixelScratch.GetData();

	ParallelFor(TexHeight, [&](int32 Y)
	{
		for (int32 X = 0; X < TexWidth; ++X)
		{
			// aspect-corrected centred coords, matching (fragCoord - 0.5*res)/res.y
			const FVector2D P(
				(X - 0.5f * TexWidth) / TexHeight,
				(Y - 0.5f * TexHeight) / TexHeight);

			// domain warp — two passes, the classic smoke recipe
			const FVector2D Qv(
				Fbm(P * 1.5f + FVector2D(0.f, T)),
				Fbm(P * 1.5f + FVector2D(5.2f, 1.3f) - FVector2D(T, T)));

			const FVector2D R(
				Fbm(P * 1.5f + 3.6f * Qv + FVector2D(1.7f, 9.2f) + FVector2D(T * 1.35f, T * 1.35f) + Mo * 0.35f),
				Fbm(P * 1.5f + 3.6f * Qv + FVector2D(8.3f, 2.8f) - FVector2D(T * 1.10f, T * 1.10f) - Mo * 0.35f));

			const float F = Fbm(P * 1.6f + 3.4f * R);

			FLinearColor Col = FMath::Lerp(Deep, Moss, FMath::SmoothStep(0.20f, 0.85f, F));
			// the jade highlight is pulled right back — it should catch only the
			// brightest curls of smoke, so the room stays night-dark
			Col = FMath::Lerp(Col, Jade, FMath::SmoothStep(0.72f, 1.15f, F) * 0.35f);

			// gold veins riding the warp field — the one thing allowed to burn
			// bright, so the contrast does the drama instead of overall exposure
			const float Veins = FMath::Pow(FMath::Clamp(R.Size() / 1.45f, 0.f, 1.f), 3.5f);
			Col += Gold * Veins * 0.9f;

			// the lantern's breath — a glow that follows the cursor
			const float G = FMath::Exp(-(P - Mo * 1.15f).Size() * 2.1f);
			Col += Gold * G * 0.30f + Jade * G * 0.16f;

			// vignette
			const FVector2D Pv = P * FVector2D(0.72f, 1.0f);
			Col *= 1.f - 0.72f * FMath::Pow(FMath::Clamp(Pv.Size(), 0.f, 2.f), 2.2f);

			// ── .site-bg-veil: two stacked washes of near-black ──
			const FVector2D Uv(static_cast<float>(X) / TexWidth, static_cast<float>(Y) / TexHeight);

			// radial(85% 65% at 50% 30%, transparent 35% -> rgba(6,12,9,.72))
			const float RadialDist = FVector2D((Uv.X - 0.5f) / 0.85f, (Uv.Y - 0.3f) / 0.65f).Size();
			const float RadialVeil = FMath::SmoothStep(0.35f, 1.f, RadialDist) * 0.72f;

			// linear(top .5 -> .12 at 30% -> .55 at bottom)
			const float LinearVeil = Uv.Y < 0.3f
				? FMath::Lerp(0.5f, 0.12f, Uv.Y / 0.3f)
				: FMath::Lerp(0.12f, 0.55f, (Uv.Y - 0.3f) / 0.7f);

			Col *= (1.f - RadialVeil) * (1.f - LinearVeil);

			// film grain
			const float Grain = (Hash2(X + GrainSeed * 37.f, Y + GrainSeed * 53.f) - 0.5f) * 0.032f;
			Col += FLinearColor(Grain, Grain, Grain, 0.f);

			// The shader wrote these straight to the framebuffer, so they are
			// already display values — quantize without a gamma encode, or the
			// whole backdrop lifts into washed-out gray.
			Out[Y * TexWidth + X] = Col.GetClamped().ToFColor(/*bSRGB*/ false);
		}
	});

	FTexture2DMipMap& Mip = FogTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, PixelScratch.GetData(), PixelScratch.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	FogTexture->UpdateResource();
}

FVector2D SFogBackdrop::ComputeDesiredSize(float) const
{
	return FVector2D(64.f, 64.f);
}

int32 SFogBackdrop::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		FogBrush.Get(),
		ESlateDrawEffect::None,
		FLinearColor::White);

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FSlateBrush* GlowBrush = MenuStyle::SoftCircleBrush();

	// ── the lantern's pool of light, riding on top of the low-res smoke ──
	{
		const FVector2D CursorPixel(MouseNorm.X * LocalSize.X, (1.f - MouseNorm.Y) * LocalSize.Y);
		const FLinearColor Gold = MenuStyle::Gold();

		// two passes: a wide, faint haze and a tighter, warmer core
		const float Radii[] = { 620.f, 260.f };
		const float Alphas[] = { 0.10f, 0.13f };
		for (int32 i = 0; i < 2; ++i)
		{
			const FVector2D GlowSize(Radii[i], Radii[i]);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(GlowSize, FSlateLayoutTransform(CursorPixel - GlowSize * 0.5f)),
				GlowBrush,
				ESlateDrawEffect::None,
				FLinearColor(Gold.R, Gold.G, Gold.B, Alphas[i]));
		}
	}

	const FSlateBrush* EmberBrush = GlowBrush;
	const int32 EmberLayer = LayerId + 2;
	const FLinearColor EmberTint = MenuStyle::GoldLite();

	for (const FEmberParticle& Ember : Embers)
	{
		// fade in as it rises, out as it nears the top — the ember-rise keyframes
		const float Rise = FMath::Clamp(1.f - Ember.Pos.Y, 0.f, 1.f);
		const float Alpha = FMath::Min(FMath::SmoothStep(0.f, 0.12f, Rise), 1.f - FMath::SmoothStep(0.75f, 1.f, Rise))
			* (0.35f + 0.5f * FMath::Abs(FMath::Sin(ElapsedTime * 1.3f + Ember.Seed)));
		if (Alpha <= 0.f)
		{
			continue;
		}

		const FVector2D PixelPos(Ember.Pos.X * LocalSize.X, (1.f - Ember.Pos.Y) * LocalSize.Y);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			EmberLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(Ember.Size, Ember.Size), FSlateLayoutTransform(PixelPos)),
			EmberBrush,
			ESlateDrawEffect::None,
			FLinearColor(EmberTint.R, EmberTint.G, EmberTint.B, Alpha));
	}

	return EmberLayer;
}
