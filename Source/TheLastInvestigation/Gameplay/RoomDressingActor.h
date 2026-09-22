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
class FRoomBuilder;

/** The shell dimensions the dressing has to fit inside, handed over by the room that spawns it. */
struct FRoomDressingSetup
{
	float Width = 800.f;
	float Depth = 650.f;
	float Height = 340.f;
	float WallThickness = 20.f;

	float DoorOpeningWidth = 106.f;
	/** Where along the door wall (+Y) the opening sits, measured from the room's centre. */
	float DoorOpeningCenterX = 170.f;
	float WindowOpeningWidth = 175.f;
	float WindowSillHeight = 86.f;
	float WindowTopHeight = 248.f;

	/** Where the detective wakes up. Nothing is dropped on top of him, and nothing blocks his view. */
	FVector2D WakeSpot = FVector2D(-300.f, -70.f);
};

/**
 * Everything inside the room that is not the shell: the surfaces (wallpaper over plaster, rotten
 * boards, stained ceiling), the furniture, the debris, and the traces left behind.
 *
 * Furniture is imported CC0 meshes; the wear, the layout and the storytelling are built around
 * them from primitives and painted marks. Split out from AInvestigationRoomActor because the two
 * have different lifetimes — the shell is fixed geometry from the constructor, this is a seeded
 * random layout built at BeginPlay. The seed is fixed, so it is the same room every time.
 *
 * The clue objects are AClueActors, not props: examining them is how the detective reads the
 * house. Nothing is highlighted — they are placed to be found, by sitting where a searching
 * lantern beam naturally sweeps.
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
	// One builder is threaded through the whole build pass: it caches material instances, and a
	// fresh one per function would make hundreds of duplicates of the same few surfaces.
	void CacheMaterials(FRoomBuilder& Build);
	void BuildWalls(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildFurniture(FRoomBuilder& Build);
	/** The corner he wakes up in: the bed, the chair beside it, and what is left on the walls. */
	void BuildBedroom(FRoomBuilder& Build);
	/** Where the bed stands, so that nothing else in the room is scattered through it. */
	void BedFootprint(FVector2D& OutCentre, FVector2D& OutHalfExtent) const;
	/**
	 * Where the nightstand stands. Shared with the clues, because the photograph lies on its top:
	 * a position worked out twice is a position that agrees until one of the two is nudged.
	 */
	FVector NightstandSeat() const;
	/** Standing water: the floor is wet where the roof and the broken pane let the storm in. */
	void BuildPuddles(FRoomBuilder& Build);
	/**
	 * What is in the bookcase, built in the bookcase's own space: it stands at an angle and leans,
	 * and placing books against that in room coordinates is what had them hanging out of the side
	 * of the carcass and through the floor.
	 */
	void BuildBookcaseContents(const FVector& Spot, const FRotator& Facing, float HeightCm);
	void BuildDebris(FRoomBuilder& Build);
	void BuildTraces(FRoomBuilder& Build);
	void BuildClues();

	/** Spawns a clue actor and returns it ready for its body to be built under GetRootScene(). */
	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description);

	/** True if a point is far enough from the door, the window and the player's spawn to drop a prop on. */
	bool IsFloorSpotClear(const FVector2D& Point, float Radius) const;

	UPROPERTY(VisibleAnywhere, Category = "Dressing")
	TObjectPtr<USceneComponent> DressingRoot;

	UPROPERTY(VisibleAnywhere, Category = "Dressing")
	TObjectPtr<UDustMotesComponent> DustMotes;

	/** Removes any clue whose body never got built, because its prop mesh was not on disk. */
	void PruneBodilessClues();

	/** Every clue spawned this pass, so PruneBodilessClues can check each one has a body. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> SpawnedClues;

	/** The cobweb strands, which breathe with the wind coming through the broken window. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> WindMovedParts;

	TArray<float> WindPartPhases;

	/** The drop that falls from the ceiling stain into the puddle, over and over. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WaterDrop;

	UPROPERTY(Transient)
	TWeakObjectPtr<AStormWindowActor> Storm;

	// Photographed surfaces, made once and shared by everything that uses them.
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaster;
	/** Brick and coarse render: what is behind the plaster, wherever the plaster has gone. */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSubstrate;
	/** The plaster once it is off the wall and on the boards — dirtier and warmer than the wall. */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloorboards;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloorboardsWorn;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRoughWood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCloth;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBedding;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRust;

	/** Paper — sheets, letters, what is left on the table — carried on the linen photograph. */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	/** The same paper where the damp has reached it: bleached of its colour and gone dark. */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaperDamp;

	// Flat tints, for the handful of things with no photographed surface of their own.
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPhoto;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBlood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWater;

	FRoomDressingSetup Setup;
	FRandomStream Random;

	float ElapsedTime = 0.f;
	float DropFallTime = 0.f;
	float DropStartZ = 0.f;
	FVector DropOrigin = FVector::ZeroVector;
};
