#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MasterBedroomActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AClueActor;

/** The shell the bedroom is fitted into, handed over by Room01 (AInvestigationRoomActor). */
struct FMasterBedroomSetup
{
	float Width = 800.f;
	float Depth = 650.f;
	float Height = 305.f;
	float WallThickness = 20.f;
	/** The door, in the south wall (+Y). */
	float DoorCenterX = 205.f;
	float DoorWidth = 106.f;
	float DoorHeight = 208.f;
	/** The window, in the east wall (+X), centred on Y = 0. */
	float WindowWidth = 196.f;
	float WindowSill = 82.f;
	float WindowTop = 262.f;
	/** Where the detective comes round: kept clear of everything. */
	FVector2D WakeSpot = FVector2D(228.f, -110.f);
};

/**
 * The master bedroom: Room01, the room the detective wakes in and the room the story ends in.
 *
 * It was done over ten or twenty years before the house was shut, and it is the one room that
 * says who the house belonged to. The Victorian shell is kept — the tall sash window on the
 * storm, the panelled door, a plaster cornice — and dressed in white: painted panel mouldings
 * on the walls over a dado rail, a white ceiling with a flush light, dark oak boards. Into it
 * went modern things: a king-size bed with a tall upholstered headboard, built-in wardrobes in
 * matte white, a white dresser, a big white television over a floating cabinet, a white armchair
 * by the window turned to the storm. All of it is under an even dust, and nothing in it is broken:
 * life in here simply stopped. It is kept deliberately sparse, with the floor in the middle of the
 * room open, because the story will need that floor later.
 *
 *                        north
 *      +-- nightstand [ BED head ] nightstand ---- dresser ----- plant --+
 *      |                [  bed   ]                  clock over           |
 *  W   |  wardrobes     [  foot  ]                                       |  window
 *  E   |  (west wall,      bench                          wake *         | (east, the
 *  S   |   one open)                                        armchair  ottoman storm)
 *  T   |  suitcase            (hook)                 lamp  side table    |
 *      |                                                                 |
 *      +----------- TV over the cabinet ------- MIRROR [door] -----------+
 *                        south (the corridor)
 *
 * Built in Room01's own frame (the actor stands at the room's centre on the floor), so every
 * number in here reads straight off -RoomShotX/Y/Z. The furniture is generated in Blender by
 * Tools/make_bedroom.py, the pictures baked by Tools/make_bedroom_art.py; the floor is Poly
 * Haven's wood_floor, the walls and ceiling painted in bedroom_paint.
 */
UCLASS()
class AMasterBedroomActor : public AActor
{
	GENERATED_BODY()

public:
	AMasterBedroomActor();

	/** Must be called before BeginPlay (deferred spawn). */
	void Configure(const FMasterBedroomSetup& InSetup);

	virtual void BeginPlay() override;

	// Shared with Tools/make_bedroom.py.
	static constexpr float BedLength = 216.f;
	static constexpr float BedWidth = 206.f;
	static constexpr float MattressTop = 58.f;
	static constexpr float NightstandTop = 58.f;
	static constexpr float BenchTop = 46.f;
	static constexpr float DresserTop = 84.f;
	static constexpr float MediaHeight = 36.f;
	static constexpr float WardrobeModule = 100.f;
	static constexpr float WardrobeDepth = 62.f;
	static constexpr float WardrobeHeight = 290.f;
	static constexpr float SideTableTop = 56.f;

	/** The painted joinery on the walls. */
	static constexpr float DadoZ = 95.f;
	static constexpr float CorniceDrop = 34.f;

private:
	enum class EWall : uint8 { North, South, East, West };

	// The room's inner faces.
	float WestX() const { return -Setup.Width * 0.5f + Setup.WallThickness * 0.5f; }
	float EastX() const { return Setup.Width * 0.5f - Setup.WallThickness * 0.5f; }
	float NorthY() const { return -Setup.Depth * 0.5f + Setup.WallThickness * 0.5f; }
	float SouthY() const { return Setup.Depth * 0.5f - Setup.WallThickness * 0.5f; }

	// Where the furniture stands, shared between the pieces and what is put on them.
	/** The bed: head to the north wall, its middle line at X = BedX. */
	static constexpr float BedX = -90.f;
	FVector BedSeat() const { return FVector(BedX, NorthY() + BedLength * 0.5f, 0.f); }
	FVector NightstandSeat(int32 Side) const { return FVector(BedX + Side * 144.f, NorthY() + 22.f, 0.f); }
	FVector BenchSeat() const { return FVector(BedX, NorthY() + BedLength + 33.f, 0.f); }
	/** The run of built-in wardrobes along the west wall, from WardrobeStartY to the south wall. */
	float WardrobeStartY() const { return SouthY() - WardrobeModule * 4.f; }
	FVector WardrobeSeat(int32 Module) const { return FVector(WestX() + WardrobeDepth * 0.5f, WardrobeStartY() + WardrobeModule * (Module + 0.5f), 0.f); }
	/** Which module stands with its right-hand door open. */
	static constexpr int32 OpenWardrobe = 2;
	FVector DresserSeat() const { return FVector(215.f, NorthY() + 24.f, 0.f); }
	/** The cabinet and the television over it, on the south wall, across from the bed. */
	FVector MediaSeat() const { return FVector(BedX, SouthY() - 20.f, 30.f); }
	FVector TelevisionSeat() const { return FVector(BedX, SouthY(), 92.f); }
	/** The black mirror leaning on the south wall beside the door. */
	FVector MirrorSeat() const { return FVector(Setup.DoorCenterX - Setup.DoorWidth * 0.5f - 72.f, SouthY() - 18.5f, 0.f); }
	/** The armchair by the window, turned to the storm, and what stands round it. */
	FVector ArmchairSeat() const { return FVector(275.f, 40.f, 0.f); }
	static constexpr float ArmchairYaw = -109.f;
	FVector SideTableSeat() const { return FVector(312.f, 85.f, 0.f); }
	/** On the window side of the table, out of the waking view: behind the chair it stood across the door. */
	FVector FloorLampSeat() const { return FVector(352.f, 150.f, 0.f); }
	FVector OttomanSeat() const { return FVector(335.f, 19.f, 0.f); }
	FVector PlantSeat() const { return FVector(350.f, -272.f, 0.f); }
	/** The ceiling hook: where it always was, in the middle of the waking view. */
	FVector HookAnchor() const { return FVector(95.f, 60.f, Setup.Height); }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildWalls(FRoomBuilder& Build);
	void BuildJoinery(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildHook(FRoomBuilder& Build);
	void BuildBed(FRoomBuilder& Build);
	void BuildWardrobes(FRoomBuilder& Build);
	void BuildDresser(FRoomBuilder& Build);
	void BuildTelevision(FRoomBuilder& Build);
	void BuildMirror(FRoomBuilder& Build);
	void BuildSittingCorner(FRoomBuilder& Build);
	void BuildFloorThings(FRoomBuilder& Build);
	void BuildDamage(FRoomBuilder& Build);
	void BuildClues();

	/** Places a generated model and fills its named slots. Never collides; see Blocker. */
	UStaticMeshComponent* Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
		std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	void Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots) const;
	/** Collision only a walking man meets: the interaction trace and the camera pass through. */
	void Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw);
	/** Dust settled on a flat top: a soft projected bloom, aimed down from four over it. */
	void Dust(FRoomBuilder& Build, const FVector& Top, const FVector2D& Size, float Yaw, float Opacity = 0.4f) const;

	// Wall-space helpers: U runs along a wall (X on north/south, Y on east/west), Z is height, and
	// a wall's normal points into the room.
	float WallFace(EWall Wall) const;
	FVector WallNormal(EWall Wall) const;
	FVector WallPoint(EWall Wall, float U, float Z, float Proud) const;
	/** The yaw that turns a model's local +Y — the way every model here faces — out of a wall. */
	static float FacingYaw(EWall Wall);
	/** The openings on a wall, as (along, up) rectangles, with their trim. */
	TArray<FBox2D> Openings(EWall Wall, float Trim) const;
	/** A straight run of moulding (bedroom_panel_moulding, _dado, _skirting) on a wall, along U or up Z. */
	void Moulding(FRoomBuilder& Build, const TCHAR* Name, EWall Wall, float U, float Z, float Length, bool bVertical);

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);

	UPROPERTY(VisibleAnywhere, Category = "Bedroom")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Bedroom")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	FMasterBedroomSetup Setup;
	FRandomStream Random;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWall;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloor;
	/** White paint on the generated models' own UVs (furniture and joinery), and on the builder's boxes. */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaint;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaintBox;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatInside;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatUpholstery;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLinen;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMattress;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatThrow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShade;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShirt;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShirtBlue;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDress;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatKnit;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLining;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRug;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOak;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPictures;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	/** Flat colours by name: steel, brass, black glass, the mirror, ceramic, leather. */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> Flats;

	/** A named flat colour from Flats. */
	UMaterialInterface* F(const TCHAR* Name) const;
};
