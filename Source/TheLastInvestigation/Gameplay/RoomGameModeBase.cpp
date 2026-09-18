#include "RoomGameModeBase.h"
#include "DetectiveCharacter.h"
#include "RoomHUD.h"
#include "InvestigationRoomActor.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/Pawn.h"

ARoomGameModeBase::ARoomGameModeBase()
{
	DefaultPawnClass = ADetectiveCharacter::StaticClass();
	HUDClass = ARoomHUD::StaticClass();
}

void ARoomGameModeBase::EnsureRoomSpawned()
{
	if (!RoomActor)
	{
		RoomActor = GetWorld()->SpawnActor<AInvestigationRoomActor>(AInvestigationRoomActor::StaticClass(), FTransform::Identity);
	}
}

void ARoomGameModeBase::BeginPlay()
{
	Super::BeginPlay();
	EnsureRoomSpawned();
	ScheduleHeadlessScreenshot();
}

void ARoomGameModeBase::ScheduleHeadlessScreenshot()
{
	// The project has no Editor GUI in its workflow, which leaves no way to *look* at the room
	// while working on it — and the room is almost entirely a lighting and dressing problem, so
	// not looking at it is not an option. This runs the game for real, waits for the shader
	// compiler and Lumen to settle (the first frames are the default material and a black
	// screen), takes a full-resolution shot and quits:
	//
	//     UnrealEditor-Cmd.exe <uproject> Room01 -game -windowed -resx=1600 -resy=900 -RoomShot=25
	//
	// Does nothing at all without the switch, so it cannot affect a normal run.
	float ShotDelay = 0.f;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-RoomShot="), ShotDelay) || ShotDelay <= 0.f)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Room01: headless screenshot in %.1fs"), ShotDelay);

	FTimerHandle ShotTimer;
	GetWorldTimerManager().SetTimer(ShotTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		// Put the camera back where the detective woke up. The window has been taking mouse input
		// for the whole delay, so without this the shot is of whatever the desk mouse last did.
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->SetActorLocationAndRotation(AInvestigationRoomActor::GetWakeLocation(), FRotator::ZeroRotator);
			}
			PC->SetControlRotation(AInvestigationRoomActor::GetWakeRotation());
		}

		// FScreenshotRequest, not GEngine->Exec("HighResShot"): that console command is handled by
		// the game viewport client, and an Exec routed through GEngine never reaches it — it is
		// swallowed silently, which looks exactly like a screenshot that was taken and lost.
		FScreenshotRequest::RequestScreenshot(/*bShowUI*/ false);

		// A screenshot is written at the end of the frame after the one that requested it, so the
		// quit has to wait a beat or the file is truncated or never written at all.
		FTimerHandle QuitTimer;
		GetWorldTimerManager().SetTimer(QuitTimer, FTimerDelegate::CreateWeakLambda(this, []()
		{
			FPlatformMisc::RequestExit(/*Force*/ false);
		}), 3.f, false);
	}), ShotDelay, false);
}

void ARoomGameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// The room must exist before the pawn is positioned: PostLogin (and so this) runs before the
	// GameMode's own BeginPlay, so spawning the room only there left the player briefly standing
	// on nothing — long enough to start falling and end up under the floor once it appeared.
	EnsureRoomSpawned();

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	// The room is always built at the world origin, so the player is placed relative to it
	// directly — this makes correct spawn placement independent of wherever the level's
	// PlayerStart was hand-dragged to, which is otherwise an easy thing to get wrong. The room
	// owns the spot and the facing, because the whole layout is composed around that one view.
	if (APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr)
	{
		Pawn->SetActorLocationAndRotation(AInvestigationRoomActor::GetWakeLocation(), FRotator::ZeroRotator);
		// bUseControllerRotationYaw drives facing from this, not the actor's own rotation. The
		// slight downward pitch is a man opening his eyes on the floor, not a camera on a tripod.
		NewPlayer->SetControlRotation(AInvestigationRoomActor::GetWakeRotation());
		UE_LOG(LogTemp, Log, TEXT("Room01: player placed at %s"), *Pawn->GetActorLocation().ToString());
	}
}
