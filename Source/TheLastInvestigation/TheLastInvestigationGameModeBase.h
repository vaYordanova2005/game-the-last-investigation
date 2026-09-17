#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TheLastInvestigationGameModeBase.generated.h"

class SMainMenu;

UCLASS()
class ATheLastInvestigationGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleNewGameClicked();
	void HandleQuitClicked();

	TSharedPtr<SMainMenu> MainMenuWidget;
};
