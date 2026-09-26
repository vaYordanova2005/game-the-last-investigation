#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CorridorActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AStormWindowActor;
class AHallDoorActor;
class AClueActor;
class AGrandStaircaseActor;

/** The bedroom this corridor runs past, handed over by the room that spawns it. */
struct FCorridorSetup
{
	/** Half the room's width and depth, measured to the wall centrelines. */
	float RoomHalfWidth = 400.f;
	float RoomHalfDepth = 325.f;
	float WallThickness = 20.f;
	float Height = 305.f;
	/** The bedroom door: where along the shared wall it is, and its opening. */
	float StartDoorCenterX = 205.f;
	float StartDoorWidth = 106.f;
	float StartDoorHeight = 208.f;
};

/**
 * The upstairs corridor, outside the bedroom the detective wakes in.
 *
 * It runs along the bedroom's door wall, east to west: a window at the east end on the same
 * façade as the bedroom's, five more bedroom doors down both sides, and at the far west end an
 * archway onto the gallery of the stair hall (AGrandStaircaseActor), which it spawns. It is the same floor the story ends on, so it is built as a place the
 * player is meant to remember rather than a route between rooms — the bedroom door faces a dusty
 * mirror, somebody has laid flowers at its threshold, and the runner is worn through in front of
 * it.
 *
 * Shares the room's coordinate frame (spawned at the room's origin), so every number in here is
 * in the same space as the room's own layout. Everything is built at BeginPlay from the same
 * primitives, photographed surfaces and projected damage the bedroom uses.
 */
UCLASS()
class ACorridorActor : public AActor
{
	GENERATED_BODY()

public:
	ACorridorActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FCorridorSetup& InSetup, AStormWindowActor* InLeadStorm);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// The corridor, in the room's frame. Clear width 210 is a generous Victorian landing — room
	// for two people to pass, which is what makes a lone figure at the far end of it wrong.
	static constexpr float ClearWidth = 210.f;
	static constexpr float WestFace = -1250.f;
	static constexpr float WindowWidth = 160.f;
	static constexpr float WindowSill = 62.f;
	static constexpr float WindowTop = 272.f;
	static constexpr float HallDoorWidth = 100.f;
	static constexpr float HallDoorHeight = 212.f;
	/** The archway onto the stair hall's gallery. Wider than a door: it is the way to the stairs. */
	static constexpr float StairOpeningWidth = 180.f;
	static constexpr float StairOpeningHeight = 246.f;

private:
	enum class ESide : uint8 { North, South, East, West };

	/** A hole in one wall, in that wall's U (along) / V (up) space. */
	struct FOpening
	{
		ESide Side;
		float CenterU;
		float HalfU;
		float TopV;
		float BottomV = 0.f;
	};

	float NorthFace() const { return Setup.RoomHalfDepth + Setup.WallThickness * 0.5f; }
	float SouthFace() const { return NorthFace() + ClearWidth; }
	float EastFace() const { return Setup.RoomHalfWidth - Setup.WallThickness * 0.5f; }
	float CenterY() const { return (NorthFace() + SouthFace()) * 0.5f; }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildWallFinish(FRoomBuilder& Build);
	void BuildWainscot(FRoomBuilder& Build);
	void BuildDoorCasings(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildRunner(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildDamage(FRoomBuilder& Build);
	void BuildBackRooms(FRoomBuilder& Build);
	void BuildStairs(FRoomBuilder& Build);
	void BuildFurniture(FRoomBuilder& Build);
	void BuildPortraits(FRoomBuilder& Build);
	void BuildDebris(FRoomBuilder& Build);
	void BuildFigures(FRoomBuilder& Build);
	void BuildClues();
	void SpawnDoors();
	void SpawnWindow();
	void SpawnStairHall();

	/** Places a thin panel on a wall's corridor face. */
	void FacePanel(FRoomBuilder& Build, ESide Side, float U, float V, float SizeU, float SizeV, UMaterialInterface* Mat) const;
	/** The same panel, cut around every opening on that wall. */
	void FaceFill(FRoomBuilder& Build, ESide Side, float U0, float U1, float V0, float V1, UMaterialInterface* Mat) const;
	/** Where a decal on that wall goes and which way it projects. */
	void AimAt(ESide Side, float U, float V, float Roll, FVector& OutLocation, FRotator& OutRotation) const;
	bool IsOnOpening(ESide Side, float U, float V, float HalfU, float HalfV) const;
	/** Along-wall extent of a side, between its end walls. */
	void SideRange(ESide Side, float& OutU0, float& OutU1) const;

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description);

	UPROPERTY(VisibleAnywhere, Category = "Corridor")
	TObjectPtr<USceneComponent> CorridorRoot;

	UPROPERTY(VisibleAnywhere, Category = "Corridor")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> LeadStorm;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> Window;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AHallDoorActor>> Doors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	UPROPERTY(Transient)
	TObjectPtr<AGrandStaircaseActor> StairHall;

	/** The two shapes at the far end of the corridor that are only there while the sky is lit. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Figures;

	TArray<FOpening> Openings;
	FCorridorSetup Setup;
	FRandomStream Random;

	/** Strike count the figures were last decided on, so each strike is decided once. */
	int32 FiguresStrike = 0;
	bool bFiguresThisStrike = false;

	// Cached surfaces, same idea as the bedroom's: one instance per surface set and tint.
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaperDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPanel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPanelField;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTrim;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloorboards;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloorboardsWorn;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRoughWood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCarpet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaperDamp;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCloth;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBackRoom;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWax;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMirror;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStem;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPetal;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShadow;
};
