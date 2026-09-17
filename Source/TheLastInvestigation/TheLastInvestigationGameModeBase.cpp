#include "TheLastInvestigationGameModeBase.h"
#include "UI/SMainMenu.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"

void ATheLastInvestigationGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	SAssignNew(MainMenuWidget, SMainMenu)
		.OnNewGameClicked(FSimpleDelegate::CreateUObject(this, &ATheLastInvestigationGameModeBase::HandleNewGameClicked))
		.OnQuitClicked(FSimpleDelegate::CreateUObject(this, &ATheLastInvestigationGameModeBase::HandleQuitClicked));

	GEngine->GameViewport->AddViewportWidgetContent(MainMenuWidget.ToSharedRef());

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}
}

void ATheLastInvestigationGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MainMenuWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MainMenuWidget.ToSharedRef());
	}
	MainMenuWidget.Reset();

	Super::EndPlay(EndPlayReason);
}

void ATheLastInvestigationGameModeBase::HandleNewGameClicked()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("Room01")));
}

void ATheLastInvestigationGameModeBase::HandleQuitClicked()
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		UKismetSystemLibrary::QuitGame(GetWorld(), PC, EQuitPreference::Quit, false);
	}
}
