#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CellarActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UMaterialInstanceDynamic;
class ARoomDressingActor;
class AHallDoorActor;

/** Where the cellar stair leaves the stair hall, handed over by the hall that spawns it. */
struct FCellarSetup
{
	/** X of the shaft side of the end wall under the north return flight, where the door hangs. */
	float DoorWallX = -1804.f;
	/** The first riser down: the top landing runs from the door to here. */
	float TopLandingX = -1840.f;
	/** How far west the hall floor is open over the stair; past it the stair is under the floor. */
	float WellWestX = -2160.f;
	/** The stair's well, north and south faces. */
	float ShaftNorthY = -50.f;
	float ShaftSouthY = 90.f;
	/** The entrance hall's floor, which is the top of the stair. */
	float GroundZ = -340.f;
	/** The half-landing's front edge and the underside of its floor: the well's nook under it. */
	float LandingEdgeX = -2070.f;
	float LandingSoffitZ = -200.f;
	float HallWestX = -2270.f;
};

/**
 * The cellar, under the entrance hall, down a steep stair behind the door that was the cupboard
 * under the north return flight: a brick corridor running west from the foot of the stair, with
 * rooms off it on both sides.
 *
 * The first room on the right is Room01 moved downstairs: the same size, the same plaster and
 * boards, and the same furniture standing in the same places against the same walls — the
 * four-poster, the press, the wardrobe, the bookcase, the table, the clock, the dead mirror, the
 * overturned chair and every clue marker. It is an ARoomDressingActor with its contents on;
 * Room01 upstairs keeps only its walls, its window and its hook, and is to be furnished another
 * way. What the cellar bedroom does not have is the window (there is no view from under the
 * ground) and the hook with the collapse beneath it. Its door is a panelled house door.
 *
 * The other three rooms are bare cellar rooms — brick, flags, joists, damp — behind plank doors
 * that stand a crack open and give when pushed. What goes in them is not decided yet.
 *
 *                       north
 *      +-----------+ +----------------------+
 *      |  room N2  | |  bedroom (Room01's   |
 *      |  (bare)   | |  furniture)          |
 *      +---[d]-----+ +-------[opening]------+
 *      | corridor  <---------------------------- foot <-- stair, down westward -- top | door (hall)
 *      +---[d]-----+ +----[d]-------+
 *      |  room S2  | |  room S1     |
 *      |  (bare)   | |  (bare)      |
 *      +-----------+ +--------------+
 *                       south (under the hall)
 *
 * The stair runs west down a well along the hall's north wall, under the north flight and the
 * half-landing, and the corridor carries on past its foot. Shares the room's frame, like the hall,
 * so every number reads straight off -RoomShotX/Y/Z.
 */
UCLASS()
class ACellarActor : public AActor
{
	GENERATED_BODY()

public:
	ACellarActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FCellarSetup& InSetup);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Room01's shell, so its furniture stands where it stood against its walls. */
	static constexpr float RoomWidth = 800.f;
	static constexpr float RoomDepth = 650.f;
	static constexpr float RoomHeight = 305.f;
	static constexpr float WallThickness = 20.f;
	static constexpr float DoorOpeningWidth = 106.f;
	static constexpr float DoorOpeningHeight = 208.f;
	/** Along the room's +Y wall from its centre, as Room01's door is. */
	static constexpr float DoorOpeningCenterX = 205.f;
	/** The bare rooms' doorways: a plank door in each, narrower than the house's. */
	static constexpr float BareDoorWidth = 96.f;
	static constexpr float BareDoorHeight = 200.f;

	/** How far the cellar floor is under the hall's: a storey and a ceiling, and clear of its slab. */
	static constexpr float DepthBelowHall = 350.f;
	/** A cellar stair: steep, eighteen risers on a 24cm going. */
	static constexpr int32 Risers = 18;
	static constexpr float Going = 24.f;

private:
	/** One room off the corridor, north or south of it, its door wall being the corridor's wall. */
	struct FCellarRoom
	{
		float CentreX;
		float Width;
		float Depth;
		bool bNorth;
		/** The doorway's centre, in the room frame's X. */
		float DoorX;
		/** Room01's furniture, built by an ARoomDressingActor; otherwise a bare cellar room. */
		bool bBedroom;
		int32 Seed;
	};

	float FloorZ() const { return Setup.GroundZ - DepthBelowHall; }
	float Rise() const { return DepthBelowHall / Risers; }
	/** The bottom riser, where the stair meets the corridor floor. */
	float FootX() const { return Setup.TopLandingX - Going * (Risers - 1); }
	/** The bedroom's opening, a little past the foot of the stair, on the right going down. */
	float BedroomDoorX() const { return FootX() - 70.f; }
	/** Under the hall's slab: the corridor's flat ceiling, west of the well. */
	float PassageCeilingZ() const { return Setup.GroundZ - 26.f; }
	/** The inner face of the corridor's far (west) end wall. */
	float PassageWestX() const;
	TArray<FCellarRoom> Rooms() const;
	FVector RoomCentre(const FCellarRoom& Room) const;
	float DoorHalf(const FCellarRoom& Room) const { return (Room.bBedroom ? DoorOpeningWidth : BareDoorWidth) * 0.5f; }
	float DoorHeight(const FCellarRoom& Room) const { return Room.bBedroom ? DoorOpeningHeight : BareDoorHeight; }
	/** Past the cellar door: in the well, on the stair, in the corridor or in a room. */
	bool IsUnderground(const FVector& LocalPoint) const;

	void CacheMaterials(FRoomBuilder& Build);
	void BuildStairWell(FRoomBuilder& Build);
	void BuildStair(FRoomBuilder& Build);
	void BuildRoomShell(FRoomBuilder& Build, const FCellarRoom& Room);
	void BuildBareRoom(FRoomBuilder& Build, const FCellarRoom& Room);
	void SpawnBedroom(const FCellarRoom& Room);
	void SpawnDoor(const FCellarRoom& Room);

	UPROPERTY(VisibleAnywhere, Category = "Cellar")
	TObjectPtr<USceneComponent> CellarRoot;

	UPROPERTY(Transient)
	TObjectPtr<ARoomDressingActor> Bedroom;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AHallDoorActor>> Doors;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrick;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTread;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTimber;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloor;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubble;

	FCellarSetup Setup;
};
