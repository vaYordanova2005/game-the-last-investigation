#include "MenuStyle.h"
#include "Misc/Paths.h"

namespace MenuStyle
{
	FLinearColor Background()   { return FLinearColor(FColor(0x05, 0x05, 0x05)); }
	FLinearColor Panel()        { return FLinearColor(FColor(0x08, 0x08, 0x08)); }
	FLinearColor TextColor()    { return FLinearColor(FColor(0xd8, 0xd4, 0xcc)); }
	FLinearColor TextHover()    { return FLinearColor(FColor(0xff, 0xff, 0xff)); }
	FLinearColor TextDisabled() { return FLinearColor(FColor(0x5a, 0x58, 0x55)); }

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
		return LoadFont(TEXT("Cinzel-Bold.ttf"), 54);
	}

	FSlateFontInfo ButtonFont()
	{
		return LoadFont(TEXT("Cinzel-Regular.ttf"), 20);
	}
}
