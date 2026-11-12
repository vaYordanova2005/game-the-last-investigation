#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StudyActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AStormWindowActor;
class AClueActor;

/** Where the study meets the upstairs corridor and the rooms round it, handed over by the corridor. */
struct FStudySetup
{
	/** Y of the room's south face: the corridor's north wall, seen from the other side. */
	float SouthFace = 315.f;
	/** The north face, the façade: in line with the kitchen's north wall, a storey below and to the west. */
	float NorthFace = -700.f;
	/** The west face: the music room's east wall and the stair hall's, seen from this side. */
	float WestX = -1250.f;
	/** The east face: the far side of the bedroom's west wall, and the house's outside wall north of it. */
	float EastX = -410.f;
	float WallThickness = 20.f;
	/** The door from the corridor, in this room's south wall. The corridor hangs the leaf. */
	float DoorX = -830.f;
	/** The leaf's hinge, on the door's east jamb, and how far it opens into the room. */
	float HingeX = -772.f;
	float DoorLeafWidth = 116.f;
	float DoorOpenYaw = 88.f;
	/** The corridor's ceiling, and the bedroom's: the strips of wall above them are this room's to build. */
	float CorridorHeight = 305.f;
	/** The bedroom behind the east wall: how far its west wall runs north of y = 0. */
	float BedroomNorthY = -345.f;
};

/**
 * The study: the room the man of the house worked in, on the north side of the upstairs corridor
 * between the stair hall and the bedroom. Black lacquer, burgundy leather and gold.
 *
 *                      north (the storm: the kitchen's outside, a storey down)
 *      +------[window]-------- portrait --------[window]------+
 *      | bookcase              his chair               bookcase|
 *      |                 [======= DESK =======]        bookcase|
 *  W   |  case         club chair      club chair               |  E
 *  E   |  board               (the rug)           map   sofa    |  (the
 *  S   |  boxes                                  table          |   bedroom)
 *  T   |           card table          (chandelier)              |
 *      |          + chairs                                 safe |
 *      +------- bar ---------[ DOOR ]-------- coat stand -------+
 *                      south (the corridor)
 *
 * Black lacquered panelling to the dado with a gilt rail on it and burgundy silk damask over it to a
 * black cornice; a dark ceiling with gilt mouldings and a dead chandelier; dark herringbone parquet
 * and a burgundy carpet with a gold key fret under the desk. A black partners' desk under a burgundy
 * leather top faces the door, his tall buttoned chair behind it and two Chesterfield club chairs in
 * front; a Chesterfield sofa on the east wall under a city map, a card table with the game still on
 * it, a drinks cabinet with the decanters out, a safe, black bookcases, gilt sconces with burgundy
 * shades. His case board on the west wall. Two tall windows hung in burgundy velvet behind the desk,
 * the portrait of a man in a black hat between them.
 *
 * Shares the corridor's frame (the room's), so every number in here reads straight off
 * -RoomShotX/Y/Z. The door, the furniture and what is on it are generated in Blender by
 * Tools/make_study.py; the board, the portrait, the map, the folders and the carpet are baked by
 * Tools/make_study_art.py; the leather, the lacquer, the gilt, the velvet and the plaster are Poly
 * Haven photographs.
 */
UCLASS()
class AStudyActor : public AActor
{
	GENERATED_BODY()

public:
	AStudyActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FStudySetup& InSetup, AStormWindowActor* InLeadStorm);

	virtual void BeginPlay() override;

	/**
	 * The study door leaf's slots (Tools/make_study.py: door), dressed in this room's lacquer and gilt:
	 * the corridor hangs the leaf (AHallDoorActor) and hands it these, so the door and the room are
	 * one finish from one place.
	 */
	static TArray<TPair<FName, UMaterialInterface*>> DoorLeafSlots(UObject* Outer);
	/** The study door's leaf model. */
	static const TCHAR* DoorLeafModel();

	static constexpr float RoomHeight = 400.f;

	// Shared with Tools/make_study.py.
	static constexpr float DoorOpeningWidth = 120.f;
	static constexpr float DoorOpeningHeight = 250.f;
	static constexpr float DoorLeafWidth = 116.f;
	static constexpr float DoorLeafHeight = 248.f;
	/** The door case's architrave each side of the opening, and how far its cornice reaches past it. */
	static constexpr float DoorCaseWidth = 16.f;
	static constexpr float DoorCaseReach = DoorCaseWidth + 6.f;
	/** The case's top on the corridor face: under the corridor's cornice (281). */
	static constexpr float DoorCaseTop = DoorOpeningHeight + DoorCaseWidth + 14.f;
	static_assert(DoorCaseTop < 281.f, "the corridor's cornice comes down to 281");
	static constexpr float DeskW = 230.f;
	static constexpr float DeskD = 105.f;
	static constexpr float DeskTop = 78.f;
	static constexpr float BarW = 170.f;
	static constexpr float BarD = 52.f;
	static constexpr float BarTop = 98.f;
	static constexpr float SafeW = 72.f;
	static constexpr float SafeD = 66.f;
	static constexpr float SafeH = 116.f;
	static constexpr float BoardW = 200.f;
	static constexpr float BoardH = 125.f;
	static constexpr float CardTableTop = 74.f;
	static constexpr float CardTableR = 55.f;
	static constexpr float CoffeeW = 120.f;
	static constexpr float CoffeeD = 62.f;
	static constexpr float CoffeeTop = 44.f;
	static constexpr float ClubW = 98.f;
	static constexpr float SofaW = 224.f;
	static constexpr float ChesterD = 92.f;
	static constexpr float ChesterTop = 74.f;
	static constexpr float CaseW = 120.f;
	static constexpr float CaseD = 42.f;
	static constexpr float CaseH = 262.f;
	static constexpr float CaseInterior = 112.f;
	static constexpr float CaseShelves[4] = { 88.f, 130.f, 172.f, 214.f };
	static constexpr float CoatStandH = 186.f;
	static constexpr float TrayTop = 1.6f;

	// The panelling: black lacquer to the dado, a gilt rail on it, the damask over it to the cornice.
	static constexpr float DadoTop = 112.f;

	// The two tall windows in the north wall, either side of the desk.
	static constexpr float WindowHalf = 72.f;
	static constexpr float WindowSill = 52.f;
	static constexpr float WindowTop = 300.f;
	static constexpr float WindowXs[2] = { -1060.f, -600.f };
	/** The architrave round each window, on the room face. */
	static constexpr float WindowCasing = 12.f;

private:
	enum class EWall : uint8 { North, South, East, West };

	/** A hole in one wall, in that wall's U (along) / Z (up) space. */
	struct FOpening
	{
		EWall Wall;
		float CenterU;
		float HalfU;
		float BottomZ;
		float TopZ;
	};

	/** What stands on or against a wall within a decal's reach, as (along, up) ranges. */
	struct FWallKeepOut
	{
		EWall Wall;
		FVector2D U;
		FVector2D Z;
	};

	// The room's inner faces, in the corridor's frame. North is -Y, as everywhere in the house.
	float WestX() const { return Setup.WestX; }
	float EastX() const { return Setup.EastX; }
	float NorthY() const { return Setup.NorthFace; }
	float SouthY() const { return Setup.SouthFace; }
	float MidX() const { return (WestX() + EastX()) * 0.5f; }
	float MidY() const { return (NorthY() + SouthY()) * 0.5f; }

	// Where things stand, shared between the pieces, what is on them, and the decals' keep-outs.
	/** The desk, its visitors' side to the door, its middle on the door's line. */
	FVector DeskSeat() const { return FVector(Setup.DoorX, NorthY() + 270.f, 0.f); }
	/** His chair behind it, pushed back and turned. */
	FVector ChairSeat() const { return DeskSeat() + FVector(10.f, -DeskD * 0.5f - 52.f, 0.f); }
	static constexpr float ChairYaw = 14.f;
	/** The two club chairs in front of the desk, facing it, turned a little to each other. */
	FVector ClubSeat(int32 Side) const { return DeskSeat() + FVector(Side * 78.f, DeskD * 0.5f + 92.f, 0.f); }
	static float ClubYaw(int32 Side) { return 180.f - Side * 14.f; }
	/** The carpet under the desk and the club chairs. */
	FVector RugSeat() const { return DeskSeat() + FVector(0.f, 40.f, 0.f); }
	static constexpr float RugW = 400.f;
	static constexpr float RugD = 280.f;
	/** The portrait between the windows, over his chair. */
	FVector PortraitAt() const { return FVector(Setup.DoorX, NorthY(), 205.f); }
	/** The case board on the west wall, and the file boxes under it. */
	static constexpr float BoardY = -330.f;
	/** Its frame's foot clear over the dado rail (DadoTop + 7): lower, the gilt rail ran across the cork. */
	static constexpr float BoardZ = 192.f;
	static_assert(BoardZ - BoardH * 0.5f - 5.f > DadoTop + 7.f, "the board's frame stands clear of the dado rail");
	/** The sofa on the east wall, the map over it, the table in front of it. */
	static constexpr float SofaY = -140.f;
	FVector SofaSeat() const { return FVector(EastX() - ChesterD * 0.5f - 3.f, SofaY, 0.f); }
	FVector CoffeeSeat() const { return FVector(EastX() - ChesterD - 82.f, SofaY, 0.f); }
	static constexpr float MapZ = 178.f;
	/** The bookcases: one on the west wall north of the board, two on the east wall north of the sofa. */
	FVector WestCaseSeat() const { return FVector(WestX() + CaseD * 0.5f + 1.f, NorthY() + 95.f, 0.f); }
	FVector EastCaseSeat(int32 Index) const { return FVector(EastX() - CaseD * 0.5f - 1.f, NorthY() + 95.f + Index * (CaseW + 4.f), 0.f); }
	/** The card table, in the south-west, and its chairs round it. */
	FVector CardTableSeat() const { return FVector(WestX() + 250.f, 40.f, 0.f); }
	/** The drinks cabinet on the south wall west of the door. */
	FVector BarSeat() const { return FVector(Setup.DoorX - 250.f, SouthY() - BarD * 0.5f - 2.f, 0.f); }
	/** The safe in the south-east corner, facing the room. */
	FVector SafeSeat() const { return FVector(EastX() - SafeW * 0.5f - 6.f, SouthY() - SafeD * 0.5f - 6.f, 0.f); }
	/** The coat stand inside the door, clear of the leaf's sweep. */
	FVector CoatStandSeat() const { return FVector(Setup.HingeX + 70.f, SouthY() - 45.f, 0.f); }
	/** The chandelier's rose. */
	FVector ChandelierHook() const { return FVector(Setup.DoorX, -180.f, RoomHeight); }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildWalls(FRoomBuilder& Build);
	void BuildWindows(FRoomBuilder& Build);
	void BuildDoorCase(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildChandelier(FRoomBuilder& Build);
	void BuildDesk(FRoomBuilder& Build);
	void BuildSeating(FRoomBuilder& Build);
	void BuildWestWall(FRoomBuilder& Build);
	void BuildEastWall(FRoomBuilder& Build);
	void BuildSouthWall(FRoomBuilder& Build);
	void BuildCardTable(FRoomBuilder& Build);
	void BuildSconces(FRoomBuilder& Build);
	void BuildFloorDust(FRoomBuilder& Build);
	void BuildDamage(FRoomBuilder& Build);
	void SpawnWindows();
	void BuildClues();

	/** Places a generated model and dresses its slots by name. Never collides; see Blocker. */
	UStaticMeshComponent* Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
		std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	static void Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots);
	/** Every slot every study model uses, dressed from the room's materials. */
	UStaticMeshComponent* PlaceStudy(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation);
	/** Collision only a walking man meets: the interaction trace and the camera pass through. */
	void Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw = 0.f);
	/** Dust settled on a flat top: a soft projected bloom, aimed down from four over it. */
	void Dust(FRoomBuilder& Build, const FVector& Centre, const FVector2D& Size, float Opacity = 0.4f) const;

	// Wall-space helpers: U runs along a wall (X on north/south, Y on east/west), Z is height, and a
	// wall's normal points into the room.
	float WallFace(EWall Wall) const;
	FVector WallNormal(EWall Wall) const;
	FVector WallPoint(EWall Wall, float U, float Z, float Proud) const;
	/** The yaw that turns a model's local +Y — the way every model here faces — out of a wall. */
	static float FacingYaw(EWall Wall);
	/** The openings of a wall, with the trim round them. */
	TArray<FBox2D> Holes(EWall Wall) const;
	/** A sheet of the damask over pieces of a wall, its UVs in centimetres from the wall's origin. */
	void DamaskSheet(EWall Wall, const TArray<FBox2D>& Pieces);
	/** A lacquered panel on a wall, on its own instance with the repeats written out (the 10-09 rule). */
	void PanelPiece(FRoomBuilder& Build, EWall Wall, const FBox2D& Piece, float Proud);
	/** A run of a 1m moulding (bedroom_dado, _skirting, _panel_moulding) along a wall, standing Proud off it. */
	void Moulding(FRoomBuilder& Build, const TCHAR* Name, EWall Wall, float U0, float U1, float Z, float Proud, UMaterialInterface* Mat);
	/** Where a decal on that wall goes and which way it projects. */
	void AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const;
	/** Whether a wall decal of this half extent reaches an opening's trim (+15) or a keep-out (+5). */
	bool WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfU, float HalfZ, bool bOpenings = true) const;
	FBox RoomBox(const UPrimitiveComponent* Part) const;
	/** Records a part within a wall decal's reach of a wall as a keep-out, read off its bounds. */
	void KeepWallDecalsOff(const UPrimitiveComponent* Part);
	/** Records a part standing on the floor as a keep-out for the floor's dust. */
	void KeepFloorDustOff(const UPrimitiveComponent* Part);
	bool FloorDustHitsSomething(const FVector2D& Centre, const FVector2D& HalfExtent) const;

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);

	UPROPERTY(VisibleAnywhere, Category = "Study")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Study")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> LeadStorm;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AStormWindowActor>> Windows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	FStudySetup Setup;
	FRandomStream Random;
	TArray<FOpening> Openings;
	TArray<FWallKeepOut> WallKeepOuts;
	TArray<FBox2D> FloorKeepOuts;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloor;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDamask;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLacquer;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLacquerBox;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLeather;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGilt;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGiltBox;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSteel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPictures;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRug;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBooks;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlasterSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCrystal;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PanelMats;
	/** What every model slot of that name is dressed in (PlaceStudy). */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInterface>> SlotMats;
	/** Flat colours by name: the cards, the chips, the cigar, the shades... */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> Flats;

	/** A named flat colour from Flats. */
	UMaterialInterface* F(const TCHAR* Name) const;
};
