#include "MenuStyle.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"

namespace MenuStyle
{
	FLinearColor SmokeDeep() { return FLinearColor(0.024f, 0.052f, 0.040f); }
	FLinearColor SmokeMoss() { return FLinearColor(0.070f, 0.215f, 0.148f); }
	FLinearColor SmokeJade() { return FLinearColor(0.200f, 0.520f, 0.360f); }
	FLinearColor SmokeGold() { return FLinearColor(0.830f, 0.660f, 0.330f); }

	FLinearColor Ink()      { return FLinearColor(FColor(0x0a, 0x13, 0x10)); }
	FLinearColor InkDeep()  { return FLinearColor(FColor(0x06, 0x0c, 0x09)); }
	FLinearColor Gold()     { return FLinearColor(FColor(0xd4, 0xa8, 0x53)); }
	FLinearColor GoldLite() { return FLinearColor(FColor(0xff, 0xea, 0xb0)); }
	FLinearColor GoldDark() { return FLinearColor(FColor(0x8a, 0x64, 0x20)); }

	FLinearColor GlitchBlue() { return FLinearColor(FColor(0x4d, 0xc4, 0xff)); }
	FLinearColor GlitchRed()  { return FLinearColor(FColor(0xff, 0x3b, 0x5c)); }

	FLinearColor TextColor()  { return FLinearColor(238.f / 255.f, 244.f / 255.f, 232.f / 255.f, 0.88f); }
	FLinearColor MutedColor() { return FLinearColor(214.f / 255.f, 226.f / 255.f, 205.f / 255.f, 0.6f); }

	// Cinzel (SIL Open Font License, free for commercial use) loaded straight off
	// disk — no editor import step, so the fonts stay plain files in the repo.
	// Each face is built once and cached; the raw-path FSlateFontInfo constructor
	// is deprecated, so this goes through FStandaloneCompositeFont instead.
	static FSlateFontInfo LoadFont(const TCHAR* FileName, float Size)
	{
		static TMap<FString, TSharedPtr<FCompositeFont>> Cache;

		const FString Key(FileName);
		if (const TSharedPtr<FCompositeFont>* Found = Cache.Find(Key))
		{
			return FSlateFontInfo(*Found, Size);
		}

		const FString Path = FPaths::ProjectContentDir() / TEXT("Fonts") / FileName;
		TSharedPtr<FCompositeFont> Font = MakeShared<FStandaloneCompositeFont>(
			NAME_None, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);

		Cache.Add(Key, Font);
		return FSlateFontInfo(Font, Size);
	}

	FSlateFontInfo TitleFont()
	{
		FSlateFontInfo Font = LoadFont(TEXT("Cinzel-Black.ttf"), 62);
		// Kept to a single pixel: SKineticTitle stacks offset copies behind each
		// letter to extrude it, and a fat outline would swallow them.
		Font.OutlineSettings.OutlineSize = 1;
		Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
		return Font;
	}

	FSlateFontInfo ButtonFont()
	{
		return LoadFont(TEXT("Cinzel-Regular.ttf"), 20);
	}

	FSlateFontInfo SubtitleFont()
	{
		return LoadFont(TEXT("Cinzel-Regular.ttf"), 15);
	}

	FString Tracked(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() * 2);
		for (int32 i = 0; i < In.Len(); ++i)
		{
			if (i > 0)
			{
				// a wider gap between words than between letters
				Out.AppendChar(TEXT(' '));
				if (In[i] == TEXT(' '))
				{
					Out.AppendChar(TEXT(' '));
				}
			}
			Out.AppendChar(In[i]);
		}
		return Out;
	}

	const FSlateBrush* SoftCircleBrush()
	{
		static TSharedPtr<FSlateBrush> Brush;
		if (Brush.IsValid())
		{
			return Brush.Get();
		}

		constexpr int32 Size = 64;
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->AddToRoot();

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

		constexpr float Centre = Size * 0.5f;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const float Dist = FVector2D(X + 0.5f - Centre, Y + 0.5f - Centre).Size() / Centre;
				const float Falloff = FMath::Square(1.f - FMath::Clamp(Dist, 0.f, 1.f));
				const uint8 Alpha = static_cast<uint8>(FMath::Clamp(Falloff, 0.f, 1.f) * 255.f);

				const int32 Index = (Y * Size + X) * 4;
				Data[Index + 0] = 255; // B
				Data[Index + 1] = 255; // G
				Data[Index + 2] = 255; // R
				Data[Index + 3] = Alpha;
			}
		}

		Mip.BulkData.Unlock();
		Texture->UpdateResource();

		Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(Texture);
		Brush->ImageSize = FVector2D(Size, Size);
		Brush->DrawAs = ESlateBrushDrawType::Image;
		Brush->Tiling = ESlateBrushTileType::NoTile;
		return Brush.Get();
	}
}
