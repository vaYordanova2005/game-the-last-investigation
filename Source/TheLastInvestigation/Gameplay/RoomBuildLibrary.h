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
 * Authored colours for the room. These are final display-space values (the same convention the
 * menu's fog shader uses) fed straight to BasicShapeMaterial's "Color" parameter, so they read
 * as albedo: everything sits in the 0.03-0.35 range because a house that has been dark for
 * decades has no bright surfaces, and a lantern lighting a 0.6-albedo wall looks like daylight.
 */
namespace RoomPalette
{
	extern const FLinearColor Plaster;        // cracked plaster exposed under torn wallpaper
	extern const FLinearColor Wallpaper;      // the wallpaper's surviving colour
	extern const FLinearColor WallpaperFaded; // sun/damp-bleached patches of the same paper
	extern const FLinearColor CeilingStain;   // water damage rings
	extern const FLinearColor RottenWood;     // grey-brown, moisture-eaten
	extern const FLinearColor DarkWood;       // furniture and beams, still holding some colour
	extern const FLinearColor Mold;           // damp-corner growth
	extern const FLinearColor Rust;
	extern const FLinearColor Iron;
	extern const FLinearColor Brass;
	extern const FLinearColor Paper;          // letters, book pages
	extern const FLinearColor Photo;          // faded photographic print
	extern const FLinearColor GlassShard;
	extern const FLinearColor Cloth;          // curtains, upholstery
	extern const FLinearColor DriedBlood;
	extern const FLinearColor DustFilm;       // the grey film lying over every upward face
	extern const FLinearColor NightSky;
	extern const FLinearColor Foliage;
	extern const FLinearColor Rain;
}

/** Lazily loaded engine basic shapes. All of them are 100uu across, so scale is always Size/100. */
struct FRoomShapes
{
	static UStaticMesh* Cube();
	static UStaticMesh* Cylinder();
	static UStaticMesh* Cone();
	static UStaticMesh* Sphere();
	static UStaticMesh* Plane();
};

/**
 * Runtime geometry helper. The whole project is code-first (no editor GUI), so every prop in the
 * room is assembled from tinted engine primitives at BeginPlay rather than placed by hand. This
 * wraps the NewObject/AttachTo/RegisterComponent dance and the Size-in-world-units convention so
 * the dressing code reads as a list of shapes instead of boilerplate.
 */
class FRoomBuilder
{
public:
	FRoomBuilder(AActor* InOwner, USceneComponent* InParent);

	/** A dynamic BasicShapeMaterial instance. Cheap enough to make one per material *variant*, not per part. */
	UMaterialInstanceDynamic* Material(const FLinearColor& Color, float Roughness = 0.92f, float Metallic = 0.f) const;

	UStaticMeshComponent* Add(UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision = true);

	UStaticMeshComponent* Box(const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision = true);
	UStaticMeshComponent* Cyl(const FVector& Location, const FRotator& Rotation, const FVector& SizeUU, UMaterialInterface* Mat, bool bBlockingCollision = true);
	UStaticMeshComponent* Sph(const FVector& Location, float Diameter, UMaterialInterface* Mat, bool bBlockingCollision = false);

	/**
	 * A stain/mark: a paper-thin slab laid flat against a surface. Never collides, so it can't
	 * catch the interaction trace, and is pushed a fraction off the surface to avoid z-fighting.
	 */
	UStaticMeshComponent* Mark(const FVector& Location, const FRotator& Rotation, const FVector2D& SizeUU, UMaterialInterface* Mat);

	UInstancedStaticMeshComponent* Instances(UStaticMesh* Mesh, UMaterialInterface* Mat);

	USceneComponent* Parent() const { return ParentComponent; }

private:
	AActor* Owner = nullptr;
	USceneComponent* ParentComponent = nullptr;
};
