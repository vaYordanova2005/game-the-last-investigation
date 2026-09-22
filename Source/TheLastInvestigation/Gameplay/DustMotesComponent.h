#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustMotesComponent.generated.h"

class UInstancedStaticMeshComponent;

/**
 * The dust hanging in the air. Motes are instanced spheres drifting on a slow noise field inside
 * the room's volume; they are invisible until something lights them, which is exactly the point —
 * the lantern's beam and each lightning flash pick them out and the air stops reading as empty.
 *
 * Instanced meshes rather than Niagara because particle systems are Content Browser assets and
 * this project is built entirely from code.
 */
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class UDustMotesComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustMotesComponent();

	/**
	 * Volume the motes fill, in the owner's local space.
	 *
	 * Must be called before the owner's BeginPlay, not from inside it: AActor::BeginPlay
	 * dispatches BeginPlay to every registered component before it returns, so by the time an
	 * owner's own BeginPlay body runs, the motes have already been scattered. The owner is
	 * spawned deferred, so Configure() is the place.
	 */
	void ConfigureVolume(const FVector& InExtent, const FVector& InCenter);

	/** Pushed in each frame by the storm so a gust visibly stirs the room's air. */
	void SetWindStrength(float InWind) { WindStrength = InWind; }

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Motes;

	TArray<FVector> Positions;
	TArray<float> Phases;
	TArray<float> Sizes;

	/** Scratch buffer for the per-frame instance update, kept alive so the tick allocates nothing. */
	TArray<FTransform> Transforms;

	FVector Extent = FVector(400.f, 325.f, 170.f);
	FVector Center = FVector(0.f, 0.f, 170.f);

	FRandomStream Random;
	float ElapsedTime = 0.f;
	float WindStrength = 0.f;

	UPROPERTY(EditAnywhere, Category = "Dust")
	int32 MoteCount = 300;

	/** Baseline sink rate. Dust settles; the noise field is what keeps it alive on the way down. */
	UPROPERTY(EditAnywhere, Category = "Dust")
	float FallSpeed = 3.2f;
};
