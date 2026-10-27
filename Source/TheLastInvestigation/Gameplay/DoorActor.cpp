#include "DoorActor.h"
#include "DetectiveCharacter.h"
#include "RoomBuildLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ADoorActor::ADoorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// HingeRoot sits at the hinge edge; the leaf is offset sideways from it, so rotating the hinge
	// swings the leaf like a real door instead of spinning it around its own centre.
	HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRoot"));
	SetRootComponent(HingeRoot);

	// The part that turns, and the reason it is not HingeRoot itself.
	//
	// HingeRoot is the root component, so HingeRoot->GetRelativeRotation() is the actor's rotation
	// in the world, and the actor is spawned yawed ninety degrees to stand the leaf across the
	// doorway. Tick read that ninety, compared it against a closed target of zero, and dutifully
	// turned it to zero — so the door swung itself wide open on the first frame of every session
	// and stayed there. It has been standing open behind a prompt that says it is locked.
	//
	// Swing starts at zero and is the only thing the animation touches, so closed is zero and open
	// is the swing angle, whatever the actor happens to be yawed to in the world.
	Swing = CreateDefaultSubobject<USceneComponent>(TEXT("Swing"));
	Swing->SetupAttachment(HingeRoot);
	Swing->SetMobility(EComponentMobility::Movable);

	DoorLeaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorLeaf"));
	DoorLeaf->SetupAttachment(Swing);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		DoorLeaf->SetStaticMesh(CubeMeshFinder.Object);
	}

	// Cube is 100uu per side; scale to a door-leaf slab and offset so the hinge edge lines up with
	// the root.
	//
	// Sized to the opening it is shut into, not six centimetres short of it. The opening is 106 by
	// 208 and the leaf was 100 by 204, which left a finger of doorway uncovered down the lock side
	// and another along the top — and what is behind that doorway is the unlit rest of the house,
	// so those fingers were solid black. A door with a black strip down its edge does not read as
	// a door that is shut; it reads as a door standing ajar.
	const FVector DoorSize(4.5f, 106.f, 210.f);
	DoorLeaf->SetRelativeScale3D(DoorSize / 100.f);
	DoorLeaf->SetRelativeLocation(FVector(0.f, DoorSize.Y * 0.5f, DoorSize.Z * 0.5f));
	DoorLeaf->SetMobility(EComponentMobility::Movable);
	DoorLeaf->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorLeaf->SetCollisionResponseToAllChannels(ECR_Block);
}

void ADoorActor::BeginPlay()
{
	Super::BeginPlay();

	// The door's own stream, for the same reason the clock has one: the object the player spends
	// the longest looking at has to be the same door every session (09-22).
	Random.Initialize(19551104 + 7);

	BuildDoorDetail();

	// -OpenDoors: every door in the house starts open, for walking round it without playing it.
	// A development switch; without it the bedroom is locked until the key is found.
	if (FParse::Param(FCommandLine::Get(), TEXT("OpenDoors")))
	{
		bIsLocked = false;
		bIsOpen = true;
		TargetYaw = OpenYaw;
	}
}

void ADoorActor::BuildDoorDetail()
{
	// Detail is attached to the swinging pivot in the leaf's own space, so it swings with the door.
	// Built here rather than in the constructor because FRoomBuilder creates components at runtime.
	FRoomBuilder Build(this, Swing);

	// The master bedroom's door, put in when the room was done over: a flush leaf in black satin
	// paint, two fine shadow grooves across it, a brushed steel lever on a round rose with the
	// keyhole under it, black hinges. The old panelled door, rotten and padlocked, went with the
	// room's old life (the cellar keeps the rest of it).
	//
	// The paint is the room's own (RoomSurfaces::Paint, linear ~0.58) taken down to about 0.022: a
	// black that still shows its surface under the lantern. Flat-colour black at this size is a
	// hole in the wall, which is how the doorway read before the leaf was widened (09-20).
	UMaterialInstanceDynamic* LeafMat = Build.Surface(RoomSurfaces::Paint, FLinearColor(0.040f, 0.039f, 0.041f), 0.55f);
	UMaterialInstanceDynamic* GrooveMat = Build.Flat(FLinearColor(0.002f, 0.002f, 0.002f), 1.f);
	UMaterialInstanceDynamic* SteelMat = Build.Flat(FLinearColor(0.30f, 0.30f, 0.30f), 0.38f, 1.f);
	UMaterialInstanceDynamic* HingeMat = Build.Flat(FLinearColor(0.012f, 0.012f, 0.013f), 0.45f, 0.6f);
	UMaterialInstanceDynamic* SlotMat = Build.Flat(FLinearColor(0.f, 0.f, 0.f), 1.f);

	if (LeafMat)
	{
		DoorLeaf->SetMaterial(0, LeafMat);
	}

	const float LeafY = 53.f;   // the leaf's centre, offset from the hinge
	const float LeafHalf = 2.25f;

	// The leaf is 4.5 thick, so its faces are at local X = +-2.25; the actor is yawed ninety into the
	// doorway, so local -X is the room and +X the corridor (the 09-20 note). Everything goes on both.
	for (const float Side : { -1.f, 1.f })
	{
		auto Proud = [&](float By) { return Side * (LeafHalf + By); };

		// The grooves: a shadow line a third of the way up and two thirds, edge to edge.
		for (const float Z : { 70.f, 140.f })
		{
			Build.Box(FVector(Proud(0.02f), LeafY, Z), FRotator::ZeroRotator, FVector(0.1f, 105.6f, 0.6f), GrooveMat, /*bBlockingCollision*/ false);
		}

		// The lever: a rose, a neck standing out of it, and the bar turned back towards the hinge.
		const float HandleY = LeafY + 44.f;
		const float HandleZ = 104.f;
		Build.Cyl(FVector(Proud(0.5f), HandleY, HandleZ), FRotator(90.f, 0.f, 0.f), FVector(5.6f, 5.6f, 1.f), SteelMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(Proud(3.2f), HandleY, HandleZ), FRotator(90.f, 0.f, 0.f), FVector(1.8f, 1.8f, 5.f), SteelMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(Proud(5.4f), HandleY - 6.f, HandleZ), FRotator(0.f, 0.f, 90.f), FVector(1.7f, 1.7f, 13.f), SteelMat, /*bBlockingCollision*/ false);
		Build.Sph(FVector(Proud(5.4f), HandleY - 12.4f, HandleZ), 1.7f, SteelMat);

		// The keyhole under it, in its own small rose: what the key on the sill is for.
		const float KeyZ = 90.f;
		Build.Cyl(FVector(Proud(0.4f), HandleY, KeyZ), FRotator(90.f, 0.f, 0.f), FVector(3.6f, 3.6f, 0.8f), SteelMat, /*bBlockingCollision*/ false);
		Build.Box(FVector(Proud(0.82f), HandleY, KeyZ - 0.4f), FRotator::ZeroRotator, FVector(0.1f, 0.5f, 1.6f), SlotMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(Proud(0.82f), HandleY, KeyZ + 0.5f), FRotator(90.f, 0.f, 0.f), FVector(0.9f, 0.9f, 0.1f), SlotMat, /*bBlockingCollision*/ false);
	}

	// Dust settled on the leaf where it has stood shut, heavier low down. One decal through the leaf
	// from its middle reaches both faces (nine either way, FRoomBuilder::Stain); it is kept well off
	// the floor, because it swings with the leaf, and a floor decal that moves with a door is the
	// one thing worse than a floor decal it sweeps over (10-05). Numbers drawn into locals (09-29).
	const float DustZ = Random.FRandRange(64.f, 76.f);
	const float DustOpacity = Random.FRandRange(0.16f, 0.22f);
	Build.Stain(RoomSurfaces::Damp, FVector(0.f, LeafY, DustZ), FRotator(0.f, 0.f, 90.f), FVector2D(84.f, 56.f),
		FLinearColor(0.42f, 0.40f, 0.36f), DustOpacity, 1.4f);

	// The paint is not new any more either: scuffed at the foot where it was pushed open with a
	// foot or a bag for years, and rubbed back round the handle by a hand. Kept above the boards for
	// the reason the dust is. Numbers drawn into locals first.
	for (int32 i = 0; i < 4; ++i)
	{
		const float ScuffY = Random.FRandRange(20.f, 95.f);
		const float ScuffZ = Random.FRandRange(18.f, 34.f);
		const float ScuffRoll = Random.FRandRange(0.f, 360.f);
		Build.Stain(RoomSurfaces::Substrate, FVector(0.f, ScuffY, ScuffZ), FRotator(0.f, 0.f, ScuffRoll), FVector2D(14.f, 6.f),
			FLinearColor(0.30f, 0.28f, 0.25f), 0.5f, 1.8f);
	}
	Build.Stain(RoomSurfaces::Damp, FVector(0.f, LeafY + 44.f, 100.f), FRotator(0.f, 0.f, 90.f), FVector2D(26.f, 34.f),
		FLinearColor(0.20f, 0.18f, 0.15f), 0.3f, 1.6f);

	// Three black hinges on the hinge edge, their knuckles on the room side, where the leaf swings.
	for (const float Z : { 22.f, 105.f, 188.f })
	{
		Build.Box(FVector(-(LeafHalf + 0.15f), 1.6f, Z), FRotator::ZeroRotator, FVector(0.3f, 3.2f, 10.f), HingeMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(-(LeafHalf + 0.6f), 0.f, Z), FRotator::ZeroRotator, FVector(1.4f, 1.4f, 10.f), HingeMat, /*bBlockingCollision*/ false);
	}
}

void ADoorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float CurrentYaw = Swing->GetRelativeRotation().Yaw;
	if (!FMath::IsNearlyEqual(CurrentYaw, TargetYaw, 0.1f))
	{
		const float NewYaw = FMath::FInterpTo(CurrentYaw, TargetYaw, DeltaTime, 2.5f);
		Swing->SetRelativeRotation(FRotator(0.f, NewYaw, 0.f));
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
	TargetYaw = bIsOpen ? OpenYaw : 0.f;
}

FText ADoorActor::GetInteractPrompt(const AActor* Interactor) const
{
	if (bIsLocked)
	{
		// The same test Interact makes, so the prompt appears the moment the key is in his pocket.
		// Without the key the door says nothing: no words on the doors while the house is being
		// designed.
		const ADetectiveCharacter* Detective = Cast<const ADetectiveCharacter>(Interactor);
		if (!Detective || !Detective->bHasRoomKey)
		{
			return FText::GetEmpty();
		}
		return FText::FromString(TEXT("[E] Unlock the door"));
	}
	return bIsOpen ? FText::FromString(TEXT("[E] Close door")) : FText::FromString(TEXT("[E] Open door"));
}
