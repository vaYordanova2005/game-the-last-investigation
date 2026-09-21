#include "RoomBuildLibrary.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "ProceduralMeshComponent.h"
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
	// clay_plaster was here and was the reason the walls looked untextured: it is a smooth modern
	// finish, and flat it is very nearly a single brown colour. These three carry their damage in
	// the photograph, which is the only place damage survives being seen up close.
	//
	// The three of them tile at roughly a metre, not at the two metres the library was shot at.
	// Two metres is the honest figure and it looks wrong: the wall the player stands in front of
	// reads beautifully, and the far wall — the same material, four metres wide, so two repeats
	// across the whole of it — goes smooth and empty, because its finest detail is a metre across
	// and a metre across at three metres away is nothing. Tiling twice as fine makes the far wall
	// carry the same crazing as the near one, at the cost of a repeat the eye has to be looking
	// for to find, in a room where the damage on top of the plaster never repeats at all.
	const FRoomSurface Plaster{ TEXT("cracked_concrete_wall"), 104.f };
	const FRoomSurface Substrate{ TEXT("damaged_plaster"), 130.f };
	const FRoomSurface Damp{ TEXT("plastered_stone_wall"), 118.f };
	const FRoomSurface Ceiling{ TEXT("ceiling_interior"), 200.f };
	const FRoomSurface RoughWood{ TEXT("weathered_brown_planks"), 150.f };
	const FRoomSurface PlankWall{ TEXT("raw_plank_wall"), 150.f };
	const FRoomSurface Linen{ TEXT("rough_linen"), 90.f };
	const FRoomSurface Drapery{ TEXT("rough_linen"), 34.f };
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
	const TCHAR* Bed = TEXT("GothicBed_01");
	const TCHAR* Armchair = TEXT("ArmChair_01");
	const TCHAR* Nightstand = TEXT("ClassicNightstand_01");
	const TCHAR* Press = TEXT("GothicCabinet_01");
}

namespace RoomPalette
{
	// Final display-space values, kept dark: nothing in a house that has been shut for decades is
	// bright, and a lantern lighting a high-albedo surface reads as daylight.
	// Paper used to be here and is not a flat tint any more: it is carried on the linen
	// photograph, which means its colour is a tint on a photograph and belongs with the other
	// surface tints in ARoomDressingActor.
	// A photographic print that has been face down on a wet floor since the sixties. The old value
	// was near-neutral and half again brighter than the plaster, so the one photograph in the room
	// — a clue, a thing the detective is supposed to pick out of the debris — lay on the boards as
	// a pale grey card. Prints do not go grey as they rot, they go brown.
	const FLinearColor Photo(0.118f, 0.086f, 0.062f);
	const FLinearColor GlassShard(0.130f, 0.150f, 0.158f);
	const FLinearColor DriedBlood(0.062f, 0.026f, 0.020f);
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

UMaterialInterface* FRoomBuilder::ResolveTiling(UMaterialInterface* Mat, const FVector& SizeUU, const FVector& Location)
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
	float TilingU = FMath::Max(FMath::RoundToFloat(SizeU / Origin->TexelSizeCm * 4.f) / 4.f, 0.05f);
	float TilingV = FMath::Max(FMath::RoundToFloat(SizeV / Origin->TexelSizeCm * 4.f) / 4.f, 0.05f);

	// Below one repeat, the part is scaled up until it gets one.
	//
	// Life-size texel density is the honest figure and for anything hand-sized it produces a flat
	// colour: a ten-centimetre chunk of plaster off a texture shot at a metre samples a tenth of
	// the photograph, and a tenth of a photograph of a wall is one smooth patch with no feature in
	// it larger than a freckle. That is the whole reason the rubble on the floor and the pieces at
	// the foot of the walls read as untextured rectangles — they were never untextured, they were
	// each wearing one plain crop of a surface that is only interesting at wall scale.
	//
	// Giving a small part a whole repeat makes its grain finer than life. At this size that is the
	// right trade every time: nobody can tell that the crazing on a piece of debris is at half
	// scale, and everybody can tell that it has none.
	const float Largest = FMath::Max(TilingU, TilingV);
	if (Largest < 1.f)
	{
		// Both axes by the same factor, so a long thin part stays long and thin.
		const float Boost = 1.f / Largest;
		TilingU *= Boost;
		TilingV *= Boost;
	}

	// Which crop of the photograph this part gets. Every basic-shape face maps 0..1, so without an
	// offset a part smaller than one repeat of the texture samples the same corner of it as every
	// other part that size — the reason a wall of torn wallpaper came out as rows of identical
	// flat rectangles. Sixteen crops is enough to break the repetition and few enough to keep the
	// material instance count bounded; the part's own position picks one, so the room is stable
	// between runs and a part never changes crop when something near it moves.
	const int32 CropCount = 4;
	const int32 CropSeed = GetTypeHash(FIntVector(
		FMath::FloorToInt(Location.X / 17.f),
		FMath::FloorToInt(Location.Y / 17.f),
		FMath::FloorToInt(Location.Z / 17.f)));
	const float OffsetU = (CropSeed % CropCount) / static_cast<float>(CropCount);
	const float OffsetV = ((CropSeed / CropCount) % CropCount) / static_cast<float>(CropCount);

	const FString Key = FString::Printf(TEXT("%p|%.2f|%.2f|%.2f|%.2f"), Mat, TilingU, TilingV, OffsetU, OffsetV);
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
	Tiled->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(OffsetU, OffsetV, 0.f, 1.f));
	SurfaceOrigins.Add(Tiled, *Origin);
	TilingCache.Add(Key, Tiled);
	return Tiled;
}

UMaterialInstanceDynamic* FRoomBuilder::Cobweb(const FLinearColor& Tint, float Opacity, float Sharpness)
{
	if (!Owner)
	{
		return nullptr;
	}

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomWeb.M_RoomWeb"));
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("M_RoomWeb not found — run Tools/build_art.py"));
		return Flat(Tint * 0.3f, 1.f);
	}

	const FString Key = FString::Printf(TEXT("web|%s|%.2f|%.2f"), *Tint.ToString(), Opacity, Sharpness);
	if (UMaterialInstanceDynamic** Found = DecalCache.Find(Key))
	{
		return *Found;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, Owner);
	if (Instance)
	{
		Instance->SetVectorParameterValue(TEXT("Tint"), Tint);
		Instance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Instance->SetScalarParameterValue(TEXT("Sharpness"), Sharpness);
		DecalCache.Add(Key, Instance);
	}
	return Instance;
}

UDecalComponent* FRoomBuilder::AddDecal(UMaterialInterface* Mat, const FVector& Location, const FRotator& Rotation, const FVector2D& SizeUU)
{
	if (!Owner || !ParentComponent || !Mat)
	{
		return nullptr;
	}

	UDecalComponent* Component = NewObject<UDecalComponent>(Owner, MakeUniqueObjectName(Owner, UDecalComponent::StaticClass(), TEXT("Stain")));
	Component->SetDecalMaterial(Mat);
	Component->SetMobility(EComponentMobility::Movable);
	Component->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepRelativeTransform);
	Component->SetRelativeLocationAndRotation(Location, Rotation);

	// DecalSize is a half-extent box: X is how far the projection reaches along the aim direction,
	// Y and Z are the patch on the surface. The reach is deliberately short — a couple of
	// centimetres past the wall — so a stain on the wall does not also appear on the back of the
	// wardrobe standing in front of it.
	Component->DecalSize = FVector(9.f, SizeUU.X * 0.5f, SizeUU.Y * 0.5f);

	// The engine hides small decals at distance to save fill rate. Half this room's damage is
	// small and the room is only four metres across, so the saving is nothing and the cost is
	// stains that pop into existence as the player walks towards the wall.
	Component->SetFadeScreenSize(0.f);

	Component->RegisterComponent();
	Owner->AddInstanceComponent(Component);
	return Component;
}

UDecalComponent* FRoomBuilder::Stain(const FRoomSurface& Set, const FVector& Location, const FRotator& Rotation,
	const FVector2D& SizeUU, const FLinearColor& Tint, float Opacity, float EdgeNoise, float RoughnessScale)
{
	if (!Owner || !Set.Set)
	{
		return nullptr;
	}

	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomDecal.M_RoomDecal"));
	UMaterialInterface* Source = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), Set.Set, Set.Set));
	if (!Master || !Source)
	{
		// No decal master means the art pipeline has not been run here. Better a wall with no
		// damage on it than a wall with a magenta rectangle on it.
		UE_LOG(LogTemp, Warning, TEXT("M_RoomDecal or MI_%s not found — run Tools/build_art.py"), Set.Set);
		return nullptr;
	}

	// The same texel-density and crop reasoning as ResolveTiling: a stain is a window onto the
	// photograph, and without this every stain in the room would be a window onto the same corner
	// of it at the same magnification.
	const float TexelSize = FMath::Max(Set.TexelSizeCm, 1.f);
	float TilingU = FMath::Max(FMath::RoundToFloat(SizeUU.X / TexelSize * 4.f) / 4.f, 0.05f);
	float TilingV = FMath::Max(FMath::RoundToFloat(SizeUU.Y / TexelSize * 4.f) / 4.f, 0.05f);

	// And the same floor under it as ResolveTiling puts under a part: a hand-sized stain at life
	// size is one smooth crop of the photograph, which projects onto the wall as a soft blob of
	// tint with nothing in it. It is the torn edge and the grain that make it damage.
	const float Largest = FMath::Max(TilingU, TilingV);
	if (Largest < 1.f)
	{
		const float Boost = 1.f / Largest;
		TilingU *= Boost;
		TilingV *= Boost;
	}

	const int32 CropCount = 4;
	const int32 CropSeed = GetTypeHash(FIntVector(
		FMath::FloorToInt(Location.X / 13.f),
		FMath::FloorToInt(Location.Y / 13.f),
		FMath::FloorToInt(Location.Z / 13.f)));
	const float OffsetU = (CropSeed % CropCount) / static_cast<float>(CropCount);
	const float OffsetV = ((CropSeed / CropCount) % CropCount) / static_cast<float>(CropCount);

	const FString Key = FString::Printf(TEXT("%s|%s|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f"),
		Set.Set, *Tint.ToString(), Opacity, EdgeNoise, RoughnessScale, TilingU, TilingV, OffsetU, OffsetV);

	UMaterialInstanceDynamic* Instance = nullptr;
	if (UMaterialInstanceDynamic** Found = DecalCache.Find(Key))
	{
		Instance = *Found;
	}
	else
	{
		Instance = UMaterialInstanceDynamic::Create(Master, Owner);
		if (!Instance)
		{
			return nullptr;
		}

		// The maps come off the surface instance the room already uses, so a damp patch is the
		// same photographed wall as the wall it sits on, only darker and torn to shape.
		static const TCHAR* TextureParameters[] = { TEXT("BaseColorMap"), TEXT("NormalMap"), TEXT("ARMMap") };
		for (const TCHAR* ParameterName : TextureParameters)
		{
			UTexture* Texture = nullptr;
			if (Source->GetTextureParameterValue(FMaterialParameterInfo(ParameterName), Texture) && Texture)
			{
				Instance->SetTextureParameterValue(ParameterName, Texture);
			}
		}

		Instance->SetVectorParameterValue(TEXT("Tint"), Tint);
		Instance->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(TilingU, TilingV, 0.f, 1.f));
		Instance->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(OffsetU, OffsetV, 0.f, 1.f));
		Instance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Instance->SetScalarParameterValue(TEXT("EdgeNoise"), EdgeNoise);
		Instance->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
		DecalCache.Add(Key, Instance);
	}

	return AddDecal(Instance, Location, Rotation, SizeUU);
}

UDecalComponent* FRoomBuilder::Crack(const FVector& Location, const FRotator& Rotation, const FVector2D& SizeUU,
	float Opacity, float Sharpness)
{
	if (!Owner)
	{
		return nullptr;
	}

	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomCrack.M_RoomCrack"));
	if (!Master)
	{
		UE_LOG(LogTemp, Warning, TEXT("M_RoomCrack not found — run Tools/build_art.py"));
		return nullptr;
	}

	const FString Key = FString::Printf(TEXT("crack|%.2f|%.2f"), Opacity, Sharpness);
	UMaterialInstanceDynamic* Instance = nullptr;
	if (UMaterialInstanceDynamic** Found = DecalCache.Find(Key))
	{
		Instance = *Found;
	}
	else
	{
		Instance = UMaterialInstanceDynamic::Create(Master, Owner);
		if (!Instance)
		{
			return nullptr;
		}
		Instance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Instance->SetScalarParameterValue(TEXT("Sharpness"), Sharpness);
		DecalCache.Add(Key, Instance);
	}

	return AddDecal(Instance, Location, Rotation, SizeUU);
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
		Component->SetMaterial(0, ResolveTiling(Mat, SizeUU, Location));
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

UStaticMeshComponent* FRoomBuilder::PropSeated(const TCHAR* Name, const FVector& Seat, const FRotator& Rotation, float DesiredHeightCm, bool bBlockingCollision)
{
	UStaticMeshComponent* Component = Prop(Name, Seat, Rotation, DesiredHeightCm, bBlockingCollision);
	if (!Component || !Component->GetStaticMesh())
	{
		return Component;
	}

	// Where the prop's box actually ended up, relative to the point it was asked to stand on. The
	// box is transformed rather than measured axis by axis, so this stays correct for a prop that
	// has been rolled onto its side — which is the case that was worst.
	const FBox Local = Component->GetStaticMesh()->GetBoundingBox();
	const FTransform Placement(Rotation, FVector::ZeroVector, Component->GetRelativeScale3D());
	const FBox Placed = Local.TransformBy(Placement);

	const FVector Correction(
		(Placed.Min.X + Placed.Max.X) * 0.5f,
		(Placed.Min.Y + Placed.Max.Y) * 0.5f,
		Placed.Min.Z);

	Component->SetRelativeLocation(Seat - Correction);
	return Component;
}

UProceduralMeshComponent* FRoomBuilder::Cloth(const FVector& Centre, const FRotator& Facing, const FVector2D& SizeUU,
	float Rumple, float EdgeFall, int32 Seed, UMaterialInterface* Mat, float TexelSizeCm)
{
	if (!Owner || !ParentComponent)
	{
		return nullptr;
	}

	// About two centimetres a step, which is fine enough that the ragged outline does not read as
	// a staircase, and bounded so a dust sheet over a whole room does not cost a hundred thousand
	// triangles.
	const int32 Cols = FMath::Clamp(FMath::RoundToInt(SizeUU.X / 2.2f), 12, 96);
	const int32 Rows = FMath::Clamp(FMath::RoundToInt(SizeUU.Y / 2.2f), 12, 96);
	const float Thickness = 0.7f;
	const float EdgeWidth = FMath::Max(SizeUU.GetMin() * 0.16f, 10.f);

	FRandomStream Weave(Seed);
	const float Grain = Weave.FRandRange(0.f, 40.f);

	// Where the cloth has rotted away: the outline is pulled in by a wandering amount and two
	// holes are eaten out of the middle.
	struct FHole { float U; float V; float RU; float RV; };
	FHole Holes[2];
	for (FHole& Hole : Holes)
	{
		Hole.U = Weave.FRandRange(0.2f, 0.8f);
		Hole.V = Weave.FRandRange(0.2f, 0.8f);
		// Small. At a tenth of the piece a hole plus the frayed hem meet each other and take a
		// whole corner off, and what is left reads as a torn flag rather than as worn cloth.
		Hole.RU = Weave.FRandRange(0.028f, 0.06f);
		Hole.RV = Weave.FRandRange(0.028f, 0.06f);
	}

	auto Solid = [&](float U, float V) -> bool
	{
		// The hem, eaten in by up to a twelfth of the piece and never by the same amount twice.
		const float Frayed = 0.038f * (0.5f + 0.5f * FMath::PerlinNoise1D((U + V * 1.7f) * 5.3f + Grain));
		if (U < Frayed || U > 1.f - Frayed || V < Frayed || V > 1.f - Frayed)
		{
			return false;
		}
		for (const FHole& Hole : Holes)
		{
			const float DU = (U - Hole.U) / Hole.RU;
			const float DV = (V - Hole.V) / Hole.RV;
			const float Edge = 1.f + 0.5f * FMath::PerlinNoise2D(FVector2D(U * 13.f + Grain, V * 13.f));
			if (DU * DU + DV * DV < Edge * Edge)
			{
				return false;
			}
		}
		return true;
	};

	auto Surface = [&](float U, float V) -> FVector
	{
		const float X = (U - 0.5f) * SizeUU.X;
		const float Y = (V - 0.5f) * SizeUU.Y;

		// Two octaves of sag. One alone gives an even swell that reads as a moulded lid.
		float Height = Rumple * 0.62f * FMath::PerlinNoise2D(FVector2D(U * 3.1f + Grain, V * 4.7f));
		Height += Rumple * 0.38f * FMath::PerlinNoise2D(FVector2D(U * 8.3f + Grain, V * 11.9f));

		// And the fall over the edges of whatever it is lying on. Cloth does not stop at the edge
		// of a mattress, it goes over it, and that fall is most of what says the thing underneath
		// has a shape at all.
		const float ToEdge = FMath::Min(
			FMath::Min(U, 1.f - U) * SizeUU.X,
			FMath::Min(V, 1.f - V) * SizeUU.Y);
		const float Over = FMath::Clamp(1.f - ToEdge / EdgeWidth, 0.f, 1.f);
		Height -= EdgeFall * FMath::Pow(Over, 1.7f);

		return FVector(X, Y, Height);
	};

	const int32 Grid = Cols * Rows;
	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Tris;
	Verts.SetNum(Grid * 2);
	Normals.Init(FVector::ZeroVector, Grid * 2);
	UVs.SetNum(Grid * 2);
	Tangents.SetNum(Grid * 2);

	for (int32 Col = 0; Col < Cols; ++Col)
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float U = Col / static_cast<float>(Cols - 1);
			const float V = Row / static_cast<float>(Rows - 1);
			const FVector Point = Surface(U, V);
			const int32 Index = Col * Rows + Row;
			Verts[Index] = Point;
			Verts[Index + Grid] = Point - FVector(0.f, 0.f, Thickness);
			const FVector2D UV(Point.X / TexelSizeCm, Point.Y / TexelSizeCm);
			UVs[Index] = UV;
			UVs[Index + Grid] = UV;
		}
	}

	// Both windings per face, and the shading normal forced by which sheet the triangle is in.
	// The curtains learned this the hard way: winding decides what the rasteriser keeps and the
	// normal array decides how it is lit, and a single-sided material makes the first of them a
	// silent killer.
	auto Face = [&](int32 A, int32 B, int32 C, bool bUpward)
	{
		Tris.Add(A);
		Tris.Add(B);
		Tris.Add(C);
		Tris.Add(A);
		Tris.Add(C);
		Tris.Add(B);

		FVector N = FVector::CrossProduct(Verts[B] - Verts[A], Verts[C] - Verts[A]);
		if ((N.Z > 0.f) != bUpward)
		{
			N = -N;
		}
		Normals[A] += N;
		Normals[B] += N;
		Normals[C] += N;
	};

	for (int32 Col = 0; Col + 1 < Cols; ++Col)
	{
		for (int32 Row = 0; Row + 1 < Rows; ++Row)
		{
			const float U0 = Col / static_cast<float>(Cols - 1);
			const float U1 = (Col + 1) / static_cast<float>(Cols - 1);
			const float V0 = Row / static_cast<float>(Rows - 1);
			const float V1 = (Row + 1) / static_cast<float>(Rows - 1);
			if (!Solid(U0, V0) || !Solid(U1, V0) || !Solid(U0, V1) || !Solid(U1, V1))
			{
				continue;
			}

			const int32 A = Col * Rows + Row;
			const int32 B = (Col + 1) * Rows + Row;
			const int32 C = (Col + 1) * Rows + Row + 1;
			const int32 D = Col * Rows + Row + 1;

			Face(A, B, C, /*bUpward*/ true);
			Face(A, C, D, /*bUpward*/ true);
			Face(A + Grid, B + Grid, C + Grid, /*bUpward*/ false);
			Face(A + Grid, C + Grid, D + Grid, /*bUpward*/ false);
		}
	}

	for (int32 Col = 0; Col < Cols; ++Col)
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const int32 Index = Col * Rows + Row;
			const int32 Before = FMath::Max(Col - 1, 0) * Rows + Row;
			const int32 After = FMath::Min(Col + 1, Cols - 1) * Rows + Row;
			const FVector Along = (Verts[After] - Verts[Before]).GetSafeNormal();
			Tangents[Index] = FProcMeshTangent(Along, false);
			Tangents[Index + Grid] = FProcMeshTangent(Along, false);

			Normals[Index] = Normals[Index].GetSafeNormal();
			Normals[Index + Grid] = Normals[Index + Grid].GetSafeNormal();
		}
	}

	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("Cloth")));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepRelativeTransform);
	Mesh->SetRelativeLocationAndRotation(Centre, Facing);
	Mesh->bUseAsyncCooking = false;
	Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, TArray<FLinearColor>(), Tangents, /*bCreateCollision*/ false);
	if (Mat)
	{
		// The UVs are already in repeats, so the instance must not scale them again.
		if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Mat))
		{
			Instance->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Instance->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
		Mesh->SetMaterial(0, Mat);
	}
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->RegisterComponent();
	Owner->AddInstanceComponent(Mesh);
	return Mesh;
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
