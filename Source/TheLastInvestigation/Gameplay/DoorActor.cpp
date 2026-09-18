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
	const FVector DoorSize(4.5f, 100.f, 204.f);
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

	// A panelled interior door, not a barn plank: painted joinery, four fields, and the paint
	// coming off in sheets. It sits at the right-hand edge of the waking view and is the only
	// pale vertical in that half of the frame, so it carries more of the composition than its
	// size suggests.
	UMaterialInstanceDynamic* PaintMat = Build.Flat(RoomPalette::PaintedTrim, 0.72f);
	UMaterialInstanceDynamic* PanelMat = Build.Flat(RoomPalette::PaintedTrim * 0.82f, 0.78f);
	UMaterialInstanceDynamic* WoodMat = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.62f, 0.56f, 0.50f));
	UMaterialInstanceDynamic* IronMat = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.55f, 0.55f, 0.58f));
	UMaterialInstanceDynamic* RustMat = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.85f, 0.70f, 0.55f));
	UMaterialInstanceDynamic* CrackMat = Build.Flat(FLinearColor(0.006f, 0.006f, 0.006f), 1.f);

	if (PaintMat)
	{
		DoorLeaf->SetMaterial(0, PaintMat);
	}

	const float LeafY = 50.f;   // the leaf's centre, offset from the hinge
	const float LeafZ = 102.f;
	const float FaceX = 2.6f;   // just proud of the leaf, on the room side

	// Stiles and rails, standing proud of the panel fields. Two boxes of frame per face is all it
	// takes: what the eye reads as a panelled door is the shadow line around each field.
	auto Frame = [&](float Y, float Z, float SizeY, float SizeZ)
	{
		for (int32 Face = 0; Face < 2; ++Face)
		{
			const float X = (Face == 0) ? FaceX : -FaceX;
			Build.Box(FVector(X, Y, Z), FRotator::ZeroRotator, FVector(1.6f, SizeY, SizeZ), PaintMat, /*bBlockingCollision*/ false);
		}
	};

	Frame(LeafY - 44.f, LeafZ, 12.f, 196.f);  // hanging stile
	Frame(LeafY + 44.f, LeafZ, 12.f, 196.f);  // lock stile
	Frame(LeafY, LeafZ + 92.f, 100.f, 12.f);  // top rail
	Frame(LeafY, LeafZ + 8.f, 100.f, 16.f);   // lock rail
	Frame(LeafY, LeafZ - 92.f, 100.f, 14.f);  // bottom rail

	// The panel fields themselves, set back a hair and a shade darker, which is what gives each
	// one its own edge in a room lit from one side.
	for (int32 Column = 0; Column < 2; ++Column)
	{
		for (int32 Row = 0; Row < 2; ++Row)
		{
			const float PanelY = LeafY + (Column == 0 ? -22.f : 22.f);
			const float PanelZ = LeafZ + (Row == 0 ? -50.f : 50.f);
			for (int32 Face = 0; Face < 2; ++Face)
			{
				const float X = (Face == 0) ? FaceX - 0.9f : -(FaceX - 0.9f);
				Build.Box(FVector(X, PanelY, PanelZ), FRotator::ZeroRotator, FVector(1.f, 32.f, 70.f), PanelMat, /*bBlockingCollision*/ false);
			}
		}
	}

	// Paint lifting in sheets, with bare grey wood underneath. Patches rather than a texture: the
	// flakes need to cross the joinery lines to look like paint failing rather than panels tinted
	// two colours.
	for (int32 i = 0; i < 11; ++i)
	{
		const float PatchY = LeafY + FMath::FRandRange(-44.f, 44.f);
		const float PatchZ = LeafZ + FMath::FRandRange(-94.f, 94.f);
		Build.Box(FVector(FaceX + 0.4f, PatchY, PatchZ), FRotator(0.f, 0.f, FMath::FRandRange(-6.f, 6.f)),
			FVector(1.f, FMath::FRandRange(10.f, 34.f), FMath::FRandRange(14.f, 56.f)), WoodMat, /*bBlockingCollision*/ false);
	}

	// Deep splits running with the grain, and the kick marks at the bottom where a boot has been
	// put through the lower panel and the wood has never been replaced.
	for (int32 i = 0; i < 4; ++i)
	{
		const float CrackY = LeafY + FMath::FRandRange(-40.f, 40.f);
		Build.Box(FVector(FaceX + 0.9f, CrackY, LeafZ + FMath::FRandRange(-70.f, 60.f)),
			FRotator(0.f, 0.f, FMath::FRandRange(-3.f, 3.f)),
			FVector(1.2f, 2.f, FMath::FRandRange(40.f, 110.f)), CrackMat, /*bBlockingCollision*/ false);
	}

	// Hinges, seized with rust, on the hinge edge.
	for (int32 i = 0; i < 2; ++i)
	{
		const float HingeZ = (i == 0) ? 30.f : 172.f;
		Build.Box(FVector(FaceX, 8.f, HingeZ), FRotator::ZeroRotator, FVector(1.5f, 22.f, 16.f), RustMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(FaceX + 1.f, 1.f, HingeZ), FRotator::ZeroRotator, FVector(6.f, 6.f, 20.f), RustMat, /*bBlockingCollision*/ false);
	}

	// The lock: a hasp screwed across the joint between leaf and frame, with a padlock through the
	// staple. It exists to be read, not solved — the way out of this room is not through the lock,
	// and a padlock says that in one glance where a keyhole would invite picking.
	const float LockZ = LeafZ + 8.f;
	Build.Box(FVector(FaceX, LeafY + 42.f, LockZ), FRotator::ZeroRotator, FVector(2.f, 44.f, 12.f), IronMat, /*bBlockingCollision*/ false);
	Build.Box(FVector(FaceX + 1.f, LeafY + 62.f, LockZ), FRotator::ZeroRotator, FVector(2.4f, 10.f, 16.f), RustMat, /*bBlockingCollision*/ false);

	// The padlock body, hanging a little off vertical from the staple, and its shackle.
	const FVector LockCenter(FaceX + 4.f, LeafY + 62.f, LockZ - 16.f);
	Build.Box(LockCenter, FRotator(0.f, 0.f, 7.f), FVector(5.f, 15.f, 19.f), RustMat, /*bBlockingCollision*/ false);
	Build.Cyl(LockCenter + FVector(0.f, -5.f, 12.f), FRotator(0.f, 0.f, 4.f), FVector(2.6f, 2.6f, 16.f), IronMat, /*bBlockingCollision*/ false);
	Build.Cyl(LockCenter + FVector(0.f, 5.f, 12.f), FRotator(0.f, 0.f, 4.f), FVector(2.6f, 2.6f, 16.f), IronMat, /*bBlockingCollision*/ false);
	Build.Cyl(LockCenter + FVector(0.f, 0.f, 20.f), FRotator(0.f, 0.f, 90.f), FVector(2.6f, 2.6f, 10.f), IronMat, /*bBlockingCollision*/ false);

	for (int32 i = 0; i < 4; ++i)
	{
		const float BoltY = LeafY + 30.f + (i % 2) * 22.f;
		const float BoltZ = LockZ - 4.f + (i / 2) * 8.f;
		Build.Sph(FVector(FaceX + 1.2f, BoltY, BoltZ), 3.4f, IronMat);
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
