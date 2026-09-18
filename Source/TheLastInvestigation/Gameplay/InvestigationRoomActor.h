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

	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* AddSlab(const FString& Name, const FVector& Center, const FVector& Size);
	void BuildRoom();
	void ApplySurfaceMaterial();
	void SpawnOccupants();

	/** Every slab built by AddSlab, so they can all be retinted with one dynamic material. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Surfaces;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<UExponentialHeightFogComponent> FogComponent;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	// Roughly 8m x 6.5m with a 3.4m ceiling — a large, hollow old room rather than a cell.
	UPROPERTY(EditAnywhere, Category = "Room|Layout")
	float RoomWidth = 800.f;

	UPROPERTY(EditAnywhere, Category = "Room|Layout")
	float RoomDepth = 650.f;

	UPROPERTY(EditAnywhere, Category = "Room|Layout")
	float RoomHeight = 340.f;

	UPROPERTY(EditAnywhere, Category = "Room|Layout")
	float WallThickness = 20.f;

	// Opening sizes, shared with the door, the storm and the dressing so nothing overlaps a gap.
	static constexpr float DoorOpeningWidth = 110.f;
	static constexpr float DoorOpeningHeight = 215.f;
	static constexpr float WindowOpeningWidth = 260.f;
	static constexpr float WindowSillHeight = 85.f;
	static constexpr float WindowTopHeight = 250.f;

	UPROPERTY(Transient)
	TObjectPtr<ADoorActor> Door;

	UPROPERTY(Transient)
	TObjectPtr<AKeyPickupActor> Key;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> Storm;

	UPROPERTY(Transient)
	TObjectPtr<ARoomDressingActor> Dressing;
};
