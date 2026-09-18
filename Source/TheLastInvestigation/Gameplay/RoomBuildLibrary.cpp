#include "RoomBuildLibrary.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/Texture.h"
#include "UObject/UObjectGlobals.h"
#include "Algo/Sort.h"

namespace RoomSurfaces
{
	// TexelSizeCm is how much world a single repeat of the photograph covers. Poly Haven shoots
	// most of its library at one or two metres square; these are the values that make the grain
	// read at life size in a room modelled in centimetres.
	const FRoomSurface Floorboards{ TEXT("old_wooden_floor_02"), 190.f };
	const FRoomSurface Wallpaper{ TEXT("decrepit_wallpaper"), 160.f };
	const FRoomSurface Plaster{ TEXT("clay_plaster"), 210.f };
	const FRoomSurface Ceiling{ TEXT("ceiling_interior"), 200.f };
	const FRoomSurface RoughWood{ TEXT("weathered_brown_planks"), 150.f };
	const FRoomSurface PlankWall{ TEXT("raw_plank_wall"), 150.f };
	const FRoomSurface Linen{ TEXT("rough_linen"), 90.f };
	const FRoomSurface RustedIron{ TEXT("green_metal_rust"), 110.f };
}

namespace RoomProps
{
	const TCHAR* Cabinet = TEXT("vintage_cabinet_01");
	const TCHAR* Bookshelf = TEXT("wooden_bookshelf_worn");
	const TCHAR* ShelfBooks = TEXT("book_encyclopedia_set_01");
	const TCHAR* LooseBooks = TEXT("decorative_book_set_01");
	const TCHAR* Chair = TEXT("WoodenChair_01");
	const TCHAR* Table = TEXT("WoodenTable_01");
	const TCHAR* WallClock = TEXT("wall_clock");
	const TCHAR* PictureFrame = TEXT("hanging_picture_frame_01");
	const TCHAR* Lantern = TEXT("Lantern_01");
	const TCHAR* Crate = TEXT("wooden_crate_01");
}

namespace RoomPalette
{
	// Final display-space values, kept dark: nothing in a house that has been shut for decades is
	// bright, and a lantern lighting a high-albedo surface reads as daylight.
	const FLinearColor Mold(0.046f, 0.062f, 0.044f);
	const FLinearColor Paper(0.180f, 0.166f, 0.140f);
	const FLinearColor Photo(0.235f, 0.218f, 0.196f);
	const FLinearColor GlassShard(0.130f, 0.150f, 0.158f);
	const FLinearColor DriedBlood(0.062f, 0.026f, 0.020f);
	const FLinearColor DustFilm(0.105f, 0.100f, 0.092f);
	const FLinearColor Web(0.320f, 0.310f, 0.290f);
	const FLinearColor Water(0.055f, 0.070f, 0.080f);
	const FLinearColor Void(0.004f, 0.004f, 0.004f);
	const FLinearColor NightSky(0.012f, 0.016f, 0.026f);
	const FLinearColor Foliage(0.018f, 0.022f, 0.018f);
	const FLinearColor Rain(0.130f, 0.150f, 0.180f);
	// Old lead-white paint on the window joinery and the door: the palest thing in the room, and
	// still only a quarter albedo — a century of smoke has been into it.
	const FLinearColor PaintedTrim(0.150f, 0.152f, 0.144f);
	const FLinearColor Lightning(0.78f, 0.86f, 1.000f);
	const FLinearColor Coat(0.034f, 0.032f, 0.038f);
	const FLinearColor Skin(0.240f, 0.152f, 0.112f);
}

namespace
{
	UStaticMesh* LoadBasicShape(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}

	UMaterialInterface* LoadFlatBaseMaterial()
	{
		// Loaded by name, never taken from a mesh's own material slot: /Engine/BasicShapes/Cube
		// ships with a WorldGridMaterial assigned, and tinting that gives a tinted checkerboard.
		static UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		return Base;
	}

	/**
	 * Restates a material instance asset's texture parameters on a dynamic instance made from it.
	 *
	 * In principle a dynamic instance inherits its parent's overrides, and reading them back
	 * confirms it does — GetTextureParameterValue returns the right texture. What it does not do
	 * is reach the renderer: a dynamic instance created from a MaterialInstanceConstant draws with
	 * the *master's* default textures, which is why every wall in this room came out wearing
	 * DefaultDiffuse's checkerboard while props with the same materials assigned straight to the
	 * mesh looked correct. Setting each texture explicitly on the instance fixes it.
	 */
	void CopyTextureParameters(UMaterialInterface* From, UMaterialInstanceDynamic* To)
	{
		if (!From || !To)
		{
			return;
		}

		static const TCHAR* TextureParameters[] = { TEXT("BaseColorMap"), TEXT("NormalMap"), TEXT("ARMMap"), TEXT("OpacityMap") };
		for (const TCHAR* ParameterName : TextureParameters)
		{
			UTexture* Texture = nullptr;
			if (From->GetTextureParameterValue(FMaterialParameterInfo(ParameterName), Texture) && Texture)
			{
				To->SetTextureParameterValue(ParameterName, Texture);
			}
		}
	}

	/** The two largest dimensions of a part — the face a texture actually reads on. */
	void LargestTwoAxes(const FVector& SizeUU, float& OutU, float& OutV)
	{
		float Axes[3] = { FMath::Abs(SizeUU.X), FMath::Abs(SizeUU.Y), FMath::Abs(SizeUU.Z) };
		Algo::Sort(Axes);
		OutU = Axes[2];
		OutV = Axes[1];
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

UStaticMesh* FRoomShapes::Prop(const TCHAR* Name)
{
	if (!Name)
	{
		return nullptr;
	}

	// Cached per name: a missing prop means the art pipeline has not been run on this machine, and
	// the room falls back to its primitive stand-in rather than failing to build.
	static TMap<FString, UStaticMesh*> Cache;
	const FString Key(Name);
	if (UStaticMesh** Found = Cache.Find(Key))
	{
		return *Found;
	}

	const FString Path = FString::Printf(TEXT("/Game/Meshes/%s.%s"), Name, Name);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prop mesh %s not found — run Tools/fetch_assets.py then Tools/build_art.py"), Name);
	}
	Cache.Add(Key, Mesh);
	return Mesh;
}

FRoomBuilder::FRoomBuilder(AActor* InOwner, USceneComponent* InParent)
	: Owner(InOwner)
	, ParentComponent(InParent)
{
}

UMaterialInstanceDynamic* FRoomBuilder::Flat(const FLinearColor& Color, float Roughness, float Metallic) const
{
	UMaterialInterface* BaseMaterial = LoadFlatBaseMaterial();
	if (!BaseMaterial || !Owner)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(BaseMaterial, Owner);
	if (Instance)
	{
		Instance->SetVectorParameterValue(TEXT("Color"), Color);
		Instance->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		Instance->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	}
	return Instance;
}

UMaterialInstanceDynamic* FRoomBuilder::Glass(const FLinearColor& Tint, float Opacity, float Roughness) const
{
	if (!Owner)
	{
		return nullptr;
	}

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomGlass.M_RoomGlass"));
	if (!Base)
	{
		// Without the pipeline there is no translucency available, so the panes fall back to the
		// opaque stand-in. The window still reads as a window; it just cannot be seen through.
		UE_LOG(LogTemp, Warning, TEXT("M_RoomGlass not found — run Tools/build_art.py"));
		return Flat(Tint, Roughness);
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, Owner);
	if (Instance)
	{
		Instance->SetVectorParameterValue(TEXT("Tint"), Tint);
		Instance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Instance->SetScalarParameterValue(TEXT("RoughnessBase"), Roughness);
	}
	return Instance;
}

UMaterialInstanceDynamic* FRoomBuilder::Emissive(const FLinearColor& Tint, float Intensity) const
{
	if (!Owner)
	{
		return nullptr;
	}

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomEmissive.M_RoomEmissive"));
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("M_RoomEmissive not found — run Tools/build_art.py"));
		return Flat(Tint, 1.f);
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, Owner);
	if (Instance)
	{
		Instance->SetVectorParameterValue(TEXT("Tint"), Tint);
		Instance->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	}
	return Instance;
}

UMaterialInstanceDynamic* FRoomBuilder::Surface(const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale)
{
	if (!Owner || !Set.Set)
	{
		return nullptr;
	}

	const FString Key = FString::Printf(TEXT("%s|%s|%.2f"), Set.Set, *Tint.ToString(), RoughnessScale);
	if (UMaterialInstanceDynamic** Found = SurfaceCache.Find(Key))
	{
		return *Found;
	}

	const FString Path = FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), Set.Set, Set.Set);
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *Path);
	if (!Parent)
	{
		// The art pipeline has not been run here. Fall back to a flat tint so the room still
		// builds and is navigable — it just looks like the old greybox.
		UE_LOG(LogTemp, Warning, TEXT("Surface MI_%s not found — run Tools/build_art.py"), Set.Set);
		return Flat(Tint * 0.2f);
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, Owner);
	if (Instance)
	{
		CopyTextureParameters(Parent, Instance);
		Instance->SetVectorParameterValue(TEXT("Tint"), Tint);
		Instance->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
		SurfaceOrigins.Add(Instance, FSurfaceOrigin{ Parent, Tint, RoughnessScale, FMath::Max(Set.TexelSizeCm, 1.f) });
	}

	SurfaceCache.Add(Key, Instance);
	return Instance;
}

UMaterialInterface* FRoomBuilder::ResolveTiling(UMaterialInterface* Mat, const FVector& SizeUU)
{
	const FSurfaceOrigin* Origin = SurfaceOrigins.Find(Mat);
	if (!Origin || !Origin->Asset)
	{
		return Mat; // a flat colour, or a material this builder did not make — nothing to tile
	}

	float SizeU = 0.f;
	float SizeV = 0.f;
	LargestTwoAxes(SizeUU, SizeU, SizeV);

	// Rounded so parts of near-identical size share one instance. Without this the floor alone
	// would create sixty material instances, one per board.
	const float TilingU = FMath::Max(FMath::RoundToFloat(SizeU / Origin->TexelSizeCm * 4.f) / 4.f, 0.05f);
	const float TilingV = FMath::Max(FMath::RoundToFloat(SizeV / Origin->TexelSizeCm * 4.f) / 4.f, 0.05f);

	const FString Key = FString::Printf(TEXT("%p|%.2f|%.2f"), Mat, TilingU, TilingV);
	if (UMaterialInstanceDynamic** Found = TilingCache.Find(Key))
	{
		return *Found;
	}

	// Built from the original asset, not from Mat: a dynamic instance cannot parent another
	// dynamic instance, so the tint and roughness are re-applied here rather than inherited.
	UMaterialInstanceDynamic* Tiled = UMaterialInstanceDynamic::Create(Origin->Asset, Owner);
	if (!Tiled)
	{
		return Mat;
	}

	CopyTextureParameters(Origin->Asset, Tiled);
	Tiled->SetVectorParameterValue(TEXT("Tint"), Origin->Tint);
	Tiled->SetScalarParameterValue(TEXT("RoughnessScale"), Origin->RoughnessScale);
	Tiled->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(TilingU, TilingV, 0.f, 1.f));
	SurfaceOrigins.Add(Tiled, *Origin);
	TilingCache.Add(Key, Tiled);
	return Tiled;
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
		Component->SetMaterial(0, ResolveTiling(Mat, SizeUU));
	}

	Component->RegisterComponent();
	Owner->AddInstanceComponent(Component);
	return Component;
}

UStaticMeshComponent* FRoomBuilder::Prop(const TCHAR* Name, const FVector& Location, const FRotator& Rotation, float DesiredHeightCm, bool bBlockingCollision)
{
	UStaticMesh* Mesh = FRoomShapes::Prop(Name);
	if (!Mesh || !Owner || !ParentComponent)
	{
		return nullptr;
	}

	float Scale = 1.f;
	if (DesiredHeightCm > 0.f)
	{
		const float NaturalHeight = Mesh->GetBoundingBox().GetSize().Z;
		if (NaturalHeight > KINDA_SMALL_NUMBER)
		{
			Scale = DesiredHeightCm / NaturalHeight;
		}
	}

	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), TEXT("Prop")));
	Component->SetStaticMesh(Mesh); // keeps the materials the art pipeline assigned to the asset
	Component->SetMobility(EComponentMobility::Movable);
	Component->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepRelativeTransform);
	Component->SetRelativeLocationAndRotation(Location, Rotation);
	Component->SetRelativeScale3D(FVector(Scale));

	if (bBlockingCollision)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
	}
	else
	{
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
