#include "KeyPickupActor.h"
#include "DetectiveCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AKeyPickupActor::AKeyPickupActor()
{
	KeyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("KeyMesh"));
	SetRootComponent(KeyMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		KeyMesh->SetStaticMesh(CubeMeshFinder.Object);
	}

	// A thin gold sliver standing in for a key mesh until a real asset is sourced.
	KeyMesh->SetRelativeScale3D(FVector(0.09f, 0.02f, 0.02f));
	KeyMesh->SetMobility(EComponentMobility::Movable);
	KeyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	KeyMesh->SetCollisionResponseToAllChannels(ECR_Block);
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
