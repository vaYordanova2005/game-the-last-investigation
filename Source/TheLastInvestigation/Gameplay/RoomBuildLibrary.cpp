#include "RoomBuildLibrary.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"

namespace RoomPalette
{
	const FLinearColor Plaster(0.150f, 0.138f, 0.122f);
	const FLinearColor Wallpaper(0.118f, 0.104f, 0.074f);
	const FLinearColor WallpaperFaded(0.148f, 0.133f, 0.104f);
	const FLinearColor CeilingStain(0.086f, 0.074f, 0.055f);
	const FLinearColor RottenWood(0.072f, 0.060f, 0.046f);
	const FLinearColor DarkWood(0.098f, 0.070f, 0.044f);
	const FLinearColor Mold(0.046f, 0.062f, 0.044f);
	const FLinearColor Rust(0.110f, 0.058f, 0.030f);
	const FLinearColor Iron(0.060f, 0.058f, 0.058f);
	const FLinearColor Brass(0.180f, 0.135f, 0.058f);
	const FLinearColor Paper(0.300f, 0.276f, 0.230f);
	const FLinearColor Photo(0.235f, 0.218f, 0.196f);
	const FLinearColor GlassShard(0.130f, 0.150f, 0.158f);
	const FLinearColor Cloth(0.090f, 0.078f, 0.068f);
	const FLinearColor DriedBlood(0.062f, 0.026f, 0.020f);
	const FLinearColor DustFilm(0.165f, 0.158f, 0.145f);
	const FLinearColor NightSky(0.012f, 0.016f, 0.026f);
	const FLinearColor Foliage(0.018f, 0.022f, 0.018f);
	const FLinearColor Rain(0.240f, 0.270f, 0.310f);
}

namespace
{
	UStaticMesh* LoadBasicShape(const TCHAR* Path)
	{
		// Cached per shape: LoadObject is a package lookup, and the dressing asks for the cube
		// several hundred times while building.
		return LoadObject<UStaticMesh>(nullptr, Path);
	}
}

UStaticMesh* FRoomShapes::Cube()
{
	static UStaticMesh* Mesh = LoadBasicShape(TEXT("/Engine/BasicShapes/Cube.Cube"));
	return Mesh;
}

UStaticMesh* FRoomShapes::Cylinder()
{
	static UStaticMesh* Mesh = LoadBasicShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	return Mesh;
}

UStaticMesh* FRoomShapes::Cone()
{
	static UStaticMesh* Mesh = LoadBasicShape(TEXT("/Engine/BasicShapes/Cone.Cone"));
	return Mesh;
}

UStaticMesh* FRoomShapes::Sphere()
{
	static UStaticMesh* Mesh = LoadBasicShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	return Mesh;
}

UStaticMesh* FRoomShapes::Plane()
{
	static UStaticMesh* Mesh = LoadBasicShape(TEXT("/Engine/BasicShapes/Plane.Plane"));
	return Mesh;
}

FRoomBuilder::FRoomBuilder(AActor* InOwner, USceneComponent* InParent)
	: Owner(InOwner)
	, ParentComponent(InParent)
{
}

UMaterialInstanceDynamic* FRoomBuilder::Material(const FLinearColor& Color, float Roughness, float Metallic) const
{
	static UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!BaseMaterial || !Owner)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(BaseMaterial, Owner);
	if (Instance)
	{
		Instance->SetVectorParameterValue(TEXT("Color"), Color);
		// BasicShapeMaterial exposes Color for certain; Roughness/Metallic are set optimistically
		// and are harmless no-ops on engine versions where the parameters aren't published.
		Instance->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		Instance->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	}
	return Instance;
}

UStaticMeshComponent* FRoomBuilder::Add(UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision)
{
	if (!Owner || !ParentComponent || !Mesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), TEXT("Part")));
	Component->SetStaticMesh(Mesh);
	Component->SetMobility(EComponentMobility::Movable); // no lighting build needed; Lumen lights everything dynamically
	Component->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepRelativeTransform);
	Component->SetRelativeLocationAndRotation(Location, Rotation);
	Component->SetRelativeScale3D(SizeUU / 100.f); // every basic shape is 100uu across

	if (bBlockingCollision)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
	}
	else
	{
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (Mat)
	{
		Component->SetMaterial(0, Mat);
	}

	Component->RegisterComponent();
	Owner->AddInstanceComponent(Component);
	return Component;
}

UStaticMeshComponent* FRoomBuilder::Box(const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision)
{
	return Add(FRoomShapes::Cube(), Location, Rotation, SizeUU, Mat, bBlockingCollision);
}

UStaticMeshComponent* FRoomBuilder::Cyl(const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision)
{
	return Add(FRoomShapes::Cylinder(), Location, Rotation, SizeUU, Mat, bBlockingCollision);
}

UStaticMeshComponent* FRoomBuilder::Sph(const FVector& Location, float Diameter, UMaterialInterface* Mat, bool bBlockingCollision)
{
	return Add(FRoomShapes::Sphere(), Location, FRotator::ZeroRotator, FVector(Diameter), Mat, bBlockingCollision);
}

UStaticMeshComponent* FRoomBuilder::Mark(const FVector& Location, const FRotator& Rotation, const FVector2D& SizeUU, UMaterialInterface* Mat)
{
	// 0.4uu thick and offset along its own up axis by half that, so the slab's visible face sits
	// just proud of the wall/floor it is painted onto rather than fighting it depth-wise.
	UStaticMeshComponent* Component = Add(FRoomShapes::Cube(), Location, Rotation, FVector(SizeUU.X, SizeUU.Y, 0.4f), Mat, /*bBlockingCollision*/ false);
	if (Component)
	{
		Component->AddLocalOffset(FVector(0.f, 0.f, 0.6f));
	}
	return Component;
}

UInstancedStaticMeshComponent* FRoomBuilder::Instances(UStaticMesh* Mesh, UMaterialInterface* Mat)
{
	if (!Owner || !ParentComponent || !Mesh)
	{
		return nullptr;
	}

	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UInstancedStaticMeshComponent::StaticClass(), TEXT("Instances")));
	Component->SetStaticMesh(Mesh);
	Component->SetMobility(EComponentMobility::Movable);
	Component->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepRelativeTransform);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Mat)
	{
		Component->SetMaterial(0, Mat);
	}
	Component->RegisterComponent();
	Owner->AddInstanceComponent(Component);
	return Component;
}
