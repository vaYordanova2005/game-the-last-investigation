#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InvestigationRoomActor.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class ADoorActor;
class AKeyPickupActor;

/**
 * Builds the opening room entirely from engine basic-shape meshes so it needs no hand-placed
 * level geometry: floor, ceiling, four walls (one with a door gap, one with a boarded window
 * gap), plus fog/post-process mood and a flickering lightning light outside the window.
 * Spawns the door and key actors itself so the level only needs this one actor placed.
 */
UCLASS()
class AInvestigationRoomActor : public AActor
{
	GENERATED_BODY()

public:
	AInvestigationRoomActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	UStaticMeshComponent* AddSlab(const FString& Name, const FVector& Center, const FVector& Size);
	void BuildRoom();
	void ApplySurfaceMaterial();

	/** Every slab built by AddSlab, so they can all be retinted with one dynamic material. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Surfaces;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<UExponentialHeightFogComponent> FogComponent;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(VisibleAnywhere, Category = "Room")
	TObjectPtr<UPointLightComponent> LightningLight;

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

	UPROPERTY(Transient)
	TObjectPtr<ADoorActor> Door;

	UPROPERTY(Transient)
	TObjectPtr<AKeyPickupActor> Key;

	// Lightning flash timing.
	float TimeUntilNextFlash = 3.f;
	float FlashTimeRemaining = 0.f;
};
