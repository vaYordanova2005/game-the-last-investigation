#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkroomActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UDustMotesComponent;
class AClueActor;
struct FRoomSurface;

/**
 * The darkroom: the small cellar room past the maid's room, where the father developed and printed
 * the family's photographs himself. Every happy picture in the house went through these trays.
 *
 * The cellar builds its shell (brick, flags, a boarded ceiling) and hangs its plank door; this
 * actor furnishes it. A deal wet bench with a lead-lined sink along the back wall, the three trays
 * in it (developer, stop, fixer), the chemicals on a shelf over it, carboys under. The red
 * safelight hangs from the middle of the ceiling, still on, and the whole room is red with it. A dry bench on the east wall with the enlarger,
 * a box of paper and the timer, a stool in front of it. On the west wall a shelf unit of negatives
 * in shoeboxes. East of the door a glazed cabinet of his cameras, oldest to newest — a box
 * camera, a folding one, a twin-lens reflex, a rangefinder, an SLR, a digital SLR and a compact —
 * with his film in boxes, cassettes and cine reels; its right door stands open. A line across the room with the prints pegged on it to dry: the wedding, the beach,
 * the girl with her violin, the house, and two prints left too long under the lamp. In the fixer,
 * a print pulled too soon: a wet road at night, two lamps. A contact sheet on the door wall.
 *
 *                              north (earth)
 *        +-------------------------------------------------+
 *        |         [ shelf / bottles ]                     |
 *        | negs     [====== wet bench: dev stop fix ]      |
 *        | shelf                                     dry   |
 *        |                                    stool  bench |
 *        | ~~~~~~~~~~~~~~~~~ line with prints ~~~~~~~~~~~~ |
 *        |                    (safelight)                  |
 *        |                  cameras     contact        bin |
 *        +---------[door]----------------------------------+
 *                              south (cellar corridor)
 *
 * The safelight is the only light in the cellar that is not the lantern: a saturated red point
 * light hung under the middle joist, shadowed, filling the room. No storm light reaches here (the cellar's rule).
 *
 * Frame: origin at the room's centre on its floor, X east, Y south (towards the corridor),
 * translated from the cellar's frame and never turned. The door wall is at +Y.
 */
UCLASS()
class ADarkroomActor : public AActor
{
	GENERATED_BODY()

public:
	ADarkroomActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Wall centre to wall centre: the cellar's N2, as it was (static_assert in CellarActor.h). */
	static constexpr float Width = 600.f;
	static constexpr float Depth = 500.f;
	static constexpr float WallThickness = 20.f;
	static constexpr float Height = 305.f;
	/** The doorway in the south wall, its centre along X from the room's centre. */
	static constexpr float DoorX = -110.f;
	static constexpr float DoorHalf = 48.f;
	static constexpr float DoorHeight = 200.f;
	static constexpr float HalfX = Width * 0.5f - WallThickness * 0.5f;
	static constexpr float HalfY = Depth * 0.5f - WallThickness * 0.5f;

private:
	// Shared with Tools/make_darkroom.py.
	static constexpr float WetLength = 240.f;
	static constexpr float WetDepth = 62.f;
	static constexpr float WetTop = 90.f;
	static constexpr float SinkFloor = 80.f;
	static constexpr float DryLength = 150.f;
	static constexpr float DryDepth = 65.f;
	static constexpr float DryTop = 90.f;
	static constexpr float TrayWidth = 32.f;
	static constexpr float TrayDepth = 26.f;
	/** The pendant's red bowl hangs this far under its rose. */
	static constexpr float PendantDrop = 52.f;
	static constexpr float CabinetWidth = 110.f;
	static constexpr float CabinetDepth = 42.f;
	static constexpr float CabinetHeight = 200.f;
	static constexpr float CabinetDoorWidth = CabinetWidth * 0.5f - 3.f;
	/** The shelves' tops in the cabinet's own frame: the cupboard's top, then two shelves. */
	static constexpr float CabinetShelves[3] = { 86.f, 124.f, 162.f };
	// Shared with Tools/make_laundry.py (the shelf unit, the wall shelf and the peg are its models).
	static constexpr float ShelvesWidth = 100.f;
	static constexpr float ShelvesDepth = 36.f;
	static constexpr float WallShelfLength = 150.f;
	static constexpr float WallShelfDepth = 24.f;

	/** Where things stand. */
	static FVector WetBench() { return FVector(40.f, -HalfY + WetDepth * 0.5f + 2.f, 0.f); }
	static FVector WallShelf() { return FVector(20.f, -HalfY, 158.f); }
	/** The pendant's rose, on the underside of the middle joist (the joists are 52 apart from -HalfX + 30). */
	static FVector Pendant() { return FVector(-HalfX + 30.f + 52.f * 5.f, -30.f, Height - 18.f); }
	/** The camera cabinet against the door wall, east of the door (on the left as you face the door
	 *  from inside), clear of the hinge jamb; the leaf swings the other way. It faces -Y. */
	static FVector Cabinet() { return FVector(DoorX + DoorHalf + 18.f + CabinetWidth * 0.5f, HalfY - CabinetDepth * 0.5f - 1.f, 0.f); }
	/** A point in the cabinet's own frame (front +Y, turned 180 into the room) in the room's. */
	static FVector InCabinet(const FVector& P) { return Cabinet() + FVector(-P.X, -P.Y, P.Z); }
	static FVector DryBench() { return FVector(HalfX - DryDepth * 0.5f - 2.f, -40.f, 0.f); }
	static FVector Shelving() { return FVector(-HalfX + ShelvesDepth * 0.5f + 1.f, -110.f, 0.f); }
	/** The drying line across the room, wall to wall. */
	static constexpr float LineY = 30.f;
	static constexpr float LineZ = 200.f;

	enum class EWall : uint8 { North, South, East, West };
	struct FWallKeepOut
	{
		EWall Wall;
		FVector2D U;
		FVector2D Z;
	};

	void CacheMaterials(FRoomBuilder& Build);
	UMaterialInstanceDynamic* Sheet(FRoomBuilder& Build, const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale = 1.f);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildWetBench(FRoomBuilder& Build);
	void BuildDryBench(FRoomBuilder& Build);
	void BuildWestWall(FRoomBuilder& Build);
	void BuildDryingLine(FRoomBuilder& Build);
	void BuildDoorWall(FRoomBuilder& Build);
	void BuildSafelight(FRoomBuilder& Build);
	void BuildCameraCabinet(FRoomBuilder& Build);
	/** After everything that stands against a wall. */
	void BuildWallDamp(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildDust(FRoomBuilder& Build);
	void BuildCobwebs(FRoomBuilder& Build);

	static void Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	UStaticMeshComponent* Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
		std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	void Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw = 0.f);
	/** A rectangle of the darkroom_paper atlas on a plane: Normal out of its face, Up its top. */
	UStaticMeshComponent* Paper(FRoomBuilder& Build, const TCHAR* Rect, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H);
	/** A print hung on the line by a peg at its top: the picture faces the door, its plain back the wall. */
	void HangPrint(FRoomBuilder& Build, const TCHAR* Rect, float X, float W, float H, float Swing);
	void Web(FRoomBuilder& Build, const FVector& Centre, const FRotator& Rotation, const FVector2D& Size);
	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);
	void KeepWallDecalsOff(const UPrimitiveComponent* Part);
	bool WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfAlong, float HalfUp) const;
	/** The doorway and the quarter of floor the leaf sweeps (ACellarActor::SpawnDoor hangs it). */
	static bool FloorDecalReachesDoor(const FVector2D& Point, const FVector2D& HalfExtent);

	TArray<FWallKeepOut> WallKeepOuts;

	UPROPERTY(VisibleAnywhere, Category = "Darkroom")
	TObjectPtr<USceneComponent> DarkroomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Darkroom")
	TObjectPtr<UDustMotesComponent> DustMotes;

	/** The safelight's bulb: red, dim, shadowed. */
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> SafeLight;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	/** The flicker of an old bulb on a tired filament: its own stream (09-22). */
	FRandomStream FlickerRandom;
	float FlickerTarget = 1.f;
	float FlickerValue = 1.f;
	float FlickerTimer = 0.f;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBench;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLead;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBlackSteel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSteel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatEnamel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRim;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLiquid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLens;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPrintBack;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCard;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatYellow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatAmber;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCap;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubber;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBellows;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSafeGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCord;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLabel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShadow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLeather;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlastic;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSilver;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFilm;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCaseGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCabinet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShelfWood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatJoist;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLine;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> PaperMats;
};
