#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InvestigationRoomActor.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class ADoorActor;
class AKeyPickupActor;
class AStormWindowActor;
class ARoomDressingActor;

/**
 * The room's shell and its mood: floor, ceiling, four walls (one broken for the door, one for the
 * window), the volumetric fog, and the post process that fixes the exposure and the colour grade.
 *
 * Everything that makes the room *decayed* rather than merely enclosed lives in ARoomDressingActor,
 * and everything beyond the glass lives in AStormWindowActor; this actor spawns both, along with
 * the door and the key, so a level only ever needs this one actor in it.
 */
UCLASS()
class AInvestigationRoomActor : public AActor
{
	GENERATED_BODY()

public:
	AInvestigationRoomActor();

	/**
	 * Where the detective comes round, relative to the room's origin, and which way he is facing.
	 *
	 * He does not wake in the middle of the floor: he wakes in the corner furthest from the door,
	 * because that is the one place in the room from which the whole of it — wardrobe, bookcase,
	 * window, locked door — is in a single frame. The game mode places the pawn here, and the
	 * dressing keeps the spot clear of furniture and debris.
	 */
	/**
	 * Where the detective comes round, and which way he is facing when he does.
	 *
	 * He used to wake in the west corner looking down the room at the window. The corner is a
	 * bedroom now and the bed is the largest thing in the house, so waking there means waking
	 * inside it. He wakes under the window instead — on the floor, in the one part of the room
	 * the storm actually lights, which is a better place to open your eyes in any case — turned
	 * so that the locked door is in front of him. The brief is explicit that the door has to be
	 * visible from the first frame: it is the thing he is trying to reach.
	 *
	 * The bed is then behind him and to the right. Waking with your back to it is worth more than
	 * waking looking at it.
	 */
	static FVector GetWakeLocation() { return FVector(228.f, -110.f, 100.f); }
	static FRotator GetWakeRotation() { return FRotator(-7.f, 128.f, 0.f); }

	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* AddSlab(const FString& Name, const FVector& Center, const FVector& Size);
	void BuildRoom();
	void ApplySurfaceMaterial();
	void SpawnOccupants();

	/** Every slab built by AddSlab, so they can all be retinted with one dynamic material. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Surfaces;

	/**
	 * The floor slab, kept apart from the rest because it is the one piece of the shell the player
	 * ever actually sees: the boards laid on top of it have gaps between them and gaps where a
	 * board is missing, and what shows in those gaps is this.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FloorSlab;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<UExponentialHeightFogComponent> FogComponent;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	// Roughly 8m x 6.5m with a 3m ceiling — a large, hollow old room rather than a cell, but low
	// enough overhead that the ceiling is part of the picture rather than lost in the dark.
	// Visible, not editable: the shell is built in the constructor from these defaults, so an
	// edited value would only move the dressing away from the walls.
	UPROPERTY(VisibleAnywhere, Category = "Room|Layout")
	float RoomWidth = 800.f;

	UPROPERTY(VisibleAnywhere, Category = "Room|Layout")
	float RoomDepth = 650.f;

	UPROPERTY(VisibleAnywhere, Category = "Room|Layout")
	float RoomHeight = 305.f;

	UPROPERTY(VisibleAnywhere, Category = "Room|Layout")
	float WallThickness = 20.f;

	// Opening sizes, shared with the door, the storm and the dressing so nothing overlaps a gap.
	//
	// The two openings are on adjacent walls and both have to be in frame at once: the detective
	// wakes in the far corner looking at a window with a locked door to the right of it, and the
	// whole room is laid out around that one view. The window is a tall domestic sash rather than
	// the wide gap the greybox had — a narrow opening throws a sharper, more readable rectangle of
	// storm light across the door wall, and leaves wall either side of it to decay.
	static constexpr float DoorOpeningWidth = 106.f;
	static constexpr float DoorOpeningHeight = 208.f;
	/** How far along the door wall the opening sits, from the room's centre. */
	static constexpr float DoorOpeningCenterX = 205.f;
	static constexpr float WindowOpeningWidth = 196.f;
	static constexpr float WindowSillHeight = 82.f;
	static constexpr float WindowTopHeight = 262.f;

	UPROPERTY(Transient)
	TObjectPtr<ADoorActor> Door;

	UPROPERTY(Transient)
	TObjectPtr<AKeyPickupActor> Key;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> Storm;

	UPROPERTY(Transient)
	TObjectPtr<ARoomDressingActor> Dressing;
};
