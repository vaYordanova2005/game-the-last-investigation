#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StormWindowActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UDirectionalLightComponent;
class UPointLightComponent;
class URectLightComponent;
class UMaterialInstanceDynamic;

/** Geometry of the wall opening this storm is seen through, handed over by the room that spawns it. */
struct FStormWindowSetup
{
	float OpeningWidth = 260.f;
	float SillHeight = 85.f;
	float TopHeight = 250.f;
	float WallThickness = 20.f;
};

/**
 * Everything on the far side of the glass, plus the storm lighting that reaches into the room.
 *
 * Owns three things that have to stay in sync: the window itself (frame, cracked glass, torn
 * curtains), the world outside it (ground, treeline, falling rain), and the lightning. The
 * lightning is a *shadow-casting directional light* rather than the point light the greybox used,
 * because the brief's "long moving shadows" only happen if the flash comes from far away and
 * outside — a point light at the window casts shadows that fan out from the opening instead.
 *
 * Convention: the actor is spawned facing so that +X is outward, away from the room.
 */
UCLASS()
class AStormWindowActor : public AActor
{
	GENERATED_BODY()

public:
	AStormWindowActor();

	/** Must be called before BeginPlay (i.e. via a deferred spawn) so the frame matches the wall gap. */
	void Configure(const FStormWindowSetup& InSetup);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 0..1 gust strength, shared so wallpaper strips elsewhere in the room breathe with the same wind. */
	float GetWindGust() const;

	/** 0..1 how bright the current lightning flash is, for anything that needs to react to it. */
	float GetFlashAlpha() const { return FlashAlpha; }

private:
	void BuildWindow();
	void BuildOutsideWorld();
	void BuildRain();
	void BuildLightningBolts();
	void TickCurtains(float DeltaTime);
	void TickTrees(float DeltaTime);
	void TickRain(float DeltaTime);
	void TickLightning(float DeltaTime);
	void BeginStrike();

	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<USceneComponent> StormRoot;

	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<UDirectionalLightComponent> LightningLight;

	/** Sits just outside the opening; gives the flash a bright near-field core the directional light can't. */
	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<UPointLightComponent> LightningGlow;

	/**
	 * The overcast sky, as a light the size of the window and standing just outside it.
	 *
	 * This is what actually lights the room between strikes, and it replaces trying to do that job
	 * with the distant directional light — which could not do it: at the shallow angle the long
	 * shadows need, its beam spends forty metres inside the treeline before it ever reaches the
	 * glass, so the room stayed black no matter how many lux it was given. A rect light in the
	 * opening is also simply what is physically there — a window is an area source of sky — and
	 * because it sits *outside* the glass, the muntin bars throw their grid across the far wall.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<URectLightComponent> SkyPortal;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Curtains;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Trees;

	/**
	 * The strikes themselves, seen through the window. Three are built up front and one is shown
	 * per strike: a bolt has to be a fixed shape for the fraction of a second it exists, and
	 * rebuilding its geometry mid-flash would cost more than keeping three spare ones around.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Bolts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BoltMaterials;

	/** The overcast sky behind the trees, brightened while a flash lasts. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkyMaterial;

	int32 ActiveBolt = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> RainInstances;

	FStormWindowSetup Setup;

	TArray<FVector> RainPositions;
	TArray<float> RainSpeeds;

	/** Scratch buffer for the per-frame instance update, kept alive so the tick allocates nothing. */
	TArray<FTransform> RainTransforms;
	TArray<float> TreePhases;

	FRandomStream Random;
	float ElapsedTime = 0.f;

	// Lightning state. A strike is a burst of 2-4 sub-flashes rather than a single blink, which is
	// what makes real lightning read as unpredictable instead of as a metronome.
	float TimeUntilNextStrike = 4.f;
	int32 SubFlashesRemaining = 0;
	float SubFlashTimer = 0.f;
	bool bSubFlashOn = false;
	float StrikeIntensity = 0.f;
	float FlashAlpha = 0.f;

	/** Seconds between strikes. Short — the brief asks for a relentless, frequent storm. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	FVector2f StrikeInterval = FVector2f(3.5f, 9.f);

	/** Lux at the peak of a flash. Cold blue-white, and bright enough to momentarily flatten the lantern. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	FVector2f StrikeLux = FVector2f(60.f, 150.f);

	UPROPERTY(EditAnywhere, Category = "Storm")
	int32 RainDropCount = 420;

	/**
	 * Lux the storm sky gives off between strikes — the light's floor, never switched off.
	 *
	 * This is the dial for how dark the room is. It has to be high enough to print a pale,
	 * muntin-barred rectangle of window light across the door wall, because that patch is the
	 * room's second subject after the lantern; low enough that the corners the lantern has not
	 * reached are still genuinely dark.
	 */
	UPROPERTY(EditAnywhere, Category = "Storm")
	float StormAmbientLux = 1.4f;

	/**
	 * Candelas of overcast sky coming through the opening. This is the dial for how dark the room
	 * is: it sets the pale rectangle on the door wall, the glow on the sill, and how much of the
	 * far corners the player can make out before the lantern reaches them.
	 */
	UPROPERTY(EditAnywhere, Category = "Storm")
	float SkyPortalCandelas = 310.f;

	/** How brightly the overcast sky glows between strikes, in emissive units. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	float SkyGlowFloor = 1.8f;
};
