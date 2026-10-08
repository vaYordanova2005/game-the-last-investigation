#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WineCellarActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
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

	/** Wall centre to wall centre. */
	static constexpr float Width = 1240.f;
	static constexpr float Depth = 900.f;
	static constexpr float WallThickness = 20.f;
	/** The door in the north wall, its centre along X from the room's centre (ACellarActor::WineDoorWidth/Height). */
	static constexpr float DoorX = 490.f;
	static constexpr float DoorHalf = 55.f;
	static constexpr float DoorHeight = 212.f;

	/** The inner faces of the walls. */
	static constexpr float HalfX = Width * 0.5f - WallThickness * 0.5f;
	static constexpr float HalfY = Depth * 0.5f - WallThickness * 0.5f;
	/** The aisles' flat ceiling, the same height as the cellar's other rooms. */
	static constexpr float AisleCeiling = 305.f;

	/** Two rows of piers at +-PierY, at these X; the beams run along the rows over them. */
	static constexpr float PierY = 200.f;
	static constexpr float PierHalf = 25.f;
	static constexpr float PierTop = 270.f;
	static constexpr float BeamTop = 304.f;
	static constexpr float BeamHalf = 15.f;

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

	/** One rack placed in the room: where, which way its front faces, and whether it is a spine. */
	struct FRack
	{
		FVector Location;
		float Yaw;
		bool bSpine;
	};

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildRacks(FRoomBuilder& Build);
	void FillRack(const FRack& Rack, int32 Seed, TArray<TArray<FTransform>>& PerWine);
	void BuildEastEnd(FRoomBuilder& Build);
	void BuildTastingTable(FRoomBuilder& Build);
	void BuildAlcove(FRoomBuilder& Build);
	void BuildCorners(FRoomBuilder& Build);
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

	UPROPERTY(VisibleAnywhere, Category = "Wine Cellar")
	TObjectPtr<USceneComponent> CellarRoot;

	UPROPERTY(Transient)
	TObjectPtr<ULocalFogVolumeComponent> Mist;

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
