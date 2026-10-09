#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LaundryActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ULocalFogVolumeComponent;
class UDustMotesComponent;
class AClueActor;
struct FRoomSurface;

/**
 * The laundry: the room across the cellar corridor from the maid's bedroom, where she washed,
 * dried and ironed for the family every day, and where the work stopped one day and was never
 * picked up again.
 *
 * Rendered concrete walls with the brick showing where the render has come away, worn grey flags,
 * dark joists, and pipes everywhere: the mains along the walls under the ceiling, a soil pipe across
 * it, the drops to the taps and the machines. The machines stand against the east wall — a gas
 * water heater in the corner, the washer with its door open and the wash still in the drum, the
 * dryer — and a basket half full in front of them. A deep fireclay sink against the far wall, the
 * folding table beside it with what was being folded, a shelf of soap powder and the radio over it. Tall painted cupboards and an open
 * shelf unit on the west wall, the ironing board folded against it. The airer by the east wall with
 * shirts still on their hangers and a girl's blouse, a line across the room with a towel and a
 * pillowcase pegged on it. The schedule, a note and the calendar on the door wall by the keys.
 *
 *                              north (corridor)
 *        +-------------[door]------------------------------+
 *        | shelving        keys calendar schedule   heater |
 *        |                                          washer |
 *        | cupboard              (soil pipe)   basket      |
 *        |                                          dryer  |
 *        | cupboard     ~~~~~~~~ line ~~~~~~~~   drain     |
 *        |                                          airer  |
 *        | ironing board     stool                         |
 *        | baskets    [ folding table ][ sink ]        mop |
 *        +-------------------------------------------------+
 *                                south (earth)
 *
 * Lit by nothing but the lantern: a cellar room has no window, and no storm light gets into the
 * cellar (fixed twice, 10-05; every room down here keeps it out).
 *
 * Frame: origin at the room's centre on its floor, X east, Y south (away from the corridor),
 * translated from the cellar's frame and never turned. The door wall is at -Y.
 */
UCLASS()
class ALaundryActor : public AActor
{
	GENERATED_BODY()

public:
	ALaundryActor();

	virtual void BeginPlay() override;

	/** Wall centre to wall centre; the wall thickness is the cellar's (static_assert in CellarActor.h). */
	static constexpr float Width = 640.f;
	static constexpr float Depth = 540.f;
	static constexpr float WallThickness = 20.f;
	static constexpr float Height = 305.f;
	/** The doorway in the north wall, its centre along X from the room's centre (the cellar hangs the door). */
	static constexpr float DoorX = -100.f;
	static constexpr float DoorHalf = 48.f;
	static constexpr float DoorHeight = 200.f;

	/** The inner faces of the walls. */
	static constexpr float HalfX = Width * 0.5f - WallThickness * 0.5f;
	static constexpr float HalfY = Depth * 0.5f - WallThickness * 0.5f;

	/** The sink against the south wall, its middle along X. */
	static constexpr float SinkX = 140.f;

private:
	// Shared with Tools/make_laundry.py.
	static constexpr float MachineDepth = 60.f;
	static constexpr float MachineTop = 87.f;
	static constexpr float PortZ = 44.f;
	static constexpr float DoorHingeX = -21.5f;
	static constexpr float DoorHingeY = MachineDepth * 0.5f + 1.2f;
	static constexpr float AirerLength = 110.f;
	static constexpr float AirerTop = 96.f;
	static constexpr float AirerSplay = 30.f;
	static constexpr float AirerTube = 1.1f;
	static constexpr float ShirtHook = 7.f;
	static constexpr float CounterLength = 210.f;
	static constexpr float CounterDepth = 62.f;
	static constexpr float CounterTop = 90.f;
	static constexpr float CabinetWidth = 90.f;
	static constexpr float CabinetDepth = 50.f;
	static constexpr float CabinetHeight = 212.f;
	static constexpr float ShelvesDepth = 36.f;
	static constexpr float WallShelfLength = 150.f;
	static constexpr float WallShelfDepth = 24.f;
	static constexpr float SinkTop = 90.f;
	static constexpr float SinkDepth = 50.f;
	static constexpr float HeaterRadius = 27.f;
	static constexpr float HeaterTop = 172.f;
	static constexpr float BasketHeight = 28.f;
	static constexpr float IroningLength = 122.f;

	/** Where things stand (the room's own layout, used by more than one builder). */
	static FVector Heater() { return FVector(262.f, -214.f, 0.f); }
	static FVector Washer() { return FVector(HalfX - MachineDepth * 0.5f - 4.f, -138.f, 0.f); }
	static FVector Dryer() { return FVector(HalfX - MachineDepth * 0.5f - 4.f, -70.f, 0.f); }
	static FVector Airer() { return FVector(258.f, 98.f, 0.f); }
	static FVector Sink() { return FVector(SinkX, HalfY - SinkDepth * 0.5f, 0.f); }
	static FVector Counter() { return FVector(-22.f, HalfY - CounterDepth * 0.5f - 2.f, 0.f); }
	static FVector WallShelf() { return FVector(-22.f, HalfY, 150.f); }
	/** The two cupboards on the west wall, the shut one north of the open one. */
	static FVector Cupboard(int32 Index) { return FVector(-HalfX + CabinetDepth * 0.5f + 1.f, Index == 0 ? -46.f : 50.f, 0.f); }
	static FVector Shelving() { return FVector(-HalfX + ShelvesDepth * 0.5f + 1.f, -192.f, 0.f); }
	/** The washing line across the room, west wall to over the airer, and its height. */
	static constexpr float LineY = 26.f;
	static constexpr float LineZ = 252.f;

	/** The four walls, by the way their inner faces look: north is the door wall. */
	enum class EWall : uint8 { North, South, East, West };

	/** Something standing against a wall, as a rectangle on it: U along it, Z up it. */
	struct FWallKeepOut
	{
		EWall Wall;
		FVector2D U;
		FVector2D Z;
	};

	void CacheMaterials(FRoomBuilder& Build);
	UMaterialInstanceDynamic* Sheet(FRoomBuilder& Build, const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale = 1.f);
	/** A wall piece's material: an instance of its own straight off the asset, its repeats written out
	 *  for its face (the wine cellar's narrow strip), so no piece is stretched into stripes. */
	UMaterialInterface* WallPieceMat(const FRoomSurface& Set, const FLinearColor& Tint, float W, float H, int32 Index);
	void WallPiece(FRoomBuilder& Build, const FRoomSurface& Set, const FLinearColor& Tint, const FVector& Centre, float Yaw, float W, float H, float Thick);

	void BuildShell(FRoomBuilder& Build);
	void BuildPipes(FRoomBuilder& Build);
	void BuildMachines(FRoomBuilder& Build);
	void BuildSinkAndTable(FRoomBuilder& Build);
	void BuildWestWall(FRoomBuilder& Build);
	void BuildDrying(FRoomBuilder& Build);
	void BuildDoorWall(FRoomBuilder& Build);
	void BuildCorner(FRoomBuilder& Build);
	/** After everything that stands against a wall: a wall decal has to know what it would land on. */
	void BuildWallDamp(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildDust(FRoomBuilder& Build);
	void BuildCobwebs(FRoomBuilder& Build);
	void BuildMist();

	/** Sets every named slot the mesh has. */
	static void Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	/** A placed generated prop with its slots dressed, no collision (blockers are separate). */
	UStaticMeshComponent* Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
		std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	/** Collision only a walking man meets, as everywhere in the cellar. */
	void Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw = 0.f);
	/** A rectangle of the paper atlas (Tools/make_laundry_art.py) laid on a plane: Normal out of it, Up its top. */
	UStaticMeshComponent* Paper(FRoomBuilder& Build, const TCHAR* Rect, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H);
	/** A pipe through the points, with an elbow at every turn and a bracket every so often. Its parts
	 *  take no decals, unless bTakesDecals: then they keep the wall decals off themselves instead. */
	void Pipe(FRoomBuilder& Build, TConstArrayView<FVector> Points, float Diameter, UMaterialInterface* Mat, float BracketEvery = 0.f, bool bTakesDecals = false);
	/** A cobweb as a plane. */
	void Web(FRoomBuilder& Build, const FVector& Centre, const FRotator& Rotation, const FVector2D& Size);
	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);

	/** Records Part's footprint on every wall it stands within a wall decal's reach of (the wine cellar's rule). */
	void KeepWallDecalsOff(const UPrimitiveComponent* Part);
	void KeepWallDecalsOff(const AActor* Actor);
	bool WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfAlong, float HalfUp) const;
	/** Whether a floor decal centred at Point, reaching HalfExtent along X and Y, would reach the
	 *  doorway or the quarter of floor the leaf sweeps (ACellarActor::SpawnDoor hangs it). */
	static bool FloorDecalReachesDoor(const FVector2D& Point, const FVector2D& HalfExtent);

	TArray<FWallKeepOut> WallKeepOuts;

	UPROPERTY(VisibleAnywhere, Category = "Laundry")
	TObjectPtr<USceneComponent> LaundryRoot;

	UPROPERTY(VisibleAnywhere, Category = "Laundry")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<ULocalFogVolumeComponent> Mist;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRender;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCorridorBrick;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFlags;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBoards;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatJoist;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTimber;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPipeIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLead;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatEnamel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatChrome;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubber;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDrum;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPortGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatInk;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatKnob;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShadow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCopper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatZinc;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeramic;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaint;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatScrubbed;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWicker;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStraw;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStrings;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTin;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatToolPaint;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShirt;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShirtBlue;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBlouse;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGreyCloth;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTowel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTowelBlue;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCover;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLabel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCard;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatAmber;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlastic;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCap;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSoap;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBakelite;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGrille;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlove;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCushion;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> MatThreads;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBulb;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> PaperMats;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> WallMats;
};
