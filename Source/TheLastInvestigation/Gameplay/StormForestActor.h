#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StormForestActor.generated.h"

class USceneComponent;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;

/**
 * Everything outside the house: the ground it stands on, the forest round it, and the night sky
 * over both. One of them for the whole house, owned by the lead storm window.
 *
 * Every window with a view used to bring its own world: a slab of flat sky thirty metres out, a
 * slab of ground and sixteen trees made of cones, each set standing wherever its window happened
 * to face. Four worlds at four ground levels, which met at the corners of the house and stopped
 * dead at the edges of their slabs. A house in the middle of a forest has one forest and one sky,
 * and a strike is in one place in that sky whichever window it is seen from.
 *
 *   Ground   one generated sheet, cut out under the house. Level with the ground floor on three
 *            sides; on the west it is a bank up to the stair window's sill, because the cellar
 *            runs out under it there (see Build).
 *   Forest   spruce, pine, oak and beech (Tools/make_forest.py), instanced, standing back from the
 *            walls across a strip of open ground, thickening into a wood. A far wall of painted
 *            trees closes it (forest_ring), so no gap between trunks ever shows the edge of the world.
 *   Sky      a dome with M_RoomSky on it: a drifting cloud deck, the moon in a break in it, and the
 *            lightning, which lights the cloud from inside and shows its channel.
 *
 * Nothing here casts a shadow or lights anything: the storm's light into the rooms is still the
 * windows' own lights (AStormWindowActor). The one exception is a slab under the west bank, which
 * is what keeps the storm out of the cellar.
 *
 * Built on the first UpdateStorm, not at BeginPlay: where the house is, where its ground is and
 * which way the bank runs are all read off the storm windows, and the lead's BeginPlay runs before
 * any of the others has been spawned.
 */
UCLASS()
class AStormForestActor : public AActor
{
	GENERATED_BODY()

public:
	AStormForestActor();

	/**
	 * Called by the lead window every frame. Flash is how brightly the sky is lit (0..1, already
	 * scaled by how strong the strike is), bChannel whether the bolt itself is showing, Gust the
	 * wind (0..1).
	 */
	void UpdateStorm(float Flash, bool bChannel, float Gust);

	/**
	 * A new strike, somewhere in the sky. A ground strike comes down out of the cloud to behind the
	 * trees; otherwise it is a crawler under the cloud base. World yaw of the strike in degrees, or
	 * NAN to have one picked: mostly on the lead window's side, since that is the room the
	 * directional light throws the strike into.
	 */
	void PlaceStrike(FRandomStream& Random, bool bGroundStrike, float ForcedYaw = NAN);

	/** Where the moon stands, as a direction from the house. East and a little south, low. */
	static FVector MoonDirection();

	/** How far the ground lies under a window actor's origin on the ground floor's level. */
	static constexpr float GroundDrop = 40.f;

private:
	void Build();
	void BuildGround();
	void BuildTrees();
	void BuildFarWall();
	void BuildSky();

	/** Height of the ground at a point, world space. */
	float GroundHeight(float X, float Y) const;
	/** Distance from a point to the house's footprint; zero inside it. */
	float DistanceToHouse(float X, float Y) const;

	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<USceneComponent> ForestRoot;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkyMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TreeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GroundMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WallMaterials;

	bool bBuilt = false;

	/** The house's footprint: the box round every storm window, out to the walls' outer faces. */
	FBox2D House = FBox2D(ForceInit);
	FVector2D HouseCentre = FVector2D::ZeroVector;
	/** The ground on three sides of the house. */
	float GroundZ = 0.f;
	/** The bank on the west: its top, and the wall it lies against. */
	bool bBank = false;
	float BankZ = 0.f;
	float BankX = 0.f;
	/** The lead window, from which the moon has to be seen. */
	FVector LeadWindow = FVector::ZeroVector;

	// What the sky was last told, so a parameter is only written when it changes.
	float LastFlash = -1.f;
	float LastChannel = -1.f;
	float LastGust = -1.f;
};
