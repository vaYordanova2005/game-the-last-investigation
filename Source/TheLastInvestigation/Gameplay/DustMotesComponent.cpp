#include "DustMotesComponent.h"
#include "RoomBuildLibrary.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

UDustMotesComponent::UDustMotesComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDustMotesComponent::ConfigureVolume(const FVector& InExtent, const FVector& InCenter)
{
	Extent = InExtent;
	Center = InCenter;
}

void UDustMotesComponent::BeginPlay()
{
	Super::BeginPlay();

	Random.Initialize(771402);

	AActor* Owner = GetOwner();
	USceneComponent* Parent = Owner ? Owner->GetRootComponent() : nullptr;
	if (!Parent)
	{
		return;
	}

	FRoomBuilder Build(Owner, Parent);
	// Bright albedo is right here: a mote is only ever seen as a lit speck against darkness, so it
	// needs to catch what little light reaches it. Rough, because dust is not glossy.
	UMaterialInstanceDynamic* MoteMat = Build.Material(FLinearColor(0.62f, 0.58f, 0.50f), 1.f);
	Motes = Build.Instances(FRoomShapes::Sphere(), MoteMat);
	if (!Motes)
	{
		return;
	}

	Motes->SetCastShadow(false); // hundreds of shadow-casting specks would be pure cost for nothing

	Positions.Reserve(MoteCount);
	Phases.Reserve(MoteCount);
	Sizes.Reserve(MoteCount);

	for (int32 i = 0; i < MoteCount; ++i)
	{
		const FVector Position = Center + FVector(
			Random.FRandRange(-Extent.X, Extent.X),
			Random.FRandRange(-Extent.Y, Extent.Y),
			Random.FRandRange(-Extent.Z, Extent.Z));

		Positions.Add(Position);
		Phases.Add(Random.FRandRange(0.f, 200.f));
		Sizes.Add(Random.FRandRange(0.7f, 2.1f)); // sub-centimetre specks

		Motes->AddInstance(FTransform(FRotator::ZeroRotator, Position, FVector(Sizes[i] / 100.f)));
	}
}

void UDustMotesComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Motes || Positions.Num() == 0)
	{
		return;
	}

	ElapsedTime += DeltaTime;

	// Wind coming through the broken window pushes the air along -Y and lifts it slightly.
	const float Gust = FMath::Lerp(0.6f, 5.5f, FMath::Clamp(WindStrength, 0.f, 1.f));

	TArray<FTransform> Transforms;
	Transforms.Reserve(Positions.Num());

	for (int32 i = 0; i < Positions.Num(); ++i)
	{
		FVector& Position = Positions[i];
		const float Phase = Phases[i];

		// Two Perlin samples per axis-pair give each mote its own lazy, non-repeating path. Cheap
		// enough at this count, and far more convincing than a sine wobble.
		const float DriftX = FMath::PerlinNoise1D(ElapsedTime * 0.11f + Phase);
		const float DriftY = FMath::PerlinNoise1D(ElapsedTime * 0.13f + Phase * 1.7f);
		const float Lift = FMath::PerlinNoise1D(ElapsedTime * 0.17f + Phase * 2.3f);

		Position.X += DriftX * 4.f * DeltaTime * Gust;
		Position.Y += (DriftY * 4.f - Gust * 1.6f) * DeltaTime;
		Position.Z += (Lift * 5.f - FallSpeed) * DeltaTime;

		// Wrap through the volume so the room never runs out of dust.
		const FVector Local = Position - Center;
		if (Local.Z < -Extent.Z)
		{
			Position.Z = Center.Z + Extent.Z;
		}
		else if (Local.Z > Extent.Z)
		{
			Position.Z = Center.Z - Extent.Z;
		}
		if (FMath::Abs(Local.X) > Extent.X)
		{
			Position.X = Center.X - FMath::Sign(Local.X) * Extent.X;
		}
		if (FMath::Abs(Local.Y) > Extent.Y)
		{
			Position.Y = Center.Y - FMath::Sign(Local.Y) * Extent.Y;
		}

		Transforms.Add(FTransform(FRotator::ZeroRotator, Position, FVector(Sizes[i] / 100.f)));
	}

	Motes->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}
