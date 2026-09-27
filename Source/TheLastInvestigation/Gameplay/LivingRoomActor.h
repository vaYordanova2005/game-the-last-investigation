#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LivingRoomActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AStormWindowActor;
class AClueActor;

/** Where the living room meets the stair hall, handed over by the hall that spawns it. */
struct FLivingRoomSetup
{
	/** Y of the room's north face: the hall's south wall, seen from the other side. */
	float NorthFace = 960.f;
	/** The room runs the length of the hall, west to east. */
	float WestX = -2270.f;
	float EastX = -1270.f;
	/** The entrance hall's floor, which this room shares. */
	float FloorZ = -340.f;
	float WallThickness = 20.f;
	/** The door from the hall, in this room's north wall. */
	float DoorX = -1560.f;
	float DoorHalf = 50.f;
	float DoorHeight = 212.f;
};

/**
 * The living room, behind the parlour door off the entrance hall: the room the family sat in, and
 * the room the story ends in — the television over the fireplace is the one that switches itself
 * on at the finale.
 *
 * One tall storey, no stair. The plan is composed for the two looks the player gets from the
 * doorway: straight ahead, the grand piano in the far corner against three tall windows and the
 * storm; and turning right, the stone fireplace filling the west wall with a modern flat-screen
 * over the mantel — the newest thing in the house by forty years, and the wrongest.
 *
 *            north (door from the hall)
 *      +--------------------------------------------+
 *      |  commode            coat stand   [door]     |
 *      |                                             |
 *   F  |  armchair   +------ carpet ------+  lamp    |
 *   I  |             |                    |  table   |
 *   R  |  hearth     |   coffee table     |  sofa    |  east
 *   E  |             |                    |  (faces  |
 *      |  armchair   +--------------------+   west)  |
 *      |               small sofa                    |
 *      |                                  piano      |
 *      +------ window ---- window ---- window -------+
 *            south (the storm)
 *
 * Shares the room's frame, like the hall and the corridor, so every number in here reads straight
 * off -RoomShotX/Y/Z. Built at BeginPlay from the same primitives, photographed surfaces and
 * projected damage as the rest of the house.
 */
UCLASS()
class ALivingRoomActor : public AActor
{
	GENERATED_BODY()

public:
	ALivingRoomActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FLivingRoomSetup& InSetup, AStormWindowActor* InLeadStorm);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** North to south, and floor to ceiling: one tall storey, well over the corridor's 305. */
	static constexpr float RoomDepth = 900.f;
	static constexpr float RoomHeight = 450.f;

	// The three windows in the south wall, floor to nearly the ceiling.
	static constexpr float WindowWidth = 140.f;
	static constexpr float WindowSill = 45.f;
	static constexpr float WindowTop = 395.f;
	static constexpr float WindowSpacing = 260.f;

	// The chimney breast in the middle of the west wall, and the firebox in it.
	static constexpr float BreastWidth = 300.f;
	static constexpr float BreastDepth = 60.f;
	static constexpr float FireboxWidth = 116.f;
	static constexpr float FireboxHeight = 100.f;
	static constexpr float FireboxDepth = 46.f;
	/** The top of the mantel shelf, above the floor. */
	static constexpr float MantelHeight = 138.f;

private:
	enum class EWall : uint8 { North, South, East, West };

	/** A hole in one wall — or, if not through the wall, something standing against it that the finish goes round. */
	struct FOpening
	{
		EWall Wall;
		float CenterU;
		float HalfU;
		float BottomZ;
		float TopZ;
		bool bThroughWall = true;
	};

	// Plan helpers, all in the room frame.
	float WestX() const { return Setup.WestX; }
	float EastX() const { return Setup.EastX; }
	float NorthY() const { return Setup.NorthFace; }
	float SouthY() const { return Setup.NorthFace + RoomDepth; }
	float MidX() const { return (Setup.WestX + Setup.EastX) * 0.5f; }
	float MidY() const { return Setup.NorthFace + RoomDepth * 0.5f; }
	float FloorZ() const { return Setup.FloorZ; }
	float CeilingZ() const { return Setup.FloorZ + RoomHeight; }
	float WindowX(int32 Index) const { return MidX() + (Index - 1) * WindowSpacing; }
	/** The face of the chimney breast, standing into the room off the west wall. */
	float BreastX() const { return Setup.WestX + BreastDepth; }
	/** The fireplace is on the room's centreline, and so is everything arranged round it. */
	float HearthY() const { return MidY(); }
	/** The middle of the carpet, the coffee table and the chandelier over them. */
	float LoungeX() const { return MidX() - 155.f; }
	/** Where the lamp table stands, at the north end of the big sofa: the keys are on it. */
	FVector LampTableSeat() const { return FVector(LoungeX() + 225.f, HearthY() - 215.f, FloorZ()); }
	static constexpr float LampTableHeight = 62.f;
	/** The commode against the north wall, with the photographs on it. */
	FVector CommodeSeat() const { return FVector(WestX() + 250.f, NorthY() + 30.f, FloorZ()); }
	/** GothicCommode_01 goes in at its own size, and this is its top. */
	static constexpr float CommodeHeight = 121.2f;
	/** The covered portrait on the east wall, and the panel and the table under it. */
	float CoveredPortraitY() const { return NorthY() + 290.f; }
	/** The middle of the length of cornice gone from the east wall, which is on the floor under it. */
	float CorniceGapU() const { return SouthY() - 283.f; }

	// The piano. Its own frame: +X from the keyboard to the tail, +Y to the player's right.
	FVector PianoOrigin() const { return FVector(MidX() + 240.f, SouthY() - 270.f, FloorZ()); }
	static constexpr float PianoYaw = 46.f;
	static constexpr float PianoCaseTop = 92.f;
	/** The music desk: how far behind the keys it stands, and how far it leans back. */
	static constexpr float PianoDeskX = 16.f;
	static constexpr float PianoDeskLean = 14.f;
	/** The middle of the desk, in the piano's frame; pitch -Lean tips the desk's up towards +X. */
	FVector PianoDeskCentre() const { return FVector(PianoDeskX, 0.f, PianoCaseTop + 2.4f) + FRotator(-PianoDeskLean, 0.f, 0.f).RotateVector(FVector(0.f, 0.f, 16.f)); }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildWallFinish(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildFireplace(FRoomBuilder& Build);
	void BuildTelevision(FRoomBuilder& Build);
	void BuildSeating(FRoomBuilder& Build);
	void BuildPiano(FRoomBuilder& Build);
	void BuildWallFurniture(FRoomBuilder& Build);
	void BuildChandelier(FRoomBuilder& Build);
	void BuildDamage(FRoomBuilder& Build);
	void BuildDebris(FRoomBuilder& Build);
	void BuildClues();
	void SpawnWindows();

	/** A slab laid on a wall's room face, cut around every opening on that wall. */
	void WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud = 0.f);
	/** A box standing Depth proud of a wall's room face. */
	void WallBox(FRoomBuilder& Build, EWall Wall, float U, float Z, float SizeU, float SizeZ, float Depth, float ProudBase, UMaterialInterface* Mat);
	/** Where a decal on that wall goes and which way it projects. */
	void AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const;
	bool IsOnOpening(EWall Wall, float U, float Z, float HalfU, float HalfZ) const;
	/** The rectangle [U0,U1] x [Z0,Z1] of one wall with every opening on it cut out. */
	TArray<FBox2D> CutAround(EWall Wall, float U0, float U1, float Z0, float Z1, bool bThroughWallOnly = false) const;
	float WallFace(EWall Wall) const;
	FVector WallNormal(EWall Wall) const;
	FVector WallPoint(EWall Wall, float U, float Z, float Proud) const;
	/** The yaw that turns a prop's local +Y — the way every Poly Haven prop here faces — out of a wall. */
	static float FacingYaw(EWall Wall);

	/** A free-standing floor spot the scattered debris may use: off the furniture and out of the paths. */
	bool IsFloorSpotClear(float X, float Y, float Radius) const;

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description);

	UPROPERTY(VisibleAnywhere, Category = "Living Room")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Living Room")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> LeadStorm;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AStormWindowActor>> Windows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	/** The chandelier hangs from this, and the draught through the broken panes moves it. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> ChandelierPivot;

	TArray<FOpening> Openings;
	/** What stands on the floor, in plan, so the scattered debris keeps out of it (IsFloorSpotClear). */
	TArray<FBox2D> Footprints;
	FLivingRoomSetup Setup;
	FRandomStream Random;
	float ElapsedTime = 0.f;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaperDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWainscot;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOak;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOakDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBoards;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatParquet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatParquetWorn;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCarpet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCarpetBorder;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStone;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStoneDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStoneSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSoot;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMarble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaperDamp;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatNewsprint;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDustSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWax;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShadow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCharred;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatAsh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLacquer;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIvory;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIvoryDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatEbony;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFelt;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatScreen;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlastic;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPhoto;
};
