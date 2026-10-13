#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LibraryActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AStormWindowActor;
class AClueActor;

/** Where the library meets the upstairs corridor, handed over by the corridor that spawns it. */
struct FLibrarySetup
{
	/** Y of the room's north face: the corridor's south wall, seen from the other side. */
	float NorthFace = 565.f;
	/** The west face, in line with the corridor's end wall (the stair hall is behind it). */
	float WestX = -1250.f;
	/** The east face: the far side of the girl's room's west wall. */
	float EastX = -320.f;
	float WallThickness = 20.f;
	/** The arch from the corridor, in this room's north wall. The corridor cuts the hole. */
	float ArchX = -880.f;
	/** The corridor's ceiling: the strip of north wall above it is this room's to build. */
	float CorridorHeight = 305.f;
};

/**
 * The library and study, on the south side of the upstairs corridor between the girl's room and
 * the stairs: the owner's room, the most finished in the house, and the one he spent his evenings
 * in. Entered through an open arch, no door.
 *
 *                      north (the corridor, the arch)
 *      +-- shelves --[arch]-- shelves ------------------+
 *      |  s          vitrine            chess table     |
 *      |  h ladder                        armchair  fireplace
 *      |  e                    (chandelier)   lamp      | (east)
 *      |  l                                   shelves   |
 *      |  v   globe     +--- desk ---+                  |
 *      |  e             |  the rug   |       filing cab.|
 *      +---[window]-bust-[window]-gramophone-[window]---+
 *                      south (the storm, the trees)
 *
 * Floor-to-ceiling oak bookcases on three walls, a rolling ladder on the long one, a stone
 * fireplace in the east wall with a portrait of the woman and the girl over it, the partners' desk
 * in the middle on the oriental carpet with the windows behind it, and everything where he left it
 * when he stood up from the desk.
 *
 * Shares the room's frame, like the corridor and the girl's room, so every number in here reads
 * straight off -RoomShotX/Y/Z. The bookcases, the books, the furniture and the things on the desk
 * are generated in Blender by Tools/make_library.py; the spines, the papers, the maps, the globe,
 * the paintings and the carpet are baked by Tools/make_library_art.py.
 */
UCLASS()
class ALibraryActor : public AActor
{
	GENERATED_BODY()

public:
	ALibraryActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FLibrarySetup& InSetup, AStormWindowActor* InLeadStorm);

	virtual void BeginPlay() override;

	/** A tall room: the shelves go to 470 and the ceiling is over them. */
	static constexpr float RoomHeight = 520.f;
	/** The south wall, in line with the living room's south façade below and to the west. */
	static constexpr float SouthFace = 1860.f;

	// The arch from the corridor (Tools/make_library.py: ARCH_*). The corridor cuts its wall to the
	// rectangle the arch stands in, and keeps its cornice (from 281) clear of the architrave.
	static constexpr float ArchHalf = 80.f;
	static constexpr float ArchSpring = 185.f;
	static constexpr float ArchCrown = ArchSpring + ArchHalf;
	/** How far the architrave stands out round the opening. */
	static constexpr float ArchArchitrave = 14.2f;

	// The three tall windows in the south wall, round-headed (WINDOW_HALF).
	static constexpr float WindowHalf = 75.f;
	static constexpr float WindowSill = 70.f;
	static constexpr float WindowTop = 400.f;
	static constexpr float WindowXs[3] = { -1060.f, -785.f, -510.f };

	// A bookcase bay (BAY_WIDTH, LOWER_DEPTH, UPPER_DEPTH, COUNTER_Z, SHELF_PITCH, SHELF_LEVELS,
	// SHELF_BOARD, CASE_TOP, UPRIGHT).
	static constexpr float BayWidth = 100.f;
	static constexpr float BayUpright = 2.5f;
	static constexpr float LowerDepth = 46.f;
	static constexpr float UpperDepth = 34.f;
	static constexpr float CounterZ = 92.f;
	static constexpr float ShelfPitch = 36.f;
	static constexpr int32 ShelfLevels = 10;
	static constexpr float CaseTop = 470.f;
	// The ladder's rail and its foot (RAIL_Z, RAIL_PROUD, LADDER_FOOT).
	static constexpr float RailZ = 434.f;
	static constexpr float RailProud = UpperDepth + 6.f;
	static constexpr float LadderFoot = LowerDepth + 70.f;
	/** The rows of books (ROW_LENGTHS), each exactly this long. */
	static constexpr float RowLengths[10] = { 46.f, 44.f, 47.f, 42.f, 45.f, 46.f, 43.f, 47.f, 30.f, 34.f };

	// The desk (DESK_*).
	static constexpr float DeskWidth = 180.f;
	static constexpr float DeskDepth = 90.f;
	static constexpr float DeskTop = 78.f;

	// The chimney breast in the east wall, and the chimneypiece on it.
	static constexpr float BreastY = 1010.f;
	static constexpr float BreastHalf = 120.f;
	static constexpr float BreastProud = 55.f;
	static constexpr float MantelTop = 138.f;

private:
	enum class EWall : uint8 { North, South, East, West };

	struct FOpening
	{
		EWall Wall;
		float CenterU;
		float HalfU;
		float BottomZ;
		float TopZ;
	};

	/** A wall of bookcases: the bays from U0 to U1 along one wall. */
	struct FShelfRun
	{
		EWall Wall;
		float U0;
		float U1;
	};

	/** Something standing within a wall decal's reach of a wall: its extent along the wall (U) and up it (Z). */
	struct FWallKeepOut
	{
		EWall Wall;
		FVector2D U;
		FVector2D Z;
	};

	/**
	 * The footprint of something standing on the floor, in the room's frame. Along: the wall a
	 * bookcase run lines, whose own wall-foot dust is meant to lie at its feet and is not kept off it.
	 */
	struct FFloorKeepOut
	{
		FBox2D Box;
		TOptional<EWall> Along;
	};

	// Plan helpers, in the room frame. North is -Y, as everywhere in the house.
	float WestX() const { return Setup.WestX; }
	float EastX() const { return Setup.EastX; }
	float NorthY() const { return Setup.NorthFace; }
	float SouthY() const { return SouthFace; }
	float MidX() const { return (WestX() + EastX()) * 0.5f; }
	float MidY() const { return (NorthY() + SouthY()) * 0.5f; }
	float BreastFaceX() const { return EastX() - BreastProud; }

	// Where things stand, shared between a piece and what is put on or round it.
	FVector DeskSeat() const { return FVector(-785.f, 1450.f, 0.f); }
	FVector ArmchairSeat() const { return FVector(-545.f, 1195.f, 0.f); }
	FVector ChessSeat() const { return FVector(-525.f, 760.f, 0.f); }
	FVector GlobeSeat() const { return FVector(-1052.f, 1525.f, 0.f); }
	FVector VitrineSeat() const { return FVector(-985.f, 985.f, 0.f); }
	FVector FilingSeat() const { return FVector(EastX() - 34.f, 1665.f, 0.f); }
	/** The ladder hangs on the west wall's rail, this far along it. */
	float LadderY() const { return 1110.f; }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	/** The dust and the damp on the floor, laid once the furniture stands (FloorKeepOuts). */
	void BuildFloorDust(FRoomBuilder& Build);
	void BuildWalls(FRoomBuilder& Build);
	void BuildWindows(FRoomBuilder& Build);
	void BuildShelves(FRoomBuilder& Build);
	void BuildFireplace(FRoomBuilder& Build);
	void BuildDesk(FRoomBuilder& Build);
	void BuildReadingCorner(FRoomBuilder& Build);
	void BuildDecor(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildDamage(FRoomBuilder& Build);
	void BuildClues();
	void SpawnWindows();

	/** One bay's shelves filled with rows of books, a gap here and there, a pile in one, a few gone over. */
	void FillBay(const FTransform& Bay, float ScaleX, FRandomStream& Stream);

	/** A slab laid on a wall's room face, cut around every opening on that wall. */
	void WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud = 0.f);
	/**
	 * One piece of panelling, W along its face and H up it, standing on its local Y: a thin box whose
	 * broad faces carry U along X and V up Z, on an instance with the repeats written out for this
	 * piece (the laundry's WallPiece). Laid as a builder-tiled slab, a piece narrower than it is tall
	 * stretched the panels into vertical stripes (the pitfall, 10-09).
	 */
	void PanelPiece(FRoomBuilder& Build, const FVector& Centre, float Yaw, float W, float H);
	void AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const;
	TArray<FBox2D> CutAround(EWall Wall, float U0, float U1, float Z0, float Z1) const;
	float WallFace(EWall Wall) const;
	FVector WallNormal(EWall Wall) const;
	FVector WallPoint(EWall Wall, float U, float Z, float Proud) const;
	/** The yaw that turns a prop's local +Y — the way every prop here faces — out of a wall. */
	static float FacingYaw(EWall Wall);
	/**
	 * Whether a wall decal at (U, Z) of that size would land on a bookcase, an opening, the breast or
	 * anything recorded by KeepWallDecalsOff. bOpenings off for the patches that are meant to sit just
	 * outside a window's architrave.
	 */
	bool WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfU, float HalfZ, bool bOpenings = true) const;
	/**
	 * Records a placed part that stands within a wall decal's reach of a wall, read off its bounds
	 * (the wine cellar's KeepWallDecalsOff). Except: the wall whose keep-out is already the shelf-run
	 * test, so a bookcase bay only adds the wall its end stands against. A part that takes no decals
	 * needs no keep-out and is passed over.
	 */
	void KeepWallDecalsOff(const UPrimitiveComponent* Part, TOptional<EWall> Except = TOptional<EWall>());
	void KeepWallDecalsOff(const AActor* Actor);
	/** Records a placed part standing on the floor, within a floor decal's reach, read off its bounds. */
	void KeepFloorDustOff(const UPrimitiveComponent* Part, TOptional<EWall> Along = TOptional<EWall>());
	/**
	 * Whether a floor decal centred there with that rolled half extent would reach something standing
	 * on the floor. AlongWall: the wall a wall-foot strip runs along, whose bookcases it may touch.
	 */
	bool FloorDustHitsSomething(const FVector2D& Centre, const FVector2D& HalfExtent, TOptional<EWall> AlongWall = TOptional<EWall>()) const;
	/** A part's bounds in the room's frame. */
	FBox RoomBox(const UPrimitiveComponent* Part) const;

	/** Places a generated model and puts the room's materials on its named slots. */
	UStaticMeshComponent* Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
		std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	static void Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	/** Collision only a walking man meets: the interaction trace and the camera pass through. */
	void Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw = 0.f);
	/** A sheet of the paper atlas (Tools/make_library_art.py: PAPER) on a plane facing Normal, its top towards Up. */
	UStaticMeshComponent* Paper(FRoomBuilder& Build, const TCHAR* Rect, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H);
	/** A picture from one of the baked sheets (maps, paintings), on a plane facing Normal. */
	UStaticMeshComponent* Picture(FRoomBuilder& Build, const TCHAR* Set, const FVector4f& Rect, float SheetSize, const FVector& Centre,
		const FVector& Normal, float W, float H, const FLinearColor& Tint, float RoughnessScale);
	/** Dust settled on a flat top: a soft projected bloom, aimed down. */
	void Dust(FRoomBuilder& Build, const FVector& Centre, const FVector2D& Size, float Opacity = 0.4f) const;
	/** A child scene component to build a group of parts in its own frame. */
	USceneComponent* Pivot(const FVector& Location, const FRotator& Rotation, const TCHAR* Name);

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);

	UPROPERTY(VisibleAnywhere, Category = "Library")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Library")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> LeadStorm;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AStormWindowActor>> Windows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	/** The book rows (library_books_a..j), the leaning group and the two piles: one instanced set each. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BookRows;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> BooksLeaning;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BookPiles;

	TArray<FOpening> Openings;
	TArray<FShelfRun> ShelfRuns;
	TArray<FWallKeepOut> WallKeepOuts;
	/** The footprints of what stands on the floor, in the room's frame (KeepFloorDustOff). */
	TArray<FFloorKeepOut> FloorKeepOuts;
	FLibrarySetup Setup;
	FRandomStream Random;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPanel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPanelSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatParquet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOak;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOakBox;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMahogany;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLeather;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLeatherGreen;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStone;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMarble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSootBrick;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFabric;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGreenRug;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCorridorPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBooks;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaperSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlobe;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRug;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	/** Flat colours, by name: gilt, ink, glass, felt, the chessmen, the shade. */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> Flats;
	/** One instance per paper rectangle and per picture, made lazily. */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> SheetMats;
	/** One instance per piece of panelling (PanelPiece). */
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PanelMats;

	/** A named flat colour from Flats. */
	UMaterialInterface* F(const TCHAR* Name) const;
};
