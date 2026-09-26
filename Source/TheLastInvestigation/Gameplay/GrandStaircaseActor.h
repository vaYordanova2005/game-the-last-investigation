#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrandStaircaseActor.generated.h"

class FRoomBuilder;
class USceneComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UDustMotesComponent;
class AStormWindowActor;
class AHallDoorActor;
class AClueActor;

/** Where the stair hall meets the corridor, handed over by the corridor that spawns it. */
struct FStairHallSetup
{
	/** X of the hall's east face: the corridor's west wall, seen from the other side. */
	float EastFace = -1270.f;
	/** The corridor's centreline, which is also the hall's. */
	float CenterY = 440.f;
	float WallThickness = 20.f;
	/** The archway from the corridor onto the gallery. */
	float OpeningWidth = 180.f;
	float OpeningHeight = 246.f;
};

/**
 * The stair hall: the upstairs corridor's far end opens onto a gallery, and the gallery looks down
 * into the entrance hall two storeys below.
 *
 * An imperial stair, because it is the one plan that puts everything the brief wants into a single
 * look from the gallery. A wide central flight rises from the entrance hall to a half-landing
 * against the far wall; the stained-glass window stands over the landing; two narrower flights
 * turn back from the landing along the side walls and arrive at the gallery either side of where
 * the player is standing. From the corridor archway the window is dead ahead across the void, the
 * chandelier hangs at eye level between the two, and the floor of the hall is down in the dark
 * where the lantern does not reach.
 *
 *            west (window)                                   east (corridor)
 *      +-------------+----------------+------------------+------------------+
 *      |             |  north flight  |  north gallery   |                  |
 *      |  half-      +----------------+------------------+                  |
 *      |  landing    |  alcove        |                  |    east          |
 *      |             +----------------+     void         |    gallery   <== corridor
 *      |  (-170)     |  central flight|   (open to the   |    (0)           |
 *      |             +----------------+    hall below)   |                  |
 *      |             |  alcove        |                  |                  |
 *      |             +----------------+------------------+                  |
 *      |             |  south flight  |  south gallery   |                  |
 *      +-------------+----------------+------------------+------------------+
 *
 * Levels: the corridor floor is 0, the half-landing -170, the entrance hall -340; the hall is open
 * to a beamed ceiling at +430. North and south are the corridor's (north is -Y).
 *
 * Shares the room's frame, like the corridor: it is spawned at the room's origin, so every number
 * in here is in the same space as -RoomShotX/Y/Z. Built at BeginPlay from the same primitives,
 * photographed surfaces and projected damage as the rest of the house.
 */
UCLASS()
class AGrandStaircaseActor : public AActor
{
	GENERATED_BODY()

public:
	AGrandStaircaseActor();

	/** Must be called before BeginPlay, i.e. on a deferred spawn. */
	void Configure(const FStairHallSetup& InSetup, AStormWindowActor* InLeadStorm);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// Levels.
	static constexpr float GroundZ = -340.f;
	static constexpr float LandingZ = -170.f;
	static constexpr float CeilingZ = 430.f;
	/** Depth of the floor structure under the gallery and the landing. */
	static constexpr float FloorDepth = 30.f;

	// Plan.
	static constexpr float HallLength = 1000.f;      // east to west
	static constexpr float HallWidth = 1000.f;       // north to south
	static constexpr float EastGalleryDepth = 200.f;
	static constexpr float SideWidth = 160.f;        // side galleries and the return flights
	static constexpr float CentralWidth = 280.f;
	static constexpr float LandingDepth = 200.f;

	// The stair. Ten risers of 17 to each half-landing, on a 30cm going: an easy, grand stair, which
	// is what a house like this built its main stair as, and comfortable to walk in first person.
	static constexpr int32 RisersPerFlight = 10;
	static constexpr float Rise = 17.f;
	static constexpr float Going = 30.f;

	// The window over the landing, in the wall's own terms. Must match Tools/make_stained_glass.py.
	static constexpr float WindowWidth = 300.f;
	static constexpr float WindowHeight = 450.f;
	static constexpr float WindowSill = 70.f;        // above the landing

private:
	enum class EWall : uint8 { North, South, East, West };

	/** A hole in one wall, in that wall's U (along) / Z (world height) space. */
	struct FOpening
	{
		EWall Wall;
		float CenterU;
		float HalfU;
		float BottomZ;
		float TopZ;
	};

	// Plan helpers, all in the room frame.
	float EastX() const { return Setup.EastFace; }
	float WestX() const { return Setup.EastFace - HallLength; }
	float NorthY() const { return Setup.CenterY - HallWidth * 0.5f; }
	float SouthY() const { return Setup.CenterY + HallWidth * 0.5f; }
	float GalleryEdgeX() const { return EastX() - EastGalleryDepth; }
	float LandingEdgeX() const { return WestX() + LandingDepth; }
	/** Where the return flights reach the gallery and the central flight leaves the hall floor. */
	float FlightEastX() const { return LandingEdgeX() + Going * (RisersPerFlight - 1); }
	float NorthInnerY() const { return NorthY() + SideWidth; }
	float SouthInnerY() const { return SouthY() - SideWidth; }
	float CentralNorthY() const { return Setup.CenterY - CentralWidth * 0.5f; }
	float CentralSouthY() const { return Setup.CenterY + CentralWidth * 0.5f; }
	float ChandelierX() const { return (GalleryEdgeX() + FlightEastX()) * 0.5f; }

	void CacheMaterials(FRoomBuilder& Build);
	void BuildShell(FRoomBuilder& Build);
	void BuildFloors(FRoomBuilder& Build);
	void BuildFlights(FRoomBuilder& Build);
	void BuildBalustrades(FRoomBuilder& Build);
	void BuildWallFinish(FRoomBuilder& Build);
	void BuildCeiling(FRoomBuilder& Build);
	void BuildStainedGlass(FRoomBuilder& Build);
	void BuildChandelier(FRoomBuilder& Build);
	void BuildDamage(FRoomBuilder& Build);
	void BuildFurniture(FRoomBuilder& Build);
	void BuildDebris(FRoomBuilder& Build);
	void BuildFigure(FRoomBuilder& Build);
	void BuildClues();
	void SpawnDoors();
	void SpawnWindow();

	/** One flight: treads, risers, runner, stringers, and the invisible ramp that is walked on. */
	void BuildFlight(FRoomBuilder& Build, const FVector& FootNosing, const FVector& Up, float Width, int32 Seed, bool bRunner);

	/** A run of banister between two points on the handrail line: rail, balusters, bottom rail. */
	void Banister(FRoomBuilder& Build, const FVector& From, const FVector& To, int32 Seed);
	/** A newel post standing on Base, Height tall to the top of its cap. Grand is the pair at the foot. */
	void Newel(FRoomBuilder& Build, const FVector& Base, float Height, bool bGrand);
	/** An invisible wall along a line, so a banister with gaps in it still stops a man. */
	void Blocker(FRoomBuilder& Build, const FVector& From, const FVector& To, float Height);

	/** A slab laid on a wall's hall face, cut around every opening on that wall. */
	void WallFill(FRoomBuilder& Build, EWall Wall, float U0, float U1, float Z0, float Z1, UMaterialInterface* Mat, float Proud = 0.f);
	/** A box standing Depth proud of a wall's hall face. */
	void WallBox(FRoomBuilder& Build, EWall Wall, float U, float Z, float SizeU, float SizeZ, float Depth, float ProudBase, UMaterialInterface* Mat);
	/** Where a decal on that wall goes and which way it projects. */
	void AimAt(EWall Wall, float U, float Z, float Roll, FVector& OutLocation, FRotator& OutRotation) const;
	bool IsOnOpening(EWall Wall, float U, float Z, float HalfU, float HalfZ) const;
	float WallFace(EWall Wall) const;
	FVector WallNormal(EWall Wall) const;
	FVector WallPoint(EWall Wall, float U, float Z, float Proud) const;

	AClueActor* SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description);

	UPROPERTY(VisibleAnywhere, Category = "Stair Hall")
	TObjectPtr<USceneComponent> HallRoot;

	UPROPERTY(VisibleAnywhere, Category = "Stair Hall")
	TObjectPtr<UDustMotesComponent> DustMotes;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> LeadStorm;

	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> Window;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AHallDoorActor>> Doors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AClueActor>> Clues;

	/** The chandelier hangs from this, and this is what the wind moves. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> ChandelierPivot;

	/** The man on the stairs, who is only there while the sky is lit. See Tick. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Figure;

	/** The stained glass, driven from the storm every frame. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StainedGlass;

	TArray<FOpening> Openings;
	FStairHallSetup Setup;
	FRandomStream Random;
	float ElapsedTime = 0.f;

	int32 FigureStrike = 0;
	bool bFigureThisStrike = false;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaster;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWallpaperDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWainscot;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOak;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatOakDark;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTread;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTreadWorn;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloorboards;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatFloorboardsWorn;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatTiles;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatMarble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCeiling;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBeam;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCarpet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatRubble;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaper;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPaperDamp;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCloth;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatIron;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBrass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatCrystal;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWeb;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatVoid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatBackRoom;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatWax;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatStem;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPetal;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatShadow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatUmbrella;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatYellow;
};
