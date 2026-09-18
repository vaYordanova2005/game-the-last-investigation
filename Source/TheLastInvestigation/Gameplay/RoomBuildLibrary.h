#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * The surface sets built by Tools/build_art.py from CC0 Poly Haven textures. Each name matches a
 * /Game/Materials/MI_<name> asset, and carries the real-world size the photographed texture covers
 * so geometry of any size can be tiled at a consistent texel density.
 */
struct FRoomSurface
{
	const TCHAR* Set;
	/** Centimetres of world covered by one repeat of the texture. */
	float TexelSizeCm;
};

namespace RoomSurfaces
{
	extern const FRoomSurface Floorboards;
	extern const FRoomSurface Wallpaper;
	extern const FRoomSurface Plaster;
	extern const FRoomSurface Ceiling;
	extern const FRoomSurface RoughWood;   // door, beams, carpentry
	extern const FRoomSurface PlankWall;   // skirting, loose boards, furniture
	extern const FRoomSurface Linen;       // curtains, cloth
	extern const FRoomSurface RustedIron;  // lock, hinges, tools
}

/** Imported prop meshes, /Game/Meshes/<name>. Names match Tools/fetch_assets.py's manifest. */
namespace RoomProps
{
	extern const TCHAR* Cabinet;
	extern const TCHAR* Bookshelf;
	extern const TCHAR* ShelfBooks;
	extern const TCHAR* LooseBooks;
	extern const TCHAR* Chair;
	extern const TCHAR* Table;
	extern const TCHAR* WallClock;
	extern const TCHAR* PictureFrame;
	extern const TCHAR* Lantern;
	extern const TCHAR* Crate;
}

/**
 * Tints used for the few things with no photographed surface of their own — the black under a
 * collapsed floor, glass, standing water, cobweb. Everything else gets a real texture set.
 */
namespace RoomPalette
{
	extern const FLinearColor Mold;
	extern const FLinearColor Paper;
	extern const FLinearColor Photo;
	extern const FLinearColor GlassShard;
	extern const FLinearColor DriedBlood;
	extern const FLinearColor DustFilm;
	extern const FLinearColor Web;
	extern const FLinearColor Water;
	extern const FLinearColor Void;
	extern const FLinearColor NightSky;
	extern const FLinearColor Foliage;
	extern const FLinearColor Rain;
	extern const FLinearColor PaintedTrim;
	extern const FLinearColor Lightning;
	extern const FLinearColor Coat;
	extern const FLinearColor Skin;
}

/** Lazily loaded engine basic shapes. All of them are 100uu across, so scale is always Size/100. */
struct FRoomShapes
{
	static UStaticMesh* Cube();
	static UStaticMesh* Cylinder();
	static UStaticMesh* Cone();
	static UStaticMesh* Sphere();
	static UStaticMesh* Plane();

	/** An imported prop mesh by name, or null if the art pipeline has not been run. */
	static UStaticMesh* Prop(const TCHAR* Name);
};

/**
 * Runtime geometry helper. The project is code-first — no editor GUI — so the room is assembled
 * from meshes and tinted primitives at BeginPlay rather than placed by hand. This wraps the
 * NewObject/AttachTo/RegisterComponent dance, the Size-in-world-units convention, and the tiling
 * problem described on Surface() below.
 *
 * Keep one builder alive for a whole build pass: it caches the material instances it creates, and
 * a fresh builder per function would make hundreds of duplicates.
 */
class FRoomBuilder
{
public:
	FRoomBuilder(AActor* InOwner, USceneComponent* InParent);

	/**
	 * A dynamic instance of a photographed surface set.
	 *
	 * Parts built from this get their tiling fixed up automatically in Add(): a texture shot at
	 * one metre square would otherwise be stretched the length of a four-metre floorboard and
	 * squashed across a twenty-centimetre batten. Add() measures each part, works out how many
	 * repeats it needs, and hands back a cached variant — so the room ends up with a handful of
	 * material instances rather than one per plank.
	 */
	UMaterialInstanceDynamic* Surface(const FRoomSurface& Set, const FLinearColor& Tint = FLinearColor::White, float RoughnessScale = 1.f);

	/** A plain untextured colour, for water, soot, paint and the black under the floorboards. */
	UMaterialInstanceDynamic* Flat(const FLinearColor& Color, float Roughness = 0.92f, float Metallic = 0.f) const;

	/**
	 * Translucent window glass. The panes are the one surface in the room that has to be seen
	 * *through* — the storm is the scene's second light source and its only view — so they get a
	 * real translucent material rather than the opaque stand-in the rest of the room uses.
	 */
	UMaterialInstanceDynamic* Glass(const FLinearColor& Tint, float Opacity = 0.32f, float Roughness = 0.08f) const;

	/** Unlit glow, for things that are light rather than lit: the lightning bolt outside the window. */
	UMaterialInstanceDynamic* Emissive(const FLinearColor& Tint, float Intensity = 1.f) const;

	UStaticMeshComponent* Add(UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision = true);

	/**
	 * Places an imported prop. DesiredHeightCm scales the mesh so it stands that tall in the world;
	 * pass 0 to keep its authored size. Sizing from the mesh's own bounds sidesteps the usual FBX
	 * unit mess — it does not matter whether the model was authored in metres or centimetres, a
	 * chair asked to be 95cm tall is 95cm tall.
	 */
	UStaticMeshComponent* Prop(const TCHAR* Name, const FVector& Location, const FRotator& Rotation, float DesiredHeightCm = 0.f, bool bBlockingCollision = true);

	UStaticMeshComponent* Box(const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision = true);
	UStaticMeshComponent* Cyl(const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision = true);
	UStaticMeshComponent* Sph(const FVector& Location, float Diameter, UMaterialInterface* Mat, bool bBlockingCollision = false);

	/**
	 * A stain/mark: a paper-thin slab laid flat against a surface. Never collides, so it cannot
	 * catch the interaction trace, and is pushed a fraction off the surface to avoid z-fighting.
	 */
	UStaticMeshComponent* Mark(const FVector& Location, const FRotator& Rotation, const FVector2D& SizeUU, UMaterialInterface* Mat);

	UInstancedStaticMeshComponent* Instances(UStaticMesh* Mesh, UMaterialInterface* Mat);

	USceneComponent* Parent() const { return ParentComponent; }

private:
	/** Returns the variant of Mat tiled for a part of this size, creating and caching it if needed. */
	UMaterialInterface* ResolveTiling(UMaterialInterface* Mat, const FVector& SizeUU);

	/**
	 * What a handed-out surface instance was made from. Kept because a dynamic instance cannot be
	 * the parent of another dynamic instance — UE only accepts Materials and MaterialInstance
	 * *Constants* as parents — so every per-size tiling variant has to be built from the original
	 * asset again, with the tint and roughness re-applied on top.
	 */
	struct FSurfaceOrigin
	{
		UMaterialInterface* Asset = nullptr;
		FLinearColor Tint = FLinearColor::White;
		float RoughnessScale = 1.f;
		float TexelSizeCm = 100.f;
	};

	AActor* Owner = nullptr;
	USceneComponent* ParentComponent = nullptr;

	/** Base instance per surface set + tint, and the per-size variants derived from them. */
	TMap<FString, UMaterialInstanceDynamic*> SurfaceCache;
	TMap<FString, UMaterialInstanceDynamic*> TilingCache;
	TMap<UMaterialInterface*, FSurfaceOrigin> SurfaceOrigins;
};
