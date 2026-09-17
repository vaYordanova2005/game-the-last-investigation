#include "RoomGameModeBase.h"
#include "DetectiveCharacter.h"
#include "RoomHUD.h"
#include "InvestigationRoomActor.h"
#include "GameFramework/PlayerController.h"
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
}

void ARoomGameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// The room must exist before the pawn is positioned: PostLogin (and so this) runs before the
	// GameMode's own BeginPlay, so spawning the room only there left the player briefly standing
	// on nothing — long enough to start falling and end up under the floor once it appeared.
	EnsureRoomSpawned();

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	// The room is always built at the world origin, so the player is placed there directly —
	// this makes correct spawn placement independent of wherever the level's PlayerStart was
	// hand-dragged to, which is otherwise an easy thing to get wrong.
	if (APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr)
	{
		Pawn->SetActorLocationAndRotation(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator);
		NewPlayer->SetControlRotation(FRotator::ZeroRotator); // bUseControllerRotationYaw drives facing from this, not the actor's own rotation
		UE_LOG(LogTemp, Log, TEXT("Room01: player placed at %s"), *Pawn->GetActorLocation().ToString());
	}
}
