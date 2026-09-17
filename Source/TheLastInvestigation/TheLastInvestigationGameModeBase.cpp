#include "TheLastInvestigationGameModeBase.h"
#include "UI/SMainMenu.h"
#include "Kismet/KismetSystemLibrary.h"
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
	// TODO: OpenLevel to the room once it exists — next step on the roadmap.
	UE_LOG(LogTemp, Log, TEXT("New Game pressed — no playable level yet."));
}

void ATheLastInvestigationGameModeBase::HandleQuitClicked()
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		UKismetSystemLibrary::QuitGame(GetWorld(), PC, EQuitPreference::Quit, false);
	}
}
