#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NurseryActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AStormWindowActor;
class AClueActor;

/** Where the girl's bedroom meets the upstairs corridor, handed over by the corridor that spawns it. */
struct FNurserySetup
{
	/** Y of the room's north face: the corridor's south wall, seen from the other side. */
	float NorthFace = 565.f;
	/** East face, on the same façade as the bedroom's and the corridor's windows. */
	float EastX = 390.f;
	float WallThickness = 20.f;
	/** The door from the corridor, in this room's north wall. The corridor hangs the door itself. */
	float DoorX = -40.f;
	float DoorHalf = 50.f;
	float DoorHeight = 212.f;
};

/**
 * The girl's bedroom, across the corridor from the room the detective wakes in, a few steps along.
 *
 * She was twelve: old enough for a tablet in a pink case, wireless headphones, school textbooks,
 * novels with no pictures in them and a pencil drawing of the house in perspective; young enough
 * that the bears are still on the bed and the drawings she did at six are still taped to the
 * walls. Everything is where she left it on a school day — the homework half done, the backpack
 * against the desk, the pyjamas folded on the bed — under a thin, even dust. The room is meant to
 * be sad rather than frightening: nothing in it moves except the curtains, nothing glows, and the
 * dolls are cloth dolls with stitched faces.
 *
 *              north (the corridor)
 *      +-------------[door]------------------------+
 *      |  wardrobe        rug        calendar  desk |  window
 *  W   |  (west)                              chair | (east, the
 *  E   |  bookcase                                  |  storm)
 *  S   |               toy chest                    |
 *  T   |   toy shelf      +---- bed ----+  nightstand
 *      +------------------+  (head)     +-----------+
 *              south
 *
 * Shares the room's frame, like the corridor, so every number in here reads straight off
 * -RoomShotX/Y/Z. Furniture, toys and her belongings are generated in Blender by
 * Tools/make_nursery.py; the wallpaper, the curtain print and her drawings are baked by
 * Tools/make_nursery_art.py; the rest is the house's photographed surfaces and projected damage.
 */
UCLASS()
class ANurseryActor : public AActor
{
	GENERATED_BODY()

public:
	ANurseryActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FNurserySetup& InSetup, AStormWindowActor* InLeadStorm);

	virtual void BeginPlay() override;

	/** A middling Victorian bedroom: about seven metres by five, and a tall ceiling. */
	static constexpr float RoomWidth = 690.f;
	static constexpr float RoomDepth = 500.f;
	static constexpr float RoomHeight = 320.f;

	/** The one big window, in the east wall. */
	static constexpr float WindowWidth = 180.f;
	static constexpr float WindowSill = 68.f;
	static constexpr float WindowTop = 278.f;

	static constexpr float PictureRail = 246.f;
	static constexpr float Skirting = 16.f;

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

	// Plan helpers, in the room frame. North is -Y, as everywhere in the house.
	float WestX() const { return Setup.EastX - RoomWidth; }
	float EastX() const { return Setup.EastX; }
	float NorthY() const { return Setup.NorthFace; }
	float SouthY() const { return Setup.NorthFace + RoomDepth; }
	float MidX() const { return (WestX() + EastX()) * 0.5f; }
	float MidY() const { return (NorthY() + SouthY()) * 0.5f; }
	float WindowY() const { return MidY() + 10.f; }

	// Where the furniture stands, shared between the pieces and what is put on them.
	/** The bed, head to the south wall, its middle line. */
	float BedX() const { return EastX() - 230.f; }
	/** The bed frame's own footprint, from the model (97 x 201). */
	static constexpr float BedHalfWidth = 48.7f;
	static constexpr float BedLength = 201.4f;
	static constexpr float MattressTop = 51.9f;
	FVector NightstandSeat() const { return FVector(BedX() + BedHalfWidth + 32.f, SouthY() - 24.f, 0.f); }
	/** The desk, against the east wall north of the window, its front to the room. */
	FVector DeskSeat() const { return FVector(EastX() - 29.f, NorthY() + 98.f, 0.f); }
	static constexpr float DeskTop = 75.f;
	FVector WardrobeSeat() const { return FVector(WestX() + 27.f, NorthY() + 165.f, 0.f); }
	FVector BookcaseSeat() const { return FVector(WestX() + 15.f, NorthY() + 300.f, 0.f); }
	FVector ToyShelfSeat() const { return FVector(WestX() + 145.f, SouthY() - 18.f, 0.f); }
	FVector ToyChestSeat() const { return FVector(BedX() - 6.f, SouthY() - BedLength - 38.f, 0.f); }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildWalls(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildDamage(FRoomBuilder& Build);
	void BuildBed(FRoomBuilder& Build);
	void BuildNightstand(FRoomBuilder& Build);
	void BuildWardrobe(FRoomBuilder& Build);
	void BuildDesk(FRoomBuilder& Build);
	void BuildShelves(FRoomBuilder& Build);
	void BuildToys(FRoomBuilder& Build);
	void BuildDrawings(FRoomBuilder& Build);
	void BuildFloorThings(FRoomBuilder& Build);
	void BuildClues();
	void SpawnWindow();

	/** A slab laid on a wall's room face, cut around every opening on that wall. */
	void WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud = 0.f);
	void WallBox(FRoomBuilder& Build, EWall Wall, float U, float Z, float SizeU, float SizeZ, float Depth, float ProudBase, UMaterialInterface* Mat);
	void AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const;
	bool IsOnOpening(EWall Wall, float U, float Z, float HalfU, float HalfZ) const;
	TArray<FBox2D> CutAround(EWall Wall, float U0, float U1, float Z0, float Z1) const;
	float WallFace(EWall Wall) const;
	FVector WallNormal(EWall Wall) const;
	FVector WallPoint(EWall Wall, float U, float Z, float Proud) const;
	/** The yaw that turns a prop's local +Y — the way every prop here faces — out of a wall. */
	static float FacingYaw(EWall Wall);

	/** Puts the room's materials on a generated model's named slots. */
	void Dress(UStaticMeshComponent* Mesh, const TMap<FName, UMaterialInterface*>& Slots) const;

	/** One of her drawings, from the baked atlas: Index 0..7, on a plane facing Normal, its top towards Up. */
	UStaticMeshComponent* Drawing(FRoomBuilder& Build, int32 Index, const FVector& Centre, const FVector& Normal, const FVector& Up, float Scale = 1.f);
	/** A drawing taped to a wall, a little crooked, with tape at the top corners (and sometimes the bottom). */
	void WallDrawing(FRoomBuilder& Build, EWall Wall, float U, float Z, int32 Index, float Tilt, bool bTapeBottom);
	/** A closed book lying flat: two boards, a spine and the block of pages. Base is the middle of its underside. */
	void Book(FRoomBuilder& Build, const FVector& Base, float Yaw, const FVector& Size, UMaterialInterface* Cover) const;
	/** A book standing on its foot, spine out along Out. */
	void StandingBook(FRoomBuilder& Build, const FVector& Foot, const FVector& Out, float Lean, const FVector& Size, UMaterialInterface* Cover) const;
	/** Lines of handwriting in the plane of Sheet (the kitchen's recipe-book Writing). */
	void Writing(FRoomBuilder& Build, const FTransform& Sheet, float Width, float Height, int32 Lines, int32 Seed, UMaterialInterface* Ink) const;
	void Stroke(FRoomBuilder& Build, const FTransform& Sheet, float X, float Y, float Length, float Weight, UMaterialInterface* Ink) const;
	/** A coloured pencil or a crayon lying at Base along Yaw. */
	void Pencil(FRoomBuilder& Build, const FVector& Base, float Yaw, float Length, float Diameter, UMaterialInterface* Mat) const;
	/** Dust settled on a flat top: a soft projected bloom, aimed down. */
	void Dust(FRoomBuilder& Build, const FVector& Centre, const FVector2D& Size, float Opacity = 0.4f) const;

	bool IsFloorSpotClear(float X, float Y, float Radius) const;
	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);

	UPROPERTY(VisibleAnywhere, Category = "Nursery")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Nursery")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> LeadStorm;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> Window;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	TArray<FOpening> Openings;
	/** What stands on the floor, in plan, so the scattered things keep out of it. */
	TArray<FBox2D> Footprints;
	FNurserySetup Setup;
	FRandomStream Random;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaperPeel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloorboards;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloorboardsWorn;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTrim;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPink;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPinkInside;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWhite;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBareWood;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMattress;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatQuilt;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSheet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRug;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBearFur;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRabbitFur;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPad;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDollSkin;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDollDress;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatDollHair;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBackpack;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPajamas;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGarment;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGarmentDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaperDamp;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPageEdge;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTape;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShadow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatInk;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPencil;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubble;
	/** Flat colours: buttons, plastic, the dull pastels of her pencils, covers and stickers. */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> Flats;
	/** One instance per drawing in the atlas, made lazily. */
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DrawingMats;

	/** A named flat colour from Flats. */
	UMaterialInterface* F(const TCHAR* Name) const;
};
