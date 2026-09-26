#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class AActor;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDecalComponent;
class UProceduralMeshComponent;

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
	/** Pale vertical planking. Nothing in Room01 is built from it now — see the skirting note. */
	extern const FRoomSurface PlankWall;
	extern const FRoomSurface Linen;       // paper, book cloth, anything hand-sized
	/**
	 * The same photograph as Linen, tiled nearly three times finer, for the curtains.
	 *
	 * Texel density is solved for the object, and a drape is two metres of one material: at the
	 * honest figure a curtain samples two repeats of the weave over its whole height, which is to
	 * say the threads are a centimetre apart at life size and invisible across a room in the dark.
	 * Cloth is the one surface whose *grain* is the thing being looked at.
	 */
	extern const FRoomSurface Drapery;
	extern const FRoomSurface RustedIron;  // lock, hinges, tools
	/** The stair hall: a chequered entrance floor, panelled wainscot, and marble for the pedestal. */
	extern const FRoomSurface HallTiles;
	extern const FRoomSurface Wainscot;
	extern const FRoomSurface Marble;
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
	/** The corner the detective wakes up in: a bed, and the things that stand around one. */
	extern const TCHAR* Bed;
	extern const TCHAR* Armchair;
	extern const TCHAR* Nightstand;
	/** A press: the tall cupboard in the corner past the head of the bed. */
	extern const TCHAR* Press;
	/** The stair hall. */
	extern const TCHAR* Chandelier;
	extern const TCHAR* Bust;
	extern const TCHAR* Statue;
	extern const TCHAR* LongcaseClock;
	extern const TCHAR* Suitcases;
	extern const TCHAR* CeramicVase;
	extern const TCHAR* BrassVase;
	extern const TCHAR* Candelabra;
	extern const TCHAR* SideTable;
	extern const TCHAR* Console;
	extern const TCHAR* GiltFrame;
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

	/**
	 * Re-instances a placed prop's materials with the master's Tint: how a prop that ships clean
	 * and pale is aged to the house. One slot, or every slot when Slot is INDEX_NONE.
	 */
	static void TintSlots(UStaticMeshComponent* Mesh, const FLinearColor& Tint, int32 Slot = INDEX_NONE);
};

/**
 * What has happened to one pane of glass. Everything here is in the pane's own space: BreakAt is
 * a fraction of the pane, the rest is centimetres and counts.
 */
struct FPaneDamage
{
	/** Where it was hit, as a fraction of the pane. Only used if something radiates from it. */
	FVector2D BreakAt = FVector2D(0.5f, 0.5f);
	/** Radius of the hole knocked out at BreakAt. Zero leaves the pane whole. */
	float HoleRadiusCm = 0.f;
	/** Splits running out from BreakAt. A pane can be starred without losing anything. */
	int32 StarRays = 0;
	/** Splits that wander in from the edges — the ones a house puts in glass without being hit. */
	int32 EdgeCracks = 0;
};

/**
 * Runtime geometry helper. The project is code-first — no editor GUI — so the room is assembled
 * from meshes and tinted primitives at BeginPlay rather than placed by hand. This wraps the
 * NewObject/AttachTo/RegisterComponent dance, the Size-in-world-units convention, and the tiling
 * problem described on Surface() below.
 *
 * Keep one builder alive for a whole build pass: it caches the material instances it creates, and
 * a fresh builder per function would make hundreds of duplicates.
 *
 * An FGCObject because those caches are the builder's own strong references to UObjects, and a
 * plain C++ class cannot hold a UPROPERTY. In practice every instance it hands out is attached to
 * a component almost immediately and kept alive that way, but between creating one and assigning
 * it the only thing referencing it is a bare pointer in a TMap — and a collection landing in that
 * window would leave the cache holding dangling pointers it would go on handing out.
 */
class FRoomBuilder : public FGCObject
{
public:
	FRoomBuilder(AActor* InOwner, USceneComponent* InParent);

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FRoomBuilder"); }
	//~ End FGCObject

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

	/**
	 * The fracture on a struck pane: a translucent sheet laid over the glass, carrying the crack
	 * map that Tools/make_glass_crack.py bakes.
	 *
	 * Neither assembled nor drawn in the material, and both were tried. Assembled, a crack is a
	 * heap of sticks: a bar has to be thick enough to render, and anything thick enough to render
	 * shows its side from everywhere but dead ahead, has two square ends, and keeps one width the
	 * whole way. Drawn from a noise contour — the trick the wall cracks and the cobwebs use — it
	 * is a scribble, because the contour of a smooth field is a smooth meandering curve that loops
	 * and doubles back and has no idea where the stone hit.
	 *
	 * A fracture is very nearly straight lines out of one point, kinking, forking, dying, tied
	 * together by short chords. A generator can lay that out and a material graph cannot, so it is
	 * baked, and what is left here are the things a texture cannot know: the tint, how far the
	 * splits have opened, and how they take the light.
	 *
	 * The sheet must be SQUARE and centred on the impact. The map covers a square patch of glass,
	 * so that the star lands on a pane of any proportion without coming out elliptical.
	 */
	UMaterialInstanceDynamic* GlassCrack(const FLinearColor& Tint, float Opacity = 0.95f,
		float Haze = 0.07f, float Roughness = 0.16f);

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

	/**
	 * A piece of cloth lying over something: a sheet on a bed, a dust cover over a chair.
	 *
	 * Generated rather than assembled, for the same reason the curtains are. Cloth laid over a
	 * shape is a continuous surface that sags between what holds it up and falls away over the
	 * edges, and neither of those is a thing a primitive has: a box is a slab with four corners,
	 * and a heap of squashed spheres — which is the other obvious way to do a thrown-back quilt —
	 * comes out as a clutch of eggs, because each one closes its own outline.
	 *
	 * The surface is a grid in the part's own XY plane, lifted by layered noise (Rumple) and
	 * pulled down near its boundary (EdgeFall) so that the cloth goes over the side of whatever it
	 * is on. The outline is eaten into by the same kind of noise the drapes use, so the hem is
	 * ragged and there are holes in it — a quilt that has been in this house as long as the house
	 * has been shut is not hemmed any more.
	 *
	 * Both faces are generated: M_RoomSurface is single-sided, and the underside of a quilt
	 * hanging over the edge of a bed is half of what is seen of it.
	 */
	UProceduralMeshComponent* Cloth(const FVector& Centre, const FRotator& Facing, const FVector2D& SizeUU,
		float Rumple, float EdgeFall, int32 Seed, UMaterialInterface* Mat, float TexelSizeCm = 34.f);

	/**
	 * A pane of glass with a hole smashed through it.
	 *
	 * The rectangle is kept — the edges of a pane are held in the rebate and that is where glass
	 * survives — and what is removed is a blob around the break point whose radius wanders with
	 * the angle, plus a few narrow wedges running further out from it, which are the splits that
	 * opened and let a piece drop. So the remaining glass is a ragged border with a jagged hole in
	 * the middle of it, which is what a smashed window actually is.
	 *
	 * This cannot be assembled out of boxes. Every arrangement of rectangles around an opening
	 * leaves the opening with straight inner edges, and the straight edge is the entire tell: a
	 * pane that has been hit has no straight line anywhere on it except the four the frame holds.
	 *
	 * The plate is built in the part's own XY plane with Z as its thickness, so a window pane
	 * wants a Facing of (90, 0, 0): that puts local X up the wall, local Y across it and the
	 * plate's normal into the room.
	 *
	 * SplitMat decides what a crack *is*. Left null, every split is cut out of the sheet and reads
	 * as a dark line, because a gap shows whatever is behind the glass. Given a material, the
	 * splits are drawn instead — a thin bar along each leg, in the plane of the pane — and a
	 * polished groove in glass takes a highlight along its whole length and glints white, which is
	 * what a crack in a lit pane actually does. The hole is cut either way.
	 */
	UProceduralMeshComponent* Pane(const FVector& Centre, const FRotator& Facing, const FVector2D& SizeUU,
		const FPaneDamage& Damage, int32 Seed, UMaterialInterface* Mat, UMaterialInterface* SplitMat = nullptr);

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
		TObjectPtr<UMaterialInterface> Asset = nullptr;
		FLinearColor Tint = FLinearColor::White;
		float RoughnessScale = 1.f;
		float TexelSizeCm = 100.f;
	};

	AActor* Owner = nullptr;
	USceneComponent* ParentComponent = nullptr;

	/** Shared by Stain() and Crack(): the decal component itself, aimed and sized. */
	UDecalComponent* AddDecal(UMaterialInterface* Mat, const FVector& Location, const FRotator& Rotation, const FVector2D& SizeUU);

	/** Base instance per surface set + tint, and the per-size variants derived from them. */
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> SurfaceCache;
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> TilingCache;
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> DecalCache;
	TMap<UMaterialInterface*, FSurfaceOrigin> SurfaceOrigins;

	/**
	 * This builder's origin for Mat, falling back to the process-wide registry so a surface made
	 * by one builder still tiles when it is handed to another (clue bodies, the curtains).
	 */
	const FSurfaceOrigin* FindOrigin(UMaterialInterface* Mat);

	/** Records Mat in this builder and in the shared registry. */
	void RegisterOrigin(UMaterialInterface* Mat, const FSurfaceOrigin& Origin);
};
