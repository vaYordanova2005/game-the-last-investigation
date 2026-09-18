#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RoomGameModeBase.generated.h"

class AInvestigationRoomActor;

/** GameMode for Room01 — the opening scene. Spawns the greybox room itself so the level asset only needs a PlayerStart. */
UCLASS()
class ARoomGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARoomGameModeBase();

	virtual void BeginPlay() override;

	/** Honours -RoomShot=<seconds>: waits, screenshots, quits. Art iteration without the Editor. */
	void ScheduleHeadlessScreenshot();
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

private:
	/** Spawns the room if it isn't up yet; safe to call more than once. */
	void EnsureRoomSpawned();

	UPROPERTY(Transient)
	TObjectPtr<AInvestigationRoomActor> RoomActor;
};
