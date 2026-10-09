#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WineCellarActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ULocalFogVolumeComponent;
class AClueActor;

/**
 * The wine cellar: the family's private collection, under the ground west of the house, at the
 * end of the cellar corridor behind an iron-bound oak door.
 *
 * A nave runs the length of the room under a brick barrel vault, carried on two rows of stone
 * piers and dark oak beams; either side of it the aisles are low and flat-ceilinged, and between
 * each pair of piers a bay opens off the nave lined floor to ceiling with racks on three sides —
 * the narrow aisles a cellar is walked in. The long oak tasting table stands in the middle of the
 * nave, where the room is open. At the far (west) end, through a stone arch, is the tasting alcove:
 * two leather armchairs facing each other across a small round table, a bookcase of wine books
 * and journals between them, a stopped clock over it. Two glasses and an opened bottle are still on
 * the table. The east end, by the door, has the barrels and the crates; the corners past the piers
 * the cabinet the best bottles were kept in, and the table the cellar book was written up at.
 *
 *                               north (corridor)
 *        +-------------------------------------------[door]--+
 *        | cabinet |  bay  |  bay  |  bay  |  entrance       |
 *        |      [pier]===[pier]===[pier]===[pier]   lantern  |
 *        | alcove  A  (                                ) barrels
 *        | shelf  ( ) arch       tasting table             barrels
 *        |  clock   B  (                                ) barrels
 *        |      [pier]===[pier]===[pier]===[pier]  crates    |
 *        | desk    |  bay  |  bay  |  bay  |  shelving       |
 *        +---------------------------------------------------+
 *                                 south
 *
 * Lit by nothing: the lantern the detective carries is the only light down here. No window — the
 * storm getting into the cellar was a bug fixed twice, and this room keeps it out.
 *
 * Frame: origin at the room's centre on its floor, X east, Y south (away from the corridor),
 * translated from the cellar's frame and never turned. The door wall is at -Y.
 */
UCLASS()
class AWineCellarActor : public AActor
{
	GENERATED_BODY()

public:
	AWineCellarActor();

	virtual void BeginPlay() override;

	/** Wall centre to wall centre. The wall thickness is the cellar's (static_assert in CellarActor.h). */
	static constexpr float Width = 1240.f;
	static constexpr float Depth = 900.f;
	static constexpr float WallThickness = 20.f;
	/** The door in the north wall, its centre along X from the room's centre. The source of the
	 *  cellar's WineDoorWidth/WineDoorHeight, which hang the door in it. */
	static constexpr float DoorX = 490.f;
	static constexpr float DoorHalf = 55.f;
	static constexpr float DoorHeight = 212.f;

	/** The inner faces of the walls. */
	static constexpr float HalfX = Width * 0.5f - WallThickness * 0.5f;
	static constexpr float HalfY = Depth * 0.5f - WallThickness * 0.5f;
	/** The aisles' flat ceiling, the same height as the cellar's other rooms (ACellarActor::RoomHeight,
	 *  static_assert in CellarActor.h): the corridor's wall over the door wall starts there. */
	static constexpr float AisleCeiling = 305.f;

	/** Two rows of piers at +-PierY, at these X; the beams run along the rows over them. */
	static constexpr float PierY = 200.f;
	static constexpr float PierHalf = 25.f;
	static constexpr float PierTop = 270.f;
	static constexpr float BeamTop = 304.f;
	static constexpr float BeamHalf = 15.f;

	/**
	 * The vault's inner face (Tools/make_wine_cellar.py: VAULT_*): a segmental arc springing off
	 * the beams at +-VaultHalf and rising to the crown, i.e. a circle about X through
	 * (0, VaultCentreZ) of radius VaultRadius (~209.7, centre ~205.3).
	 */
	static constexpr float VaultHalf = 185.f;
	static constexpr float VaultSpring = BeamTop;
	static constexpr float VaultCrown = 415.f;
	static constexpr float VaultRadius = (VaultHalf * VaultHalf + (VaultCrown - VaultSpring) * (VaultCrown - VaultSpring)) / (2.f * (VaultCrown - VaultSpring));
	static constexpr float VaultCentreZ = VaultCrown - VaultRadius;

	/** The racks' cell grid (Tools/make_wine_cellar.py: RACK_*). */
	static constexpr float RackPitch = 12.5f;
	static constexpr int32 RackCols = 14;
	static constexpr int32 RackRows = 22;
	static constexpr float RackPlinth = 8.f;
	static constexpr float RackSlat = 1.2f;
	static constexpr float RackInner = RackCols * RackPitch * 0.5f;
	static constexpr float RackBackDepth = 30.f;
	static constexpr float RackSpineDepth = 60.f;
	static constexpr float BottleRadius = 3.7f;
	static constexpr float BottleLength = 30.12f;

private:
	static const float PierXs[4];
	/** The bays between the piers, at the middle of each (the racks round them, the webs in their corners). */
	static const float BayXs[3];

	/** The long table (make_wine_cellar.py: tasting_table): centred on the nave's axis at
	 *  TastingTableX, its six legs at +-LegX and 0 along it, +-LegY across. */
	static constexpr float TastingTableX = 20.f;
	static constexpr float TastingTableTop = 78.f;
	static constexpr float TastingTableLegX = 130.f;
	static constexpr float TastingTableLegY = 38.f;

	/** The tasting alcove: the armchairs at AlcoveX, +-AlcoveChairY, facing each other across the
	 *  table and each turned AlcoveChairTurn degrees to the room. */
	static constexpr float AlcoveX = -500.f;
	static constexpr float AlcoveChairY = 98.f;
	static constexpr float AlcoveChairTurn = 12.f;
	/** One armchair, Side -1 the north one: where it stands, and its yaw. */
	static FVector AlcoveChair(float Side, float& OutYaw);
	/** The little round table between them. */
	static FVector AlcoveTable() { return FVector(AlcoveX - 5.f, 0.f, 0.f); }

	/** One rack placed in the room: where, which way its front faces, and whether it is a spine. */
	struct FRack
	{
		FVector Location;
		float Yaw;
		bool bSpine;
	};

	/** The four walls, by the way their inner faces look: north is the door wall. */
	enum class EWall : uint8 { North, South, East, West };

	/** Something standing against a wall, as a rectangle on it: U along it (X on the north and
	 *  south walls, Y on the east and west), Z up it. */
	struct FWallKeepOut
	{
		EWall Wall;
		FVector2D U;
		FVector2D Z;
	};

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildRacks(FRoomBuilder& Build);
	void FillRack(const FRack& Rack, int32 Seed, TArray<TArray<FTransform>>& PerWine);
	void BuildEastEnd(FRoomBuilder& Build);
	void BuildTastingTable(FRoomBuilder& Build);
	void BuildAlcove(FRoomBuilder& Build);
	void BuildCorners(FRoomBuilder& Build);
	/** Damp, moss and cracks on the walls and the vault's weeping. After everything that stands
	 *  against a wall, since a wall decal has to know what it would land on. */
	void BuildWallDamp(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildCobwebs(FRoomBuilder& Build);
	void BuildMist();

	/** Sets every named slot the mesh has. */
	static void Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	/** A placed generated prop with its slots dressed, no collision (blockers are separate). */
	UStaticMeshComponent* Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
		std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	/** A bottle: the glass, the capsule and the label are the bottling's. */
	UStaticMeshComponent* Bottle(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation, int32 Wine);
	UStaticMeshComponent* Glass(FRoomBuilder& Build, const FVector& Location, const FRotator& Rotation, bool bDregs);
	/** Collision only a walking man meets, as everywhere in the cellar. */
	void Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw = 0.f);
	/** A cell of the paper atlas (Tools/make_cellar_art.py) laid on a plane: Normal out of it, Up its top. */
	UStaticMeshComponent* Paper(FRoomBuilder& Build, int32 Cell, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H);
	/** A vineyard's brand burned into a crate's end board: a masked plane from the brands atlas. */
	void Brand(FRoomBuilder& Build, int32 Cell, const FVector& Centre, const FVector& Normal, float W);
	/** A cobweb across three points, as a plane through them. */
	void Web(FRoomBuilder& Build, const FVector& Centre, const FRotator& Rotation, const FVector2D& Size);
	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);

	/**
	 * Records Part's footprint on every wall it stands within a wall decal's reach of, so the damp
	 * and the cracks keep off it. Read off its bounds after it is placed rather than written out a
	 * second time, so a prop that is moved takes its keep-out with it.
	 */
	void KeepWallDecalsOff(const UPrimitiveComponent* Part);
	/** The same for every part of an actor (a clue's body). */
	void KeepWallDecalsOff(const AActor* Actor);
	/** Whether a wall decal centred at U along Wall and Z up it, reaching HalfAlong and HalfUp
	 *  either way, would land on something standing against it, or on the doorway. */
	bool WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfAlong, float HalfUp) const;
	/**
	 * Whether a floor decal centred at Point, reaching HalfExtent along X and Y, would reach the
	 * doorway — the sill, the reveal, the stone surround's foot — or the quarter of the floor the
	 * leaf sweeps as it opens (ACellarActor::SpawnDoor hangs it). The cellar's 10-05 rule.
	 */
	static bool FloorDecalReachesDoor(const FVector2D& Point, const FVector2D& HalfExtent);

	TArray<FWallKeepOut> WallKeepOuts;

	UPROPERTY(VisibleAnywhere, Category = "Wine Cellar")
	TObjectPtr<USceneComponent> CellarRoot;

	/** The ground mist, in overlapping pieces: a local fog volume is always a sphere (see BuildMist). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULocalFogVolumeComponent>> Mist;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWall;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCorridorBrick;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFlags;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStone;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStoneSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVaultSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBeam;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBoards;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOak;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRackOak;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLeather;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLeatherBox;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDeal;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMarble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCrystal;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCrystalDusty;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatResidue;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWax;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWick;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCork;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWineStain;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFelt;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatInk;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTag;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShadow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGilt;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPages;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLabel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> MatBooks;
	/** The bottles' glass: dark green, and the brown some bottlings came in. */
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> MatBottleGlass;
	/** Foil and wax over the corks: red foil, black wax, gold foil, burgundy wax, lead. */
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> MatCapsules;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> PaperMats;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> BrandMats;
};
