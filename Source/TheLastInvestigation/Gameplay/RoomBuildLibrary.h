#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDecalComponent;

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
	/** The wall itself: plaster that has flaked, cracked and been rained through for decades. */
	extern const FRoomSurface Plaster;
	/** What is behind the plaster — brick and coarse render — seen wherever it has come off. */
	extern const FRoomSurface Substrate;
	/** Damp: the dark bloom that spreads from the corners and down from the ceiling line. */
	extern const FRoomSurface Damp;
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
	extern const FLinearColor Photo;
	extern const FLinearColor GlassShard;
	extern const FLinearColor DriedBlood;
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

	/**
	 * A patch of damage projected onto whatever is behind it — damp, soot, blown plaster, the
	 * shadow a picture left on a wall.
	 *
	 * Damage used to be drawn as thin slabs of flat colour laid against the wall, and it read as
	 * exactly that: rectangles. Choosing a better colour cannot fix a rectangle, because what the
	 * eye picks up is the straight edge. A decal's alpha can be any shape, and M_RoomDecal's is a
	 * radial falloff torn apart by world-space noise, so a stain has no straight edge anywhere on
	 * it and no two of them are the same shape.
	 *
	 * SizeUU is the patch's extent on the surface; the decal is aimed along its own +X, so the
	 * rotation is the direction it is projected *in*, not the direction it faces.
	 *
	 * RoughnessScale multiplies the photograph's own roughness, the same as it does on a surface:
	 * at 1 the patch is as matt as the wall it is on, and low enough it is standing water.
	 */
	UDecalComponent* Stain(const FRoomSurface& Set, const FVector& Location, const FRotator& Rotation,
		const FVector2D& SizeUU, const FLinearColor& Tint, float Opacity = 0.85f, float EdgeNoise = 0.9f,
		float RoughnessScale = 1.f);

	/**
	 * A network of fissures, projected the same way. There is no texture behind this one — the
	 * crack is drawn from the contour of a noise field (see M_RoomCrack) — so it never repeats and
	 * never tiles. Sharpness is how fine the split is: high is a hairline, low is a gap.
	 */
	UDecalComponent* Crack(const FVector& Location, const FRotator& Rotation, const FVector2D& SizeUU,
		float Opacity = 1.f, float Sharpness = 26.f);

	/**
	 * Cobweb: a net of filaments drawn the same way the cracks are, on a translucent sheet rather
	 * than projected. A web spanning a ceiling corner is nearly a metre across, and an opaque slab
	 * that size is a sheet of card hanging in the room — which is exactly how it looked.
	 */
	UMaterialInstanceDynamic* Cobweb(const FLinearColor& Tint, float Opacity = 0.55f, float Sharpness = 11.f);

	UStaticMeshComponent* Add(UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision = true);

	/**
	 * Places an imported prop. DesiredHeightCm scales the mesh so it stands that tall in the world;
	 * pass 0 to keep its authored size. Sizing from the mesh's own bounds sidesteps the usual FBX
	 * unit mess — it does not matter whether the model was authored in metres or centimetres, a
	 * chair asked to be 95cm tall is 95cm tall.
	 */
	UStaticMeshComponent* Prop(const TCHAR* Name, const FVector& Location, const FRotator& Rotation, float DesiredHeightCm = 0.f, bool bBlockingCollision = true);

	/**
	 * Places an imported prop by its own bounding box rather than by its pivot: Seat is where the
	 * middle of the prop's footprint goes, and the bottom of the prop rests on Seat.Z.
	 *
	 * Prop() puts the mesh's origin at the location it is given, which is only the same thing as
	 * putting the *prop* there if whoever exported it left the origin in the middle of the floor
	 * of the model. Several of these assets did not. book_encyclopedia_set_01 has its origin at
	 * one end of the row — the books run from the origin fifty-five centimetres off to one side —
	 * so a row asked to stand in the middle of a shelf stood with the shelf at one end of it and
	 * the rest of it out in the air, and a row asked to lie on the floor rotated about that same
	 * far corner and went through the boards. Numbers tuned by eye cannot fix that, because the
	 * offset is a different direction for every rotation.
	 */
	UStaticMeshComponent* PropSeated(const TCHAR* Name, const FVector& Seat, const FRotator& Rotation, float DesiredHeightCm = 0.f, bool bBlockingCollision = true);

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
	/**
	 * Returns the variant of Mat tiled for a part of this size and cut from a particular corner of
	 * the photograph, creating and caching it if needed. The location decides the crop, so two
	 * neighbouring strips of wallpaper are never the same piece of wallpaper.
	 */
	UMaterialInterface* ResolveTiling(UMaterialInterface* Mat, const FVector& SizeUU, const FVector& Location);

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

	/** Shared by Stain() and Crack(): the decal component itself, aimed and sized. */
	UDecalComponent* AddDecal(UMaterialInterface* Mat, const FVector& Location, const FRotator& Rotation, const FVector2D& SizeUU);

	/** Base instance per surface set + tint, and the per-size variants derived from them. */
	TMap<FString, UMaterialInstanceDynamic*> SurfaceCache;
	TMap<FString, UMaterialInstanceDynamic*> TilingCache;
	TMap<FString, UMaterialInstanceDynamic*> DecalCache;
	TMap<UMaterialInterface*, FSurfaceOrigin> SurfaceOrigins;
};
