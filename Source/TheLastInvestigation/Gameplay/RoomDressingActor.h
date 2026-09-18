#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomDressingActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AClueActor;
class AStormWindowActor;
class UDustMotesComponent;

/** The shell dimensions the dressing has to fit inside, handed over by the room that spawns it. */
struct FRoomDressingSetup
{
	float Width = 800.f;
	float Depth = 650.f;
	float Height = 340.f;
	float WallThickness = 20.f;

	float DoorOpeningWidth = 110.f;
	float WindowOpeningWidth = 260.f;
	float WindowSillHeight = 85.f;
	float WindowTopHeight = 250.f;
};

/**
 * Everything inside the room that is not the shell: peeling wallpaper, rotten floorboards, the
 * water-stained ceiling, the furniture, the debris, and the traces left behind (footprints in the
 * dust, scratch marks, a stain that used to be blood).
 *
 * Split out from AInvestigationRoomActor because the two have different jobs and different
 * lifetimes — the shell is fixed geometry built in the constructor, while the dressing is
 * seeded, randomised layout built at BeginPlay. The seed is fixed, so the room is the same room
 * every time the player enters it.
 *
 * The clue objects are AClueActors, not props: examining them is how the detective reads the
 * house. Nothing here is highlighted or marked — they are placed to be *found*, by sitting where
 * a searching lantern beam naturally sweeps.
 */
UCLASS()
class ARoomDressingActor : public AActor
{
	GENERATED_BODY()

public:
	ARoomDressingActor();

	/** Must be called before BeginPlay (deferred spawn) so layout matches the shell. */
	void Configure(const FRoomDressingSetup& InSetup);

	/** The storm supplies the wind that moves the loose wallpaper and the dust. */
	void SetStorm(AStormWindowActor* InStorm) { Storm = InStorm; }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	void CacheMaterials();
	void BuildWalls();
	void BuildFloor();
	void BuildCeiling();
	void BuildFurniture();
	void BuildDebris();
	void BuildTraces();
	void BuildClues();

	/** Spawns a clue actor and returns it ready for its body to be built under GetRootScene(). */
	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description);

	/** True if a point is far enough from the door, the window and the player's spawn to drop a prop on. */
	bool IsFloorSpotClear(const FVector2D& Point, float Radius) const;

	UPROPERTY(VisibleAnywhere, Category = "Dressing")
	TObjectPtr<USceneComponent> DressingRoot;

	UPROPERTY(VisibleAnywhere, Category = "Dressing")
	TObjectPtr<UDustMotesComponent> DustMotes;

	/** Loose wallpaper strips and cobwebs, which breathe with the wind from the broken window. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> WindMovedParts;

	TArray<float> WindPartPhases;

	/** The drop that falls from the ceiling stain into the puddle, over and over. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WaterDrop;

	UPROPERTY(Transient)
	TWeakObjectPtr<AStormWindowActor> Storm;

	// Material variants, made once and shared by every part that uses them.
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaperFaded;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeilingStain;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRottenWood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDarkWood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMold;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRust;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPhoto;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCloth;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBlood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDust;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;

	FRoomDressingSetup Setup;
	FRandomStream Random;

	float ElapsedTime = 0.f;
	float DropFallTime = 0.f;
	float DropStartZ = 0.f;
	FVector DropOrigin = FVector::ZeroVector;
};
