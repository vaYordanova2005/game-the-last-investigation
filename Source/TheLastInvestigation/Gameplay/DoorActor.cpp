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
	// The paint is the room's own (RoomSurfaces::Paint, linear ~0.58) taken down to about 0.034: a
	// black that still shows its surface under the lantern. Flat-colour black at this size is a
	// hole in the wall, which is how the doorway read before the leaf was widened (09-20). It is
	// greyed a little and taken matt: the satin under years of dust, which an even film over the
	// whole leaf is, where any decal laid over it has an edge somewhere.
	UMaterialInstanceDynamic* LeafMat = Build.Surface(RoomSurfaces::Paint, FLinearColor(0.076f, 0.071f, 0.064f), 1.25f);
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

	// The door is years old and has stood shut in a dusty house: the dust is in the paint's tint
	// (above), the whole leaf greyed by it evenly; where hands pushed it and turned the handle they
	// took the dust with them, so those places are darker; the foot carries black rubber marks
	// where it was kicked open, and a band of dirt along the bottom. Every mark is DARKER than the
	// dust: a pale patch on the dark leaf, at any opacity that shows, came out as a cloud with an
	// outline (the dust bloom), and pale scuffs as a row of pills. Every patch is the room's own
	// smooth paint photograph through a soft, barely torn edge.
	// REMOVED (the user): scuffs and a dust bloom cut from the substrate and damp photographs with a
	// torn edge. Pale on black paint, their blotches came out as scraps of paper and a camouflage
	// patch stuck to the door, which is not what wear on paint looks like.
	//
	// Each decal runs nine either way through the leaf from its middle, so it reaches both faces;
	// rolled ninety, its first size goes up the leaf and its second across it. All of them stay
	// inside the leaf (Y 0 at the hinge, 106 at the lock, Z 0..210) with a margin: past an edge it
	// would land on the reveal and the architrave whenever the door is shut, and below the foot on
	// the boards the leaf sweeps (10-05).
	const FLinearColor HandTint(0.02f, 0.02f, 0.02f);
	const FLinearColor RubberTint(0.012f, 0.012f, 0.012f);
	const FLinearColor DirtTint(0.05f, 0.04f, 0.028f);
	struct FWear { float Y; float Z; float Tall; float Wide; float Turn; FLinearColor Tint; float Opacity; };
	const FWear Wear[] = {
		{ LeafY + 32.f, 100.f, 36.f, 28.f, 0.f, HandTint, 0.45f },   // wiped dark round the handle
		{ LeafY + 36.f, 130.f, 16.f, 12.f, 12.f, HandTint, 0.35f },  // where a palm pushed it open
		{ LeafY + 30.f, 143.f, 14.f, 11.f, -8.f, HandTint, 0.3f },
		{ LeafY + 38.f, 116.f, 12.f, 10.f, 20.f, HandTint, 0.28f },
		{ 40.f, 18.f, 1.6f, 16.f, 4.f, RubberTint, 0.45f },          // kicked open: rubber marks
		{ 58.f, 24.f, 2.f, 20.f, -6.f, RubberTint, 0.4f },
		{ 72.f, 15.f, 1.4f, 12.f, 8.f, RubberTint, 0.45f },
		{ 50.f, 31.f, 1.6f, 14.f, -3.f, RubberTint, 0.35f },
		{ 84.f, 21.f, 2.f, 18.f, 5.f, RubberTint, 0.4f },
		{ LeafY, 12.f, 12.f, 92.f, 0.f, DirtTint, 0.35f },           // dirt along the bottom
	};
	for (const FWear& Patch : Wear)
	{
		Build.Stain(RoomSurfaces::Paint, FVector(0.f, Patch.Y, Patch.Z), FRotator(0.f, 0.f, 90.f + Patch.Turn), FVector2D(Patch.Tall, Patch.Wide),
			Patch.Tint, Patch.Opacity, 0.3f);
	}

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
