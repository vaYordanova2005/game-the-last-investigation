#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MansionExteriorActor.generated.h"

class AStormForestActor;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDirectionalLightComponent;

/**
 * The house from outside: a Victorian mansion of soot-dark brick with weathered stone dressings,
 * steep slate roofs with carved bargeboards on every gable, a round turret on the north-east
 * corner, tall chimneys, ivy on the corners, and on the east front a porch over a new black double
 * door that somebody has boarded up from outside.
 *
 * Seen only from a camera (the user's call, 2026-11-19): nothing out here collides. It is built
 * round the rooms as they stand, never into them:
 *
 *   West block   the stair hall, the living room and the kitchen with the library and the music
 *                room over them: X WestFace..BlockEastFace, Y NorthFace..SouthFace. Eaves at
 *                BlockEavesZ, a ridge north-south and a big gable at each end.
 *   East wing    the upstairs corridor and its rooms (Room01, the girl's room, the bathroom, the
 *                study): X BlockEastFace..EastFace, Y NorthFace..WingSouthFace. The rooms are on
 *                the first floor; under them a ground floor of shuttered windows, and through it,
 *                on the line of the corridor, the entrance passage from the front door to the hall
 *                (never entered: the door is shut). Two ranges of roof side by side, gables north
 *                and south, a cross gable over the entrance.
 *   Turret       round the north-east corner, three storeys and a conical roof.
 *
 * Every wall is one generated sheet a hand (Skin) proud of the rooms' outer faces, cut round
 * every storm window where it really is (read off the AStormWindowActors, so the two cannot
 * drift) and round the blind windows this actor adds. Everything generated has its UVs in
 * centimetres in world space, so the brick courses run on across every cut and every piece.
 *
 * Lighting: the exterior meshes are on lighting channels 0 and 1. A faint moonlight on channel 1
 * only, with no indirect, no fog and no translucency, lights the house from outside and nothing
 * indoors. The storm's own lights (channel 0) light it as they always did.
 *
 * Spawned and configured by AStormForestActor once it has worked out where the ground is.
 */
UCLASS()
class AMansionExteriorActor : public AActor
{
	GENERATED_BODY()

public:
	AMansionExteriorActor();

	/** Must be called before FinishSpawning. */
	void Configure(const AStormForestActor* InForest);

	// The house's outer wall faces, in the room frame (X east, Y south). Every room's walls are 20
	// thick about their centre lines, and these are the outsides of the outermost ones.
	static constexpr float EastFace = 410.f;
	static constexpr float NorthFace = -720.f;
	static constexpr float SouthFace = 1880.f;
	static constexpr float WestFace = -2290.f;
	static constexpr float WingSouthFace = 1085.f;
	static constexpr float BlockEastFace = -1250.f;

	/** How far the outside's brick stands proud of the rooms' outer faces. */
	static constexpr float Skin = 4.f;

	static constexpr float BlockEavesZ = 560.f;
	static constexpr float WingEavesZ = 440.f;
	static constexpr float TurretEavesZ = 800.f;
	/** The ground floor under the east wing, level with the stair hall's. */
	static constexpr float GroundFloorZ = -340.f;

	// The front door (Tools/make_exterior.py: DOOR_UNIT_W, DOOR_UNIT_H, WALL_T). It stands in the
	// east front on the corridor's line, and the same model stands in the stair hall's east wall.
	static constexpr float DoorUnitWidth = 176.f;
	static constexpr float DoorUnitHeight = 262.f;
	static constexpr float FrontDoorY = 440.f;

	/** The front door model's slots, for anybody placing it (the stair hall does). */
	static void DressFrontDoor(UStaticMeshComponent* Door, AActor* Owner);

	/** Whether a point is inside the house's walls (the two blocks and the turret), in plan. */
	static bool IsInHouse(float X, float Y, float Margin = 0.f);

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

private:
	void BuildExterior();

	UPROPERTY(VisibleAnywhere, Category = "Exterior")
	TObjectPtr<USceneComponent> ExteriorRoot;

	UPROPERTY(VisibleAnywhere, Category = "Exterior")
	TObjectPtr<UDirectionalLightComponent> Moonlight;

	/** The night sky's own faint light from overhead, for the sides the moon does not reach. */
	UPROPERTY(VisibleAnywhere, Category = "Exterior")
	TObjectPtr<UDirectionalLightComponent> SkyFill;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY(Transient)
	TObjectPtr<const AStormForestActor> Forest;
};
