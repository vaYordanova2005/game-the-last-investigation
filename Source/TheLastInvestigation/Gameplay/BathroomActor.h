#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BathroomActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AClueActor;

/** Where the bathroom meets the upstairs corridor, handed over by the corridor that spawns it. */
struct FBathroomSetup
{
	/** Y of the room's north face: the corridor's south wall, seen from the other side. */
	float NorthFace = 565.f;
	/** X of the east face: the girl's room's west wall, seen from this side. */
	float EastX = -320.f;
	float WallThickness = 20.f;
	/** The door from the corridor, in this room's north wall. The corridor hangs the door itself. */
	float DoorX = -520.f;
	float DoorHalf = 50.f;
	float DoorHeight = 212.f;
	/**
	 * The leaf's hinge, on the door's west jamb, and how far it opens into the room: the floor's
	 * decals keep off the quarter disc it sweeps (the 10-05 rule).
	 */
	float HingeX = -568.f;
	float DoorLeafWidth = 96.f;
	float DoorOpenYaw = 89.f;
	/** The corridor's ceiling: the strip of north wall above it is this room's to build. */
	float CorridorHeight = 305.f;
};

/**
 * The upstairs bathroom, off the corridor between the girl's room and the stairs.
 *
 * It was done over when the master bedroom was, and it is the same idea carried further: the
 * Victorian bones kept and made much of, and modern things put in among them. White bevelled
 * tiles to shoulder height with a black liner and a black cove skirting, bottle-green paint over
 * them, a hexagon mosaic floor with black flowers in it; a cast-iron roll-top bath on brass claw
 * feet with a floor-standing brass filler, a high-level cistern on its brackets with its chain, a
 * brass towel rail. Into that went a walk-in shower behind a black steel-framed screen (its riser
 * and rose exposed and in brass again), a floating vanity in deep green under a marble slab, and
 * a big mirror with globe bulbs down both sides.
 *
 * There is no window (the user's call): nothing lights this room but the lantern, and the storm is
 * only heard. Most of the mirror's bulbs are smashed, the pendant's globe is in pieces on the floor
 * under it, and one of the sconces over the bath has lost its shade. Everything else is as it was
 * left, under the dust: three toothbrushes in a glass, his razor on its stand, a towel over the
 * side of the bath with its end in an inch of brown water, and her rubber duck floating in it.
 *
 *                        north (the corridor)
 *      +-- shower ---+------------------------[door]----- fern ----+
 *      | rose  valve |screen                                        |
 *  W   +- - - - - - -+                                  towel rail  |  E
 *  E   | hamper                                                     |  (the girl's
 *  S   | VANITY + MIRROR          (pendant, its glass on the floor) |   room)
 *  T   |                                                 roll    WC |
 *      | tiles     chair   [====== BATH ======] filler        cistern
 *      +------------------- sconce ------ sconce -------------------+
 *                        south (no window)
 *
 * Shares the corridor's frame (the room's), so every number in here reads straight off
 * -RoomShotX/Y/Z. The fittings are generated in Blender by Tools/make_bathroom.py, the wall tiles
 * and the floor's mosaic baked by Tools/make_bathroom_art.py; the rest is the house's paint,
 * marble and projected damage.
 */
UCLASS()
class ABathroomActor : public AActor
{
	GENERATED_BODY()

public:
	ABathroomActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FBathroomSetup& InSetup);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Six metres by five: a good bedroom's worth, which is what a Victorian house made its bathroom from. */
	static constexpr float RoomWidth = 600.f;
	static constexpr float RoomDepth = 500.f;
	static constexpr float RoomHeight = 320.f;

	// Shared with Tools/make_bathroom.py.
	static constexpr float BathLength = 172.f;
	static constexpr float BathWidth = 78.f;
	static constexpr float BathRim = 65.3f;
	static constexpr float FillerReach = 34.f;
	static constexpr float VanityWidth = 120.f;
	static constexpr float VanityDepth = 52.f;
	static constexpr float VanityTop = 86.f;
	static constexpr float MirrorWidth = 76.f;
	static constexpr float MirrorHeight = 100.f;
	static constexpr float MirrorSide = 12.f;
	static constexpr float MirrorDepth = 4.f;
	/** The bulb sockets' mouths: across the glass's middle, up from it, and out from the frame's back. */
	static constexpr float BulbX = MirrorWidth * 0.5f + MirrorSide * 0.5f;
	static constexpr float SocketY = MirrorDepth + 2.4f;
	static constexpr float ScreenWidth = 100.f;
	static constexpr float WCBack = 34.f;
	static constexpr float WCSeat = 43.f;
	static constexpr float RailWidth = 64.f;
	static constexpr float ChairSeat = 46.f;

	// Shared with Tools/make_bathroom_art.py.
	/** One repeat of the wall tiles: 4 across and 8 up of 15 x 7.5. */
	static constexpr float TileSheetCm = 60.f;
	/** One repeat of the floor's mosaic, across and along: a hexagon grid's periods are not square. */
	static constexpr float MosaicW = 54.f;
	static constexpr float MosaicH = 51.96f;

	/** The tiles: to shoulder height everywhere, to over the head in the shower, and their trim. */
	static constexpr float TileTop = 135.f;
	static constexpr float ShowerTileTop = 225.f;
	static constexpr float CapHeight = 5.f;
	static constexpr float PencilZ = 112.f;
	/** The tile sheet's face, off the plaster; the liner and the skirting stand on it. */
	static constexpr float TileProud = 0.9f;
	/** The door's architrave on this side: its width each side of the opening, and over the head. */
	static constexpr float DoorCasing = 9.f;

	/** The walk-in shower's slate tray in the north-west corner, and its screen along the tray's east edge. */
	static constexpr float TrayWidth = 130.f;
	static constexpr float TrayDepth = 125.f;
	static constexpr float TrayTop = 4.f;

private:
	enum class EWall : uint8 { North, South, East, West };

	// The room's inner faces, in the corridor's frame. North is -Y, as everywhere in the house.
	float WestX() const { return Setup.EastX - RoomWidth; }
	float EastX() const { return Setup.EastX; }
	float NorthY() const { return Setup.NorthFace; }
	float SouthY() const { return Setup.NorthFace + RoomDepth; }
	float MidX() const { return (WestX() + EastX()) * 0.5f; }
	float MidY() const { return (NorthY() + SouthY()) * 0.5f; }

	// Where things stand, shared between the pieces, what is on them, and the decals' keep-outs.
	/** The bath: its middle, its long axis along X, its back 30 off the south wall. */
	FVector BathSeat() const { return FVector(MidX() - 20.f, SouthY() - 30.f - BathWidth * 0.5f, 0.f); }
	/** The filler at the bath's east end, its spout reaching back over the end. */
	FVector FillerSeat() const { return BathSeat() + FVector(BathLength * 0.5f + 28.f, 0.f, 0.f); }
	/** The vanity, its back on the west wall's tiles; the mirror over it. */
	static constexpr float VanityY = 850.f;
	FVector VanitySeat() const { return FVector(WestX() + TileProud + VanityDepth * 0.5f, VanityY, 0.f); }
	static constexpr float MirrorZ = 177.f;
	/** The mirror's back stands clear of the tile cap (2.2 proud) it hangs over. */
	static constexpr float MirrorProud = 2.4f;
	/** The WC on the east wall, its pan's origin WCBack off the plaster. */
	FVector ToiletSeat() const { return FVector(EastX() - WCBack - TileProud, 975.f, 0.f); }
	FVector RailSeat() const { return FVector(EastX() - 17.f, 720.f, 0.f); }
	FVector ChairSeatAt() const { return FVector(BathSeat().X - BathLength * 0.5f - 46.f, BathSeat().Y - 18.f, 0.f); }
	FVector FernSeat() const { return FVector(EastX() - 80.f, NorthY() + 62.f, 0.f); }
	FVector HamperSeat() const { return FVector(WestX() + 32.f, NorthY() + TrayDepth + 52.f, 0.f); }
	/** The pendant's rose, in the middle of the ceiling; its globe's pieces on the floor under it. */
	FVector PendantAt() const { return FVector(MidX(), MidY() + 5.f, RoomHeight); }
	/** The two sconces over the bath on the south wall, either side of its middle. */
	static constexpr float SconceSpread = 70.f;
	static constexpr float SconceZ = 175.f;
	/** The patch on the west wall where tiles came away, south of the vanity, and their pieces under it. */
	static constexpr float FallenTilesY = 985.f;
	static constexpr float FallenTilesZ = 52.f;

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildFloor(FRoomBuilder& Build);
	void BuildWalls(FRoomBuilder& Build);
	void BuildTileTrim(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildBath(FRoomBuilder& Build);
	void BuildShower(FRoomBuilder& Build);
	void BuildVanity(FRoomBuilder& Build);
	void BuildMirror(FRoomBuilder& Build);
	void BuildLamps(FRoomBuilder& Build);
	void BuildToilet(FRoomBuilder& Build);
	void BuildFurniture(FRoomBuilder& Build);
	void BuildDecay(FRoomBuilder& Build);
	void BuildClues();

	/**
	 * Places a generated model and dresses every slot from SlotMats by its name, then from
	 * Overrides. Never collides; see Blocker.
	 */
	UStaticMeshComponent* Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
		std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Overrides);
	void Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Overrides) const;
	/** Collision only a walking man meets: the interaction trace and the camera pass through. */
	void Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw);
	/** Dust settled on a flat top: a soft projected bloom, aimed down from four over it. */
	void Dust(FRoomBuilder& Build, const FVector& Top, const FVector2D& Size, float Yaw, float Opacity = 0.4f) const;

	// Wall-space helpers: U runs along a wall (X on north/south, Y on east/west), Z is height, and a
	// wall's normal points into the room.
	float WallFace(EWall Wall) const;
	FVector WallNormal(EWall Wall) const;
	FVector WallPoint(EWall Wall, float U, float Z, float Proud) const;
	/** The yaw that turns a model's local +Y — the way every model here faces — out of a wall. */
	static float FacingYaw(EWall Wall);
	/** The door's opening on the north wall, bare or with its architrave. */
	TArray<FBox2D> Openings(EWall Wall, bool bWithArchitrave) const;
	/** The tiled runs of a wall: (U0, U1, top), to over the head in the shower's corner. */
	TArray<FVector> TileRuns(EWall Wall) const;
	/**
	 * What stands on or against a wall within a wall decal's reach, as (along, up) rectangles: the
	 * vanity and the mirror with its bulbs, the shower's fittings and screen, the cistern and its
	 * pipe, the roll holder, the rail's feet, the sconces. Read off the same seats the pieces are
	 * placed from.
	 */
	TArray<FBox2D> WallKeepOuts(EWall Wall) const;
	/** Whether a wall decal at (U, Z), of this size and roll, reaches the door (+15) or a keep-out (+5). */
	bool WallDecalHitsSomething(EWall Wall, float U, float Z, const FVector2D& Size, float Roll) const;
	/** Whether a floor decal's rolled footprint reaches the door leaf's sweep or its sill. */
	bool FloorDecalReachesDoor(const FVector2D& Centre, const FVector2D& Size, float Roll) const;
	/** A straight run of trim (bathroom_tile_cap, _pencil, _skirt) on a wall, along U or up Z, standing Proud off the plaster. */
	void Trim(FRoomBuilder& Build, const TCHAR* Name, EWall Wall, float U, float Z, float Length, bool bVertical, float Proud, UMaterialInterface* Mat);

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation);

	UPROPERTY(VisibleAnywhere, Category = "Bathroom")
	TObjectPtr<USceneComponent> RoomRoot;

	UPROPERTY(VisibleAnywhere, Category = "Bathroom")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	FBathroomSetup Setup;
	FRandomStream Random;
	/** Whether the eye was in here last frame, so the storms' glows are told only when it changes. */
	bool bEyeInside = false;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloor;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTiles;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWall;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTrim;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTrimBox;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatLacquer;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMarble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatSlate;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTowel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTowelGreen;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatScreenGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBulbGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	/** Flat colours by name: brass, enamel, the cast iron, the mirror, the opal glass... */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> Flats;
	/** What every model slot of that name is dressed in: the flats, and the sheets and glasses. */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInterface>> SlotMats;

	/** A named flat colour from Flats. */
	UMaterialInterface* F(const TCHAR* Name) const;
};
