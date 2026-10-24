#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusicRoomActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AStormWindowActor;
class AClueActor;

/** Where the music room meets the stair hall, handed over by the hall that spawns it. */
struct FMusicRoomSetup
{
	/** Y of the room's south face: the stair hall's north wall, seen from the other side. */
	float SouthFace = -80.f;
	/** The north face, the façade: the kitchen's north wall carried up. */
	float NorthFace = -700.f;
	/** The west and east faces: over the kitchen's, a storey down. */
	float WestX = -1990.f;
	float EastX = -1270.f;
	float WallThickness = 20.f;
	/** The top of the stair hall's north wall: the strip of south wall above it is this room's to build. */
	float HallWallTop = 440.f;
};

/**
 * The music room, over the kitchen: the family's room for music, where they played in the evenings.
 * Entered through a wide open arch, no door, off the stair hall's north gallery: stepping out of the
 * corridor onto the gallery and facing the stained glass, it is on the right (the library is on the
 * left). There is no piano (the user's call).
 *
 *                      north (the storm, the trees: the kitchen's outside, a storey down)
 *      +--[window]------[window]------[window]--+
 *      |   bench         cello            guitar |
 *      |  (violin case) stand chair        still life
 *      |  cabinet   iron stand            sideboard (east)
 *      | (west)    chair                    awards |
 *      |  shelf of    (chandelier)               |
 *      |  music   armchairs + table              |
 *      |  cellist                                |
 *      +--clock------river----------[ arch ]-----+
 *                      south (the stair hall's north gallery)
 *
 * Panelled to the dado in dark oak, an old-gold silk damask over it to a plaster cornice, three tall
 * round-headed windows hung with emerald velvet, a crystal chandelier from a plaster rose. In front
 * of the windows the cello in its stand beside its music stand with the manuscript still on it and
 * the chair pushed back, the violinist's iron stand with a score open on it and her bow left across
 * her chair, the violin open in its case on the window bench; two green velvet armchairs turned to
 * listen, the metronome on the table between them.
 *
 * Shares the room's frame, like the hall that spawns it and the kitchen under it, so every number in
 * here reads straight off -RoomShotX/Y/Z. The instruments, the furniture, the chandelier and the arch
 * are generated in Blender by Tools/make_music_room.py; the velvet, the damask, the music, the spines,
 * the photographs and the paintings are baked by Tools/make_music_art.py.
 */
UCLASS()
class AMusicRoomActor : public AActor
{
	GENERATED_BODY()

public:
	AMusicRoomActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FMusicRoomSetup& InSetup, AStormWindowActor* InLeadStorm);

	virtual void BeginPlay() override;

	/** A tall room, as the library across the stairwell. */
	static constexpr float RoomHeight = 520.f;

	// The arch from the stair hall's north gallery (Tools/make_music_room.py: ARCH_*): a semi-ellipse on
	// its springing. The hall cuts its north wall to the rectangle under ArchCrown and keeps its
	// mouldings off the hall face's architrave (14 round the curve: AGrandStaircaseActor::GalleryArches).
	// The room face's carved surround stands out to ArchSurroundHalf either side and up to
	// ArchSurroundTop, and the room's walls are cut to it.
	static constexpr float ArchHalf = 110.f;
	static constexpr float ArchSpring = 192.f;
	static constexpr float ArchRise = 72.f;
	static constexpr float ArchCrown = ArchSpring + ArchRise;
	static constexpr float ArchArchitrave = 14.f;
	static constexpr float ArchSurroundHalf = 136.f;
	static constexpr float ArchSurroundTop = 300.f;
	/**
	 * Where along the south wall the arch stands: at the corridor's end of the hall's north gallery,
	 * across the stairwell from the library's (ALibraryActor::ArchX; the user's call: by the corridor,
	 * not by the stairhead). The hall face's architrave keeps a hand's width off the hall's east wall,
	 * and the room face's surround off this room's (the asserts are on the hall's east wall written out;
	 * AGrandStaircaseActor::SpawnMusicRoom checks them against the hall's own).
	 */
	static constexpr float ArchX = -1420.f;
	static_assert(ArchX + ArchHalf + ArchArchitrave < -1270.f - 15.f, "the hall face stays off the hall's east wall");
	static_assert(ArchX + ArchSurroundHalf < -1270.f - 10.f, "the surround stays off the east wall");

	// The panelling: oak to the dado, a rail on it, the silk over it to the cornice.
	static constexpr float DadoTop = 118.f;

	// The three tall windows in the north wall, round-headed with the library's heads (WINDOW_HALF).
	static constexpr float WindowHalf = 75.f;
	static constexpr float WindowSill = 40.f;
	static constexpr float WindowTop = 430.f;
	static constexpr float WindowXs[3] = { -1850.f, -1630.f, -1410.f };

	// The music stands' desks (IRON_DESK_Z, WOOD_DESK_Z, DESK_LEAN, DESK_W, DESK_H): the middle of each
	// board, how far it leans back from upright, its size. A desk faces its stand's local +Y.
	static constexpr float IronDeskZ = 116.f;
	static constexpr float WoodDeskZ = 106.f;
	static constexpr float DeskLean = 22.f;
	static constexpr float DeskW = 50.f;
	static constexpr float DeskH = 34.f;

	// The display shelf (SHELF_*, BOOK_ROWS), the sideboard, the side table, the bench, the cabinet.
	static constexpr float ShelfW = 150.f;
	static constexpr float ShelfD = 34.f;
	static constexpr float ShelfBoards[3] = { 10.5f, 45.5f, 80.5f };
	static constexpr float ShelfTop = 116.f;
	static constexpr float ShelfInside = ShelfW - 6.f;
	static constexpr float BookRows[3] = { 44.f, 38.f, 31.f };
	static constexpr float SideboardW = 160.f;
	static constexpr float SideboardD = 52.f;
	static constexpr float SideboardTop = 92.f;
	static constexpr float SideTableTop = 66.f;
	static constexpr float BenchL = 104.f;
	static constexpr float BenchD = 40.f;
	static constexpr float BenchTop = 46.f;
	static constexpr float CabinetW = 130.f;
	static constexpr float CabinetD = 48.f;
	static constexpr float CabinetH = 226.f;

	// The pictures each frame holds (CELLIST_ART, LANDSCAPE_ART, CERT_LAND_ART, CERT_PORT_ART).
	static constexpr float CellistArtW = 80.f;
	static constexpr float CellistArtH = 100.f;
	static constexpr float LandscapeArtW = 76.f;
	static constexpr float LandscapeArtH = 57.f;
	static constexpr float CertLong = 42.f;
	static constexpr float CertShort = 29.7f;

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

	/** Something standing within a wall decal's reach of a wall: its extent along the wall (U) and up it (Z). */
	struct FWallKeepOut
	{
		EWall Wall;
		FVector2D U;
		FVector2D Z;
	};

	// Plan helpers, in the room frame. North is -Y, as everywhere in the house.
	float WestX() const { return Setup.WestX; }
	float EastX() const { return Setup.EastX; }
	float NorthY() const { return Setup.NorthFace; }
	float SouthY() const { return Setup.SouthFace; }
	float MidX() const { return (WestX() + EastX()) * 0.5f; }
	float MidY() const { return (NorthY() + SouthY()) * 0.5f; }

	// Where things stand, shared between a piece and what is put on or round it.
	FVector BenchSeat() const { return FVector(WindowXs[0], NorthY() + 56.f, 0.f); }
	FVector WoodStandSeat() const { return FVector(-1613.f, -420.f, 0.f); }
	float WoodStandYaw() const { return 184.f; }
	FVector CellistChairSeat() const { return FVector(-1602.f, -520.f, 0.f); }
	FVector CelloSeat() const { return FVector(-1516.f, -500.f, 0.f); }
	FVector IronStandSeat() const { return FVector(-1755.f, -408.f, 0.f); }
	float IronStandYaw() const { return 172.f; }
	FVector ViolinChairSeat() const { return FVector(-1772.f, -520.f, 0.f); }
	FVector GuitarSeat() const { return FVector(EastX() - 37.f, -598.f, 0.f); }
	/** West of the way in from the arch, turned to the players. */
	FVector ArmchairSeat(int32 Index) const { return Index == 0 ? FVector(-1755.f, -205.f, 0.f) : FVector(-1880.f, -265.f, 0.f); }
	FVector SideTableSeat() const { return FVector(-1830.f, -160.f, 0.f); }
	FVector SideboardSeat() const { return FVector(EastX() - SideboardD * 0.5f, -330.f, 0.f); }
	FVector CabinetSeat() const { return FVector(WestX() + CabinetD * 0.5f, -440.f, 0.f); }
	FVector ShelfSeat() const { return FVector(WestX() + ShelfD * 0.5f, -168.f, 0.f); }
	/** The long-case clock against the south wall west of the arch: the point under its back, on the wall. */
	FVector ClockSeat() const { return FVector(WestX() + 90.f, SouthY(), 0.f); }
	FVector ChandelierHook() const { return FVector(MidX(), MidY(), RoomHeight); }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	/** The dust and the damp on the floor, laid once the furniture stands (FloorKeepOuts). */
	void BuildFloorDust(FRoomBuilder& Build);
	void BuildWalls(FRoomBuilder& Build);
	void BuildWindows(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildChandelier(FRoomBuilder& Build);
	void BuildEnsemble(FRoomBuilder& Build);
	void BuildListening(FRoomBuilder& Build);
	void BuildWestWall(FRoomBuilder& Build);
	void BuildEastWall(FRoomBuilder& Build);
	void BuildSouthWall(FRoomBuilder& Build);
	void BuildScatteredMusic(FRoomBuilder& Build);
	void BuildDamage(FRoomBuilder& Build);
	void BuildClues();
	void SpawnWindows();

	/** The panelling to the dado, one piece of it: a thin box with the repeats written out (the library's PanelPiece). */
	void PanelPiece(FRoomBuilder& Build, const FVector& Centre, float Yaw, float W, float H);
	/** The silk on one wall: a generated sheet over every piece, its UVs in centimetres so the damask runs on across the cuts. */
	void SilkSheet(EWall Wall, const TArray<FBox2D>& Pieces);
	void AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const;
	/** The openings on a wall as holes, the arch as its whole carved surround. */
	TArray<FBox2D> Holes(EWall Wall) const;
	float WallFace(EWall Wall) const;
	FVector WallNormal(EWall Wall) const;
	FVector WallPoint(EWall Wall, float U, float Z, float Proud) const;
	/** The yaw that turns a prop's local +Y — the way every prop here faces — out of a wall. */
	static float FacingYaw(EWall Wall);
	/** Whether a wall decal at (U, Z) of that size would land on an opening, its surround, or anything recorded by KeepWallDecalsOff. */
	bool WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfU, float HalfZ, bool bOpenings = true) const;
	/** Records a placed part that stands within a wall decal's reach of a wall, read off its bounds (the wine cellar's). */
	void KeepWallDecalsOff(const UPrimitiveComponent* Part);
	void KeepWallDecalsOff(const AActor* Actor);
	/** Records a sconce (FRoomBuilder::Sconce) put at (U, Z) on a wall, which the wall's decals keep off. */
	void KeepSconce(EWall Wall, float U, float Z);
	/** Records a placed part standing on the floor, within a floor decal's reach, read off its bounds. */
	void KeepFloorDustOff(const UPrimitiveComponent* Part);
	/** Whether a floor decal centred there with that rolled half extent would reach something standing on the floor. */
	bool FloorDustHitsSomething(const FVector2D& Centre, const FVector2D& HalfExtent) const;
	FBox RoomBox(const UPrimitiveComponent* Part) const;

	/** Places a generated model and puts the room's materials on its named slots. */
	UStaticMeshComponent* Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
		std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	static void Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	/** Collision only a walking man meets: the interaction trace and the camera pass through. */
	void Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw = 0.f);
	/** A sheet of the paper atlas (Tools/make_music_art.py: PAPER) on a plane facing Normal, its top towards Up. */
	UStaticMeshComponent* Paper(FRoomBuilder& Build, const TCHAR* Rect, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H);
	/** A picture from the paintings sheet, or a certificate from the paper atlas, on a plane facing Normal. */
	UStaticMeshComponent* Picture(FRoomBuilder& Build, const TCHAR* Set, const FVector4f& Rect, float SheetSize, const FVector& Centre,
		const FVector& Normal, float W, float H, const FLinearColor& Tint);
	/** Dust settled on a flat top: a soft projected bloom, aimed down. */
	void Dust(FRoomBuilder& Build, const FVector& Centre, const FVector2D& Size, float Opacity = 0.4f) const;
	/** A child scene component to build a group of parts in its own frame. */
	USceneComponent* Pivot(const FVector& Location, const FRotator& Rotation, const TCHAR* Name);
	/** A music stand's desk in the room frame: its middle, the way it faces, and up it. */
	void DeskFrame(const FVector& Seat, float Yaw, float DeskZ, FVector& OutCentre, FVector& OutNormal, FVector& OutUp) const;

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);

	UPROPERTY(VisibleAnywhere, Category = "Music Room")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Music Room")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> LeadStorm;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AStormWindowActor>> Windows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	TArray<FOpening> Openings;
	TArray<FWallKeepOut> WallKeepOuts;
	/** The footprints of what stands on the floor, in the room's frame (KeepFloorDustOff). */
	TArray<FBox2D> FloorKeepOuts;
	FMusicRoomSetup Setup;
	FRandomStream Random;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPanel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPanelSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSilk;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlasterSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloor;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOak;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOakBox;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMahogany;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVarnish;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLeather;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLeatherGreen;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVelvet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCorridorPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBooks;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaperSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWax;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	/** Flat colours, by name: gilt, ebony, crystal, the strings, the plush. */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> Flats;
	/** One instance per paper rectangle and per picture, made lazily. */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> SheetMats;
	/** One instance per piece of panelling (PanelPiece). */
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PanelMats;

	/** A named flat colour from Flats. */
	UMaterialInterface* F(const TCHAR* Name) const;
};
