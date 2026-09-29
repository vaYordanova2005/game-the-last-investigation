#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KitchenActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AStormWindowActor;
class AClueActor;

/** Where the kitchen meets the stair hall, handed over by the hall that spawns it. */
struct FKitchenSetup
{
	/** Y of the room's south face: the hall's north wall, seen from the other side. */
	float SouthFace = -80.f;
	/** West to east: the east wall is in line with the hall's. */
	float WestX = -1990.f;
	float EastX = -1270.f;
	/** The entrance hall's floor, which this room shares. */
	float FloorZ = -340.f;
	float WallThickness = 20.f;
	/** The door from the hall, in this room's south wall. The hall hangs the door itself. */
	float DoorX = -1560.f;
	float DoorHalf = 50.f;
	float DoorHeight = 212.f;
};

/**
 * The kitchen, through the door across the entrance hall from the living room: the room the
 * house was run from, and the one that stopped mid-sentence. Supper was being got ready, somebody
 * went out to fetch the child, and nobody came back to finish it.
 *
 * One tall storey, like the living room. The plan is composed for the look from the door: the
 * long prep table in the middle of the chequered floor with a rack of pans hanging over it, the
 * two windows and the storm beyond it on the far wall with the sink under one of them, the old
 * range in its brick alcove to the left, and the painted dresser full of plates to the right.
 *
 *            north (the storm)
 *      +------ window ---------------- window ------+
 *      |  sink   counter run  plate rack   counter   |
 *      |                                             |
 *   R  |                                          D  |
 *   A  |            +---- prep table ----+        R  |
 *   N  |  (breast)  |   rack of pans     |  chair E  |  east
 *   G  |            +--------------------+        S  |
 *   E  |                                          S  |
 *      |                                          ER |
 *      |  counter, calendar, clock       [door]  larder
 *      +--------------------------------------------+
 *            south (the hall)
 *
 * Shares the room's frame, like the hall and the living room, so every number in here reads
 * straight off -RoomShotX/Y/Z. Built at BeginPlay from primitives, photographed surfaces and
 * projected damage, as the rest of the house is.
 */
UCLASS()
class AKitchenActor : public AActor
{
	GENERATED_BODY()

public:
	AKitchenActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FKitchenSetup& InSetup, AStormWindowActor* InLeadStorm);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/**
	 * West to east, south to north, and floor to ceiling. Not the hall's full length: at ten metres
	 * by nine the table was an island in a parade ground. A big house's kitchen, about seven by six,
	 * which the hall sets the west wall from (the space west of it is not part of any room).
	 */
	static constexpr float RoomWidth = 720.f;
	static constexpr float RoomDepth = 620.f;
	static constexpr float RoomHeight = 420.f;

	// The two windows in the north wall, sill just over the worktop.
	static constexpr float WindowWidth = 150.f;
	static constexpr float WindowSill = 100.f;
	static constexpr float WindowTop = 350.f;
	static constexpr float WindowSpacing = 360.f;

	// The brick chimney breast in the middle of the west wall, and the alcove the range stands in.
	static constexpr float BreastWidth = 280.f;
	static constexpr float BreastDepth = 55.f;
	static constexpr float AlcoveWidth = 170.f;
	static constexpr float AlcoveHeight = 175.f;
	/** The mantel shelf over the alcove, above the floor. */
	static constexpr float ShelfHeight = 196.f;

	/** Worktops round the walls. */
	static constexpr float CounterHeight = 90.f;
	static constexpr float CounterDepth = 62.f;

	/** The prep table: long side along X. */
	static constexpr float TableLength = 280.f;
	static constexpr float TableWidth = 116.f;
	static constexpr float TableHeight = 88.f;

private:
	enum class EWall : uint8 { North, South, East, West };

	/** A hole in one wall вЂ” or, if not through the wall, something standing against it that the finish goes round. */
	struct FOpening
	{
		EWall Wall;
		float CenterU;
		float HalfU;
		float BottomZ;
		float TopZ;
		bool bThroughWall = true;
	};

	// Plan helpers, all in the room frame. North is -Y, as everywhere in the house.
	float WestX() const { return Setup.WestX; }
	float EastX() const { return Setup.EastX; }
	float SouthY() const { return Setup.SouthFace; }
	float NorthY() const { return Setup.SouthFace - RoomDepth; }
	float MidX() const { return (Setup.WestX + Setup.EastX) * 0.5f; }
	float MidY() const { return Setup.SouthFace - RoomDepth * 0.5f; }
	float FloorZ() const { return Setup.FloorZ; }
	float CeilingZ() const { return Setup.FloorZ + RoomHeight; }
	float WindowX(int32 Index) const { return MidX() + (Index == 0 ? -0.5f : 0.5f) * WindowSpacing; }
	/** The face of the chimney breast, standing into the room off the west wall. */
	float BreastX() const { return Setup.WestX + BreastDepth; }
	float HearthY() const { return MidY(); }
	/** The middle of the prep table, a little towards the range: the room's working end. */
	FVector TableCentre() const { return FVector(MidX() - 20.f, MidY() + 10.f, FloorZ()); }
	/** The run of worktop along the south wall stops short of the door casing. */
	float SouthCounterEndX() const { return Setup.DoorX - Setup.DoorHalf - 40.f; }
	/** The dresser, built into the middle of the east wall. */
	static constexpr float DresserWidth = 300.f;
	static constexpr float DresserDepth = 52.f;
	float DresserY() const { return MidY() - 50.f; }
	/** The larder cupboard in the south-east corner, past the end of the dresser. */
	float LarderY() const { return SouthY() - 130.f; }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildWallFinish(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildRange(FRoomBuilder& Build);
	void BuildCounters(FRoomBuilder& Build);
	void BuildSink(FRoomBuilder& Build);
	void BuildDresser(FRoomBuilder& Build);
	void BuildLarder(FRoomBuilder& Build);
	void BuildTable(FRoomBuilder& Build);
	void BuildPanRack(FRoomBuilder& Build);
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
	/** The yaw that turns a prop's local +Y вЂ” the way every Poly Haven prop here faces вЂ” out of a wall. */
	static float FacingYaw(EWall Wall);

	/**
	 * A cupboard door in its own frame, hinged on one side and turned open by OpenYaw about the
	 * hinge. Hinge is on the carcass face; Along is the way the door runs from its hinge when shut,
	 * Out the way the carcass faces. Painted, with a sunk panel and a rusted handle.
	 */
	void CupboardDoor(FRoomBuilder& Build, const FVector& Hinge, const FVector& Along, const FVector& Out, float Width, float Height, float OpenYaw);
	/** A plate standing on its edge, leaning back against whatever is behind it: Out is the way it faces. */
	void StandingPlate(FRoomBuilder& Build, const FVector& Foot, const FVector& Out, float Diameter, float Lean, UMaterialInterface* Mat);
	/** A glass jar with what is left in it, standing on Base. Fill is the fraction still in it. */
	void Jar(FRoomBuilder& Build, const FVector& Base, float Diameter, float Height, float Fill, UMaterialInterface* Contents);

	/** A free-standing floor spot the scattered debris may use: off the furniture and out of the paths. */
	bool IsFloorSpotClear(float X, float Y, float Radius) const;

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description);

	UPROPERTY(VisibleAnywhere, Category = "Kitchen")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Kitchen")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> LeadStorm;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AStormWindowActor>> Windows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	/** The rack of pans over the table hangs from this, and the draught through the broken panes moves it. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> RackPivot;

	TArray<FOpening> Openings;
	/** What stands on the floor, in plan, so the scattered debris keeps out of it (IsFloorSpotClear). */
	TArray<FBox2D> Footprints;
	FKitchenSetup Setup;
	FRandomStream Random;
	float ElapsedTime = 0.f;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDistemper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrick;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTiles;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBeam;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaint;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaintDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatScrubbed;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOakDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCastIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCopper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatChina;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatChinaDusty;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaperDamp;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCloth;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShadow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSoot;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatInk;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRedInk;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRot;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRotDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMould;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPreserve;
};
