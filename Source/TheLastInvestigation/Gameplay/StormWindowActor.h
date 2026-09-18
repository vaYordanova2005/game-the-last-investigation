#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StormWindowActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UDirectionalLightComponent;
class UPointLightComponent;
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
	void TickCurtains(float DeltaTime);
	void TickTrees(float DeltaTime);
	void TickRain(float DeltaTime);
	void TickLightning(float DeltaTime);
	void BeginStrike();

	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<USceneComponent> StormRoot;

	/** Constant, very dim blue fill so the window reads as a faint rectangle between strikes. */
	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<UDirectionalLightComponent> StormAmbientLight;

	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<UDirectionalLightComponent> LightningLight;

	/** Sits just outside the opening; gives the flash a bright near-field core the directional light can't. */
	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<UPointLightComponent> LightningGlow;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Curtains;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Trees;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> RainInstances;

	FStormWindowSetup Setup;

	TArray<FVector> RainPositions;
	TArray<float> RainSpeeds;
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
	FVector2f StrikeLux = FVector2f(22.f, 55.f);

	UPROPERTY(EditAnywhere, Category = "Storm")
	int32 RainDropCount = 420;
};
