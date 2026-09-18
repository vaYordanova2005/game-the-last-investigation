#include "DoorActor.h"
#include "DetectiveCharacter.h"
#include "RoomBuildLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"

ADoorActor::ADoorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// HingeRoot sits at the hinge edge; the leaf is offset sideways from it, so rotating
	// HingeRoot swings the leaf like a real door instead of spinning it around its own center.
	HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRoot"));
	SetRootComponent(HingeRoot);

	DoorLeaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorLeaf"));
	DoorLeaf->SetupAttachment(HingeRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		DoorLeaf->SetStaticMesh(CubeMeshFinder.Object);
	}

	// Cube is 100uu per side; scale to a door-leaf slab and offset so the hinge edge lines up with the root.
	const FVector DoorSize(4.f, 100.f, 210.f);
	DoorLeaf->SetRelativeScale3D(DoorSize / 100.f);
	DoorLeaf->SetRelativeLocation(FVector(0.f, DoorSize.Y * 0.5f, DoorSize.Z * 0.5f));
	DoorLeaf->SetMobility(EComponentMobility::Movable);
	DoorLeaf->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorLeaf->SetCollisionResponseToAllChannels(ECR_Block);
}

void ADoorActor::BeginPlay()
{
	Super::BeginPlay();
	BuildDoorDetail();
}

void ADoorActor::BuildDoorDetail()
{
	// Detail is attached to HingeRoot in the leaf's own space, so it swings with the door. Built
	// here rather than in the constructor because FRoomBuilder creates components at runtime.
	FRoomBuilder Build(this, HingeRoot);

	UMaterialInstanceDynamic* WoodMat = Build.Material(RoomPalette::RottenWood, 0.98f);
	UMaterialInstanceDynamic* PaintMat = Build.Material(FLinearColor(0.088f, 0.094f, 0.082f), 0.9f);
	UMaterialInstanceDynamic* IronMat = Build.Material(RoomPalette::Iron, 0.65f, 0.9f);
	UMaterialInstanceDynamic* RustMat = Build.Material(RoomPalette::Rust, 0.95f, 0.6f);
	UMaterialInstanceDynamic* CrackMat = Build.Material(FLinearColor(0.006f, 0.006f, 0.006f), 1.f);

	if (WoodMat)
	{
		DoorLeaf->SetMaterial(0, WoodMat);
	}

	const float LeafY = 50.f;   // the leaf's centre, offset from the hinge
	const float LeafZ = 105.f;
	const float FaceX = 2.5f;   // just proud of the 4uu-thick leaf, on the room side

	// Flaking paint: patches of the door's original colour, with bare wood showing between them.
	for (int32 i = 0; i < 9; ++i)
	{
		const float PatchY = LeafY + FMath::FRandRange(-42.f, 42.f);
		const float PatchZ = LeafZ + FMath::FRandRange(-95.f, 95.f);
		Build.Box(FVector(FaceX, PatchY, PatchZ), FRotator(0.f, 0.f, FMath::FRandRange(-4.f, 4.f)),
			FVector(1.f, FMath::FRandRange(18.f, 44.f), FMath::FRandRange(20.f, 70.f)), PaintMat, /*bBlockingCollision*/ false);
	}

	// Deep splits running with the grain.
	for (int32 i = 0; i < 4; ++i)
	{
		const float CrackY = LeafY + FMath::FRandRange(-40.f, 40.f);
		Build.Box(FVector(FaceX + 0.6f, CrackY, LeafZ + FMath::FRandRange(-60.f, 60.f)),
			FRotator(0.f, 0.f, FMath::FRandRange(-3.f, 3.f)),
			FVector(1.2f, 2.f, FMath::FRandRange(50.f, 130.f)), CrackMat, /*bBlockingCollision*/ false);
	}

	// Ledge-and-brace boarding across the back of the leaf.
	for (int32 i = 0; i < 2; ++i)
	{
		Build.Box(FVector(-FaceX, LeafY, LeafZ + (i == 0 ? -70.f : 70.f)), FRotator::ZeroRotator, FVector(2.f, 96.f, 14.f), WoodMat, /*bBlockingCollision*/ false);
	}

	// Hinges, seized with rust, on the hinge edge.
	for (int32 i = 0; i < 2; ++i)
	{
		const float HingeZ = (i == 0) ? 32.f : 178.f;
		Build.Box(FVector(FaceX, 8.f, HingeZ), FRotator::ZeroRotator, FVector(1.5f, 22.f, 16.f), RustMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(FaceX + 1.f, 1.f, HingeZ), FRotator::ZeroRotator, FVector(6.f, 6.f, 20.f), RustMat, /*bBlockingCollision*/ false);
	}

	// The lock: a heavy iron plate bolted over the latch, with no keyhole on this side and a bar
	// across the frame. It exists to be read, not solved — the way out of this room is not
	// through the lock.
	const float LockZ = LeafZ - 5.f;
	Build.Box(FVector(FaceX, LeafY + 40.f, LockZ), FRotator::ZeroRotator, FVector(2.f, 26.f, 34.f), IronMat, /*bBlockingCollision*/ false);
	Build.Cyl(FVector(FaceX + 2.f, LeafY + 40.f, LockZ), FRotator(90.f, 0.f, 0.f), FVector(11.f, 11.f, 5.f), IronMat, /*bBlockingCollision*/ false);
	Build.Box(FVector(FaceX + 1.f, LeafY + 20.f, LockZ), FRotator::ZeroRotator, FVector(2.f, 60.f, 9.f), RustMat, /*bBlockingCollision*/ false);
	for (int32 i = 0; i < 4; ++i)
	{
		const float BoltY = LeafY + 30.f + (i % 2) * 20.f;
		const float BoltZ = LockZ - 12.f + (i / 2) * 24.f;
		Build.Sph(FVector(FaceX + 1.5f, BoltY, BoltZ), 4.f, IronMat);
	}
}

void ADoorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float CurrentYaw = HingeRoot->GetRelativeRotation().Yaw;
	if (!FMath::IsNearlyEqual(CurrentYaw, TargetYaw, 0.1f))
	{
		const float NewYaw = FMath::FInterpTo(CurrentYaw, TargetYaw, DeltaTime, 2.5f);
		HingeRoot->SetRelativeRotation(FRotator(0.f, NewYaw, 0.f));
	}
}

void ADoorActor::Interact(AActor* Interactor)
{
	if (bIsLocked)
	{
		const ADetectiveCharacter* Detective = Cast<ADetectiveCharacter>(Interactor);
		if (Detective && Detective->bHasRoomKey)
		{
			bIsLocked = false;
		}
		else
		{
			return; // still locked, no key yet — prompt stays "It's locked."
		}
	}

	bIsOpen = !bIsOpen;
	TargetYaw = bIsOpen ? 100.f : 0.f;
}

FText ADoorActor::GetInteractPrompt() const
{
	if (bIsLocked)
	{
		return FText::FromString(TEXT("Locked. The iron has rusted into the frame."));
	}
	return bIsOpen ? FText::FromString(TEXT("[E] Close door")) : FText::FromString(TEXT("[E] Open door"));
}
