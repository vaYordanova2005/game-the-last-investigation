#include "KeyPickupActor.h"
#include "DetectiveCharacter.h"
#include "RoomBuildLibrary.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AKeyPickupActor::AKeyPickupActor()
{
	// The trace has to find a key seven centimetres long and under one thick, from a man standing
	// at the sill, with no crosshair on the screen. Testing the mesh itself, it found it only when
	// the middle of the screen was exactly on it, which in play was never: the detective stood at
	// the window, looked straight at the key and could not pick it up. A box a hand across round it
	// is what "looking at the key" means.
	//
	// The root is the key's underside, where it lies on the sill; the box stands five over it.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	HitVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("HitVolume"));
	HitVolume->SetupAttachment(Root);
	HitVolume->SetRelativeLocation(FVector(0.f, 0.f, 5.f));
	HitVolume->SetBoxExtent(FVector(9.f, 6.f, 5.f));
	HitVolume->SetMobility(EComponentMobility::Movable);
	HitVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	HitVolume->SetHiddenInGame(true);

	KeyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("KeyMesh"));
	KeyMesh->SetupAttachment(Root);
	KeyMesh->SetMobility(EComponentMobility::Movable);
	KeyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> KeyFinder(TEXT("/Game/Meshes/bedroom_key.bedroom_key"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (KeyFinder.Succeeded())
	{
		KeyMesh->SetStaticMesh(KeyFinder.Object);
	}
	else if (CubeFinder.Succeeded())
	{
		KeyMesh->SetStaticMesh(CubeFinder.Object);
		KeyMesh->SetRelativeLocation(FVector(0.f, 0.f, 1.f));
		KeyMesh->SetRelativeScale3D(FVector(0.076f, 0.03f, 0.02f));
	}
}

void AKeyPickupActor::BeginPlay()
{
	Super::BeginPlay();

	// Old brass, dulled: the bedroom's brass (AMasterBedroomActor), a shade brighter so a key on a
	// pale sill in the storm light still reads as metal rather than as a stick of chalk, which is
	// what the untextured placeholder box was.
	FRoomBuilder Build(this, GetRootComponent());
	if (UMaterialInstanceDynamic* Brass = Build.Flat(FLinearColor(0.30f, 0.21f, 0.08f), 0.35f, 1.f))
	{
		for (int32 Slot = 0; Slot < KeyMesh->GetNumMaterials(); ++Slot)
		{
			KeyMesh->SetMaterial(Slot, Brass);
		}
	}
}

void AKeyPickupActor::Interact(AActor* Interactor)
{
	if (ADetectiveCharacter* Detective = Cast<ADetectiveCharacter>(Interactor))
	{
		Detective->bHasRoomKey = true;
	}
	Destroy();
}

FText AKeyPickupActor::GetInteractPrompt(const AActor* /*Interactor*/) const
{
	return FText::FromString(TEXT("[E] Pick up key"));
}
