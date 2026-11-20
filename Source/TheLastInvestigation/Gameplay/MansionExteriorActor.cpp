#include "MansionExteriorActor.h"
#include "StormForestActor.h"
#include "StormWindowActor.h"
#include "LibraryActor.h"
#include "MusicRoomActor.h"
#include "RoomBuildLibrary.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	// Every roof but the porch's and the turret's is pitched at fifty degrees: steep, as Gothic
	// Revival roofs were, and the gables tall enough to carry their bargeboards.
	constexpr float ExtPitchDeg = 50.f;
	const float ExtTan = FMath::Tan(FMath::DegreesToRadians(ExtPitchDeg));
	/** The roofs' thickness, measured vertically: slates, battens, rafters. */
	constexpr float ExtRoofThick = 16.f;
	constexpr float ExtEavesOver = 40.f;
	/** How far a roof runs out past a gable wall's face. */
	constexpr float ExtRakeOver = 34.f;
	/** Stone dressings round an opening, and how far they stand proud of the brick. */
	constexpr float ExtSurround = 16.f;
	constexpr float ExtSurroundProud = 4.f;
	/** Where an opening's reveal stops, in from the wall face: short of the sash (StormWindowActor: SashX). */
	constexpr float ExtRevealDepth = -5.f;
	/**
	 * The bottom of every wall: under the lawn (the ground is at -380, the lawn ten over it), and over
	 * the cellar's ceiling slabs (-385..-375). The maid's room runs out under the west front, and at
	 * -420 the wall's foot and its plinth stood 35cm down into the room along its whole ceiling, a
	 * grey beam that was not wood (the user, 2026-11-20).
	 */
	constexpr float ExtWallFoot = -374.f;
	constexpr float ExtPlinthTop = -300.f;
	constexpr float ExtStringZ0 = -30.f;
	constexpr float ExtStringZ1 = -14.f;

	/** The turret, round the north-east corner. */
	const FVector2D ExtTurretCentre(AMansionExteriorActor::EastFace, AMansionExteriorActor::NorthFace);
	constexpr float ExtTurretApothem = 240.f;
	constexpr int32 ExtTurretSides = 12;
	constexpr float ExtTurretPitchDeg = 65.f;

	// Texel sizes, from the photographs' own dimensions (Poly Haven).
	const FRoomSurface ExtBrick{ TEXT("brick_wall_10"), 190.f };
	const FRoomSurface ExtStone{ TEXT("concrete_wall_007"), 216.f };
	const FRoomSurface ExtSlate{ TEXT("roof_slates_02"), 300.f };
	const FRoomSurface ExtGravel{ TEXT("gravel_stones"), 200.f };
	const FRoomSurface ExtLawn{ TEXT("forrest_ground_01"), 200.f };

	// Shutter leaves (Tools/make_exterior.py: SHUTTERS): width of one leaf, height.
	struct FExtShutterModel { const TCHAR* Name; float Width; float Height; };
	const FExtShutterModel ExtShutters[] = {
		{ TEXT("exterior_shutter_tall"), 72.f, 222.f },
		{ TEXT("exterior_shutter_low"), 72.f, 192.f },
		{ TEXT("exterior_shutter_narrow"), 46.f, 172.f },
	};
	enum : int32 { ExtShutterTall = 0, ExtShutterLow = 1, ExtShutterNarrow = 2 };

	/** Ivy cards (Tools/make_exterior_art.py): 50cm of wall to a cell, a 2 x 2 atlas. */
	constexpr float ExtIvyCard = 50.f;

	// ---------------------------------------------------------------------------------------------
	// Generated geometry, batched by material, UVs in centimetres.
	// ---------------------------------------------------------------------------------------------

	struct FExtSection
	{
		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Tris;
	};

	/** Two axes in a face's plane for a planar projection: horizontal along it, and up it. */
	void ExtPlanarAxes(const FVector& N, FVector& U, FVector& V)
	{
		if (FMath::Abs(N.Z) > 0.92f)
		{
			U = FVector(1.f, 0.f, 0.f);
			V = FVector(0.f, 1.f, 0.f);
			return;
		}
		U = FVector::CrossProduct(FVector::UpVector, N).GetSafeNormal();
		V = FVector::CrossProduct(N, U).GetSafeNormal();
	}

	class FExtMesh
	{
	public:
		/**
		 * A planar polygon, convex or fanned from its first point. UVs are the points projected on
		 * UAxis/VAxis in centimetres, over the texel size. Both windings, the normal forced (the
		 * 09-21 pane lesson), so nothing here can come out invisible from the wrong side.
		 */
		void Poly(UMaterialInterface* Mat, TConstArrayView<FVector> Pts, const FVector& Normal, const FVector& UAxis, const FVector& VAxis, float Texel)
		{
			if (!Mat || Pts.Num() < 3)
			{
				return;
			}
			FExtSection& S = Get(Mat);
			const int32 First = S.Verts.Num();
			for (const FVector& P : Pts)
			{
				S.Verts.Add(P);
				S.Normals.Add(Normal);
				S.UVs.Add(FVector2D(FVector::DotProduct(P, UAxis) / Texel, -FVector::DotProduct(P, VAxis) / Texel));
				S.Tangents.Add(FProcMeshTangent(UAxis, false));
			}
			for (int32 i = 1; i + 1 < Pts.Num(); ++i)
			{
				S.Tris.Append({ First, First + i, First + i + 1, First, First + i + 1, First + i });
			}
		}

		/** A planar polygon projected by its own normal's planar axes (world-aligned). */
		void PolyWorld(UMaterialInterface* Mat, TConstArrayView<FVector> Pts, const FVector& Normal, float Texel)
		{
			FVector U, V;
			ExtPlanarAxes(Normal, U, V);
			Poly(Mat, Pts, Normal, U, V, Texel);
		}

		/**
		 * A box. bLocalGrain lays the UVs along the box's own long axis (local X) — wood, whose grain
		 * runs along the board; otherwise every face is projected in world space, so stone and brick
		 * line up from one piece to the next.
		 */
		void Box(UMaterialInterface* Mat, const FVector& Centre, const FQuat& Rot, const FVector& Size, float Texel, bool bLocalGrain = false)
		{
			const FVector X = Rot.GetAxisX(), Y = Rot.GetAxisY(), Z = Rot.GetAxisZ();
			const FVector H = Size * 0.5f;
			auto Corner = [&](float a, float b, float c) { return Centre + X * (a * H.X) + Y * (b * H.Y) + Z * (c * H.Z); };
			struct FFace { FVector N; FVector A, B, C, D; FVector U, V; };
			const FFace Faces[] = {
				{ X, Corner(1, -1, -1), Corner(1, 1, -1), Corner(1, 1, 1), Corner(1, -1, 1), Y, Z },
				{ -X, Corner(-1, 1, -1), Corner(-1, -1, -1), Corner(-1, -1, 1), Corner(-1, 1, 1), Y, Z },
				{ Y, Corner(1, 1, -1), Corner(-1, 1, -1), Corner(-1, 1, 1), Corner(1, 1, 1), X, Z },
				{ -Y, Corner(-1, -1, -1), Corner(1, -1, -1), Corner(1, -1, 1), Corner(-1, -1, 1), X, Z },
				{ Z, Corner(-1, -1, 1), Corner(1, -1, 1), Corner(1, 1, 1), Corner(-1, 1, 1), X, Y },
				{ -Z, Corner(-1, 1, -1), Corner(1, 1, -1), Corner(1, -1, -1), Corner(-1, -1, -1), X, Y },
			};
			for (const FFace& F : Faces)
			{
				const FVector Pts[] = { F.A, F.B, F.C, F.D };
				if (bLocalGrain)
				{
					Poly(Mat, Pts, F.N, F.U, F.V, Texel);
				}
				else
				{
					PolyWorld(Mat, Pts, F.N, Texel);
				}
			}
		}

		UProceduralMeshComponent* Commit(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
		{
			if (Order.Num() == 0)
			{
				return nullptr;
			}
			UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), Name));
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
			Mesh->bUseAsyncCooking = false;
			for (int32 i = 0; i < Order.Num(); ++i)
			{
				const FExtSection& S = Sections[Order[i]];
				Mesh->CreateMeshSection_LinearColor(i, S.Verts, S.Tris, S.Normals, S.UVs, TArray<FLinearColor>(), S.Tangents, /*bCreateCollision*/ false);
				Mesh->SetMaterial(i, Order[i]);
			}
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->RegisterComponent();
			Owner->AddInstanceComponent(Mesh);
			Order.Reset();
			Sections.Reset();
			return Mesh;
		}

	private:
		FExtSection& Get(UMaterialInterface* Mat)
		{
			if (FExtSection* Found = Sections.Find(Mat))
			{
				return *Found;
			}
			Order.Add(Mat);
			return Sections.Add(Mat);
		}

		TMap<UMaterialInterface*, FExtSection> Sections;
		TArray<UMaterialInterface*> Order;
	};

	// ---------------------------------------------------------------------------------------------
	// The house's walls, as planes with openings in them.
	// ---------------------------------------------------------------------------------------------

	/** A wall plane: outward normal, the face's offset along it, its extent in (U, Z). */
	struct FExtFacade
	{
		FVector N;
		float D;
		float U0, U1, Z0, Z1;
		/** An eaves wall carries the corbel table under its roof; a gable wall's is its string. */
		bool bCorbels = true;
		/** The turret's facets: no plinth, string or corbels from the flat walls' code. */
		bool bTurret = false;

		FVector Along() const { return FVector::CrossProduct(N, FVector::UpVector); }
		FVector At(float U, float Z, float Depth) const { return Along() * U + N * (D + Depth) + FVector(0.f, 0.f, Z); }
		float UOf(const FVector& P) const { return FVector::DotProduct(P, Along()); }
	};

	enum class EExtHole : uint8 { Window, Arched, Door, Blind };

	struct FExtHole
	{
		int32 Facade;
		float U0, U1, Z0, Z1;
		EExtHole Kind;
		int32 Shutter = INDEX_NONE;
		int32 Seed = 0;

		float Mid() const { return (U0 + U1) * 0.5f; }
		float Radius() const { return (U1 - U0) * 0.5f; }
	};

	/** A gable wall: the triangle over a wall's eaves line, and the roof it is the end of. */
	struct FExtGable
	{
		int32 Facade;
		float U0, U1, Z;
		float ApexU;
		bool bVent = true;

		float RidgeZ() const { return Z + (U1 - U0) * 0.5f * ExtTan; }
	};

	/** A gabled roof: the ridge from A to B (plan), half the span, the eaves, the overhang each side. */
	struct FExtRoof
	{
		FVector2D A, B;
		float HalfSpan;
		float EavesZ;
		float OverLeft, OverRight;
		float RakeA, RakeB;
		/** The pitch's tangent; zero for the house's fifty degrees. */
		float Tan = 0.f;

		float Pitch() const { return Tan > 0.f ? Tan : ExtTan; }
	};

	/** Cusped scallops along a board's lower edge: how far the edge is lifted at W along it. */
	float ExtScallop(float W, float Period, float Radius)
	{
		const float C = FMath::Fmod(FMath::Abs(W), Period) - Period * 0.5f;
		const float T = FMath::Clamp(C / Radius, -1.f, 1.f);
		return Radius * FMath::Sqrt(FMath::Max(0.f, 1.f - T * T));
	}

	UMaterialInstanceDynamic* ExtSheet(AActor* Owner, const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale = 1.f)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), Set.Set, Set.Set));
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Parent, Owner);
		Mat->SetVectorParameterValue(TEXT("Tint"), Tint);
		Mat->SetScalarParameterValue(TEXT("RoughnessScale"), RoughnessScale);
		// The generated geometry's UVs are already in repeats of the texel size.
		Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
		Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		return Mat;
	}

	void ExtDressSlots(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
	{
		if (!Mesh)
		{
			return;
		}
		for (const TPair<const TCHAR*, UMaterialInterface*>& Slot : Slots)
		{
			if (Slot.Value)
			{
				Mesh->SetMaterialByName(FName(Slot.Key), Slot.Value);
			}
		}
	}

	/** A model's frame turned onto a wall: its +X along the wall, its +Y out of it. */
	FQuat ExtOnWall(const FExtFacade& F, float Yaw = 0.f)
	{
		const FMatrix Basis(F.Along(), F.N, FVector::UpVector, FVector::ZeroVector);
		return FQuat(Basis) * FQuat(FRotator(0.f, Yaw, 0.f));
	}
}

AMansionExteriorActor::AMansionExteriorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	ExteriorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ExteriorRoot"));
	SetRootComponent(ExteriorRoot);
	ExteriorRoot->SetMobility(EComponentMobility::Movable);

	// The moon, on the house's outside only (lighting channel 1): no indirect light, no fog, no
	// translucency, so it cannot get into a room by any route, and it is never the light the
	// renderer picks for forward shading (the storm's is, ForwardShadingPriority).
	Moonlight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moonlight"));
	Moonlight->SetupAttachment(ExteriorRoot);
	Moonlight->SetMobility(EComponentMobility::Movable);
	Moonlight->SetLightingChannels(false, true, false);
	// Bright for moonlight, because the house's exposure is set for a room lit by a flame and a window:
	// at an honest half lux the outside is a silhouette.
	Moonlight->SetIntensity(40.f);
	Moonlight->SetLightColor(FLinearColor(0.55f, 0.64f, 0.92f));
	Moonlight->SetCastShadows(true);
	Moonlight->SetIndirectLightingIntensity(0.f);
	Moonlight->SetVolumetricScatteringIntensity(0.f);
	Moonlight->bAffectTranslucentLighting = false;
	Moonlight->SetForwardShadingPriority(0);
	Moonlight->SetAtmosphereSunLight(false);

	// The overcast's own glow, from high in the west, unshadowed: without it every wall the moon
	// does not face is a black cut-out. Same rules as the moon: the outside only.
	SkyFill = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SkyFill"));
	SkyFill->SetupAttachment(ExteriorRoot);
	SkyFill->SetMobility(EComponentMobility::Movable);
	SkyFill->SetLightingChannels(false, true, false);
	SkyFill->SetIntensity(32.f);
	SkyFill->SetLightColor(FLinearColor(0.45f, 0.55f, 0.75f));
	SkyFill->SetCastShadows(false);
	SkyFill->SetIndirectLightingIntensity(0.f);
	SkyFill->SetVolumetricScatteringIntensity(0.f);
	SkyFill->bAffectTranslucentLighting = false;
	SkyFill->SetForwardShadingPriority(0);
	SkyFill->SetAtmosphereSunLight(false);
	SkyFill->SetRelativeRotation(FRotator(-58.f, -30.f, 0.f));
}

bool AMansionExteriorActor::IsInHouse(float X, float Y, float Margin)
{
	const bool bBlock = X > WestFace - Margin && X < BlockEastFace + Margin && Y > NorthFace - Margin && Y < SouthFace + Margin;
	const bool bWing = X > BlockEastFace - Margin && X < EastFace + Margin && Y > NorthFace - Margin && Y < WingSouthFace + Margin;
	const bool bTurret = FVector2D::Distance(FVector2D(X, Y), ExtTurretCentre) < ExtTurretApothem + Margin;
	return bBlock || bWing || bTurret;
}

void AMansionExteriorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager || !Forest)
	{
		return;
	}
	// Outside means out of the walls and above the ground: under the bank is the cellar.
	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	const bool bOutside = !IsInHouse(Eye.X, Eye.Y) && Eye.Z > Forest->GroundHeight(Eye.X, Eye.Y) - 30.f;
	AStormWindowActor::SetViewOutside(bOutside);
	// The moon and the sky's fill are for looking at the house from outside. Through a window from
	// inside, the lawn, the porch roof and the forest stay as the storm alone lights them.
	if (Moonlight->IsVisible() != bOutside)
	{
		Moonlight->SetVisibility(bOutside);
		SkyFill->SetVisibility(bOutside);
	}
}

void AMansionExteriorActor::Configure(const AStormForestActor* InForest)
{
	Forest = InForest;
}

void AMansionExteriorActor::BeginPlay()
{
	Super::BeginPlay();
	Moonlight->SetWorldRotation((-AStormForestActor::MoonDirection()).Rotation());
	BuildExterior();
}

void AMansionExteriorActor::DressFrontDoor(UStaticMeshComponent* Door, AActor* Owner)
{
	if (!Door || !Owner)
	{
		return;
	}
	// New black paint, satin, over the smooth paint photograph (the master bedroom's door, 10-27).
	UMaterialInstanceDynamic* Paint = ExtSheet(Owner, RoomSurfaces::Paint, FLinearColor(0.024f, 0.024f, 0.026f), 0.55f);
	FRoomBuilder Build(Owner, Door);
	UMaterialInterface* Brass = Build.Flat(FLinearColor(0.34f, 0.24f, 0.09f), 0.36f, 1.f);
	UMaterialInterface* Glass = Build.Flat(FLinearColor(0.010f, 0.012f, 0.014f), 0.07f);
	UMaterialInterface* Shadow = Build.Flat(FLinearColor(0.004f, 0.004f, 0.004f), 0.95f);
	UMaterialInterface* Iron = Build.Flat(FLinearColor(0.03f, 0.03f, 0.03f), 0.6f, 0.6f);
	ExtDressSlots(Door, { { TEXT("Paint"), Paint }, { TEXT("Brass"), Brass }, { TEXT("Glass"), Glass }, { TEXT("Shadow"), Shadow }, { TEXT("Iron"), Iron } });
}

void AMansionExteriorActor::BuildExterior()
{
	if (!Forest)
	{
		return;
	}
	FRoomBuilder Build(this, ExteriorRoot);
	FRandomStream Random(18870611);

	// ---- Materials --------------------------------------------------------------------------
	UMaterialInterface* Brick = ExtSheet(this, ExtBrick, FLinearColor(0.95f, 0.95f, 0.95f));
	UMaterialInterface* Stone = ExtSheet(this, ExtStone, FLinearColor(0.33f, 0.355f, 0.45f));
	UMaterialInterface* Slate = ExtSheet(this, ExtSlate, FLinearColor(0.17f, 0.215f, 0.31f));
	UMaterialInterface* Trim = ExtSheet(this, RoomSurfaces::RoughWood, FLinearColor(0.33f, 0.55f, 0.87f), 1.2f);
	UMaterialInterface* ShutterPaint = ExtSheet(this, RoomSurfaces::RoughWood, FLinearColor(0.29f, 0.62f, 0.78f), 1.2f);
	UMaterialInterface* Boards = ExtSheet(this, RoomSurfaces::RoughWood, FLinearColor(0.62f, 0.78f, 1.05f), 1.3f);
	// Both matt: under a low moon, looked at against it, ground with any sheen at all is a sheet of snow.
	UMaterialInterface* Gravel = ExtSheet(this, ExtGravel, FLinearColor(0.5f, 0.5f, 0.5f), 3.f);
	// As dark as the forest's own ground under the same light, so where the lawn ends does not show.
	UMaterialInterface* Lawn = ExtSheet(this, ExtLawn, FLinearColor(0.011f, 0.015f, 0.017f), 3.f);
	UMaterialInterface* Iron = Build.Flat(FLinearColor(0.025f, 0.025f, 0.027f), 0.55f, 0.5f);
	UMaterialInterface* Dark = Build.Flat(FLinearColor(0.004f, 0.004f, 0.005f), 0.95f);
	UMaterialInterface* DarkGlass = Build.Flat(FLinearColor(0.008f, 0.009f, 0.011f), 0.06f);
	UMaterialInterface* Pot = Build.Flat(FLinearColor(0.055f, 0.026f, 0.016f), 0.85f);
	for (UMaterialInterface* M : { Brick, Stone, Slate, Trim, ShutterPaint, Boards, Gravel, Lawn })
	{
		Materials.Add(M);
	}

	FExtMesh Walls;    // brick
	FExtMesh Dress;    // stone, wood and iron detail
	FExtMesh Roofs;
	FExtMesh GroundSheet;   // lawn and drive: lit as the forest's ground is, by the storm alone

	// ---- Facades ------------------------------------------------------------------------------
	TArray<FExtFacade> Facades;
	const FVector XP(1.f, 0.f, 0.f), XN(-1.f, 0.f, 0.f), YP(0.f, 1.f, 0.f), YN(0.f, -1.f, 0.f);
	// The west block.
	const int32 BlockSouth = Facades.Add({ YP, SouthFace, WestFace, BlockEastFace, ExtWallFoot, BlockEavesZ, false });
	const int32 BlockNorth = Facades.Add({ YN, -NorthFace, -BlockEastFace, -WestFace, ExtWallFoot, BlockEavesZ, false });
	const int32 BlockWest = Facades.Add({ XN, -WestFace, NorthFace, SouthFace, ExtWallFoot, BlockEavesZ });
	const int32 BlockEastLow = Facades.Add({ XP, BlockEastFace, -SouthFace, -WingSouthFace, ExtWallFoot, BlockEavesZ });
	// Over the wing's roof: its foot is under the wing's slates.
	const int32 BlockEastHigh = Facades.Add({ XP, BlockEastFace, -WingSouthFace, -NorthFace, WingEavesZ - 20.f, BlockEavesZ });
	// The east wing.
	const int32 WingEast = Facades.Add({ XP, EastFace, -WingSouthFace, -NorthFace, ExtWallFoot, WingEavesZ });
	const int32 WingNorth = Facades.Add({ YN, -NorthFace, -EastFace, -BlockEastFace, ExtWallFoot, WingEavesZ, false });
	const int32 WingSouth = Facades.Add({ YP, WingSouthFace, BlockEastFace, EastFace, ExtWallFoot, WingEavesZ, false });
	const int32 FlatFacades = Facades.Num();

	// The turret's facets: one faces north-east, the rest go round from it.
	const float TurretHalf = ExtTurretApothem * FMath::Tan(PI / ExtTurretSides);
	TArray<float> TurretAngles;
	for (int32 k = 0; k < ExtTurretSides; ++k)
	{
		const float Angle = -45.f + 360.f * k / ExtTurretSides;
		TurretAngles.Add(Angle);
		const FVector N(FMath::Cos(FMath::DegreesToRadians(Angle)), FMath::Sin(FMath::DegreesToRadians(Angle)), 0.f);
		const FVector2D Mid = ExtTurretCentre + FVector2D(N.X, N.Y) * ExtTurretApothem;
		// The facets in the corner the house occupies only show over the wing's roof.
		const bool bInside = Mid.X < EastFace - 1.f && Mid.Y > NorthFace + 1.f;
		FExtFacade F{ N, FVector::DotProduct(FVector(ExtTurretCentre, 0.f), N) + ExtTurretApothem - Skin, 0.f, 0.f, bInside ? WingEavesZ + 80.f : ExtWallFoot, TurretEavesZ };
		const float UC = F.UOf(FVector(ExtTurretCentre, 0.f));
		F.U0 = UC - TurretHalf;
		F.U1 = UC + TurretHalf;
		F.bTurret = true;
		Facades.Add(F);
	}

	// ---- Openings ---------------------------------------------------------------------------
	TArray<FExtHole> Holes;
	auto FindFacade = [&](const FVector& N, const FVector& FacePoint, float Z) -> int32
	{
		for (int32 i = 0; i < FlatFacades; ++i)
		{
			const FExtFacade& F = Facades[i];
			const float U = F.UOf(FacePoint);
			if (FVector::DotProduct(F.N, N) > 0.9f && FMath::Abs(FVector::DotProduct(FacePoint, F.N) - F.D) < 15.f
				&& U > F.U0 - 1.f && U < F.U1 + 1.f && Z > F.Z0 && Z < F.Z1)
			{
				return i;
			}
		}
		return INDEX_NONE;
	};
	// The real windows, where the rooms put them.
	for (TActorIterator<AStormWindowActor> It(GetWorld()); It; ++It)
	{
		const FStormWindowSetup& S = It->GetSetup();
		const FVector Out = It->GetActorForwardVector();
		const FVector Face = It->GetActorLocation() + Out * (S.WallThickness * 0.5f);
		const float Z0 = It->GetActorLocation().Z + S.SillHeight;
		const float Z1 = It->GetActorLocation().Z + S.TopHeight;
		const int32 Index = FindFacade(Out, Face, (Z0 + Z1) * 0.5f);
		if (!ensureMsgf(Index != INDEX_NONE, TEXT("Exterior: no wall for the window at %s"), *It->GetActorLocation().ToString()))
		{
			continue;
		}
		const AActor* Room = It->GetOwner();
		const bool bArched = Room && (Room->IsA<ALibraryActor>() || Room->IsA<AMusicRoomActor>());
		const float U = Facades[Index].UOf(Face);
		Holes.Add({ Index, U - S.OpeningWidth * 0.5f, U + S.OpeningWidth * 0.5f, Z0, Z1, bArched ? EExtHole::Arched : EExtHole::Window });
	}
	// The front door, on the corridor's line.
	Holes.Add({ WingEast, -FrontDoorY - DoorUnitWidth * 0.5f, -FrontDoorY + DoorUnitWidth * 0.5f, GroundFloorZ, GroundFloorZ + DoorUnitHeight, EExtHole::Door });
	// Blind windows, shuttered: where a wall with nothing behind it (the ground floor under the wing,
	// the filled corners) or a room with no window on that side would otherwise be a blank.
	auto Blind = [&](int32 Facade, float U, float Z0, int32 Shutter)
	{
		const FExtShutterModel& M = ExtShutters[Shutter];
		Holes.Add({ Facade, U - M.Width, U + M.Width, Z0, Z0 + M.Height, EExtHole::Blind, Shutter, 9000 + Holes.Num() * 17 });
	};
	const float LowSill = GroundFloorZ + 70.f;
	const float WingUpperSill = 60.f;
	const float BlockUpperSill = 120.f;
	const float BlockLowSill = -287.f;
	Blind(WingEast, 0.f, LowSill, ExtShutterLow);               // under Room01
	Blind(WingEast, -825.f, LowSill, ExtShutterLow);            // under the girl's room
	Blind(WingNorth, 1060.f, LowSill, ExtShutterLow);           // under the study
	Blind(WingNorth, 600.f, LowSill, ExtShutterLow);
	Blind(WingNorth, 200.f, LowSill, ExtShutterLow);
	Blind(WingNorth, 200.f, WingUpperSill, ExtShutterTall);
	for (const float X : { -1090.f, -620.f, 35.f })
	{
		Blind(WingSouth, X, LowSill, ExtShutterLow);
		Blind(WingSouth, X, WingUpperSill, ExtShutterTall);
	}
	for (const float Y : { 1230.f, 1700.f })
	{
		Blind(BlockEastLow, -Y, BlockLowSill, ExtShutterLow);
		Blind(BlockEastLow, -Y, BlockUpperSill, ExtShutterTall);
		Blind(BlockWest, Y, BlockUpperSill, ExtShutterTall);
		// Under the bank the ground floor has no window.
		if (Forest->GroundHeight(WestFace - 60.f, Y) < BlockLowSill - 30.f)
		{
			Blind(BlockWest, Y, BlockLowSill, ExtShutterLow);
		}
	}
	Blind(BlockWest, -400.f, BlockUpperSill, ExtShutterTall);
	Blind(BlockNorth, 2140.f, BlockUpperSill, ExtShutterTall);
	if (Forest->GroundHeight(-2140.f, NorthFace - 60.f) < BlockLowSill - 30.f)
	{
		Blind(BlockNorth, 2140.f, BlockLowSill, ExtShutterLow);
	}
	// The turret: the north-east facet on all three floors, its neighbours on the upper two.
	for (int32 k = 0; k < ExtTurretSides; ++k)
	{
		const float Angle = TurretAngles[k];
		const int32 Index = FlatFacades + k;
		const float U = (Facades[Index].U0 + Facades[Index].U1) * 0.5f;
		if (FMath::IsNearlyEqual(Angle, -45.f))
		{
			Blind(Index, U, LowSill + 10.f, ExtShutterNarrow);
		}
		if (FMath::IsNearlyEqual(Angle, -45.f) || FMath::IsNearlyEqual(Angle, -15.f) || FMath::IsNearlyEqual(Angle, -75.f))
		{
			Blind(Index, U, WingUpperSill, ExtShutterNarrow);
			Blind(Index, U, 480.f, ExtShutterNarrow);
		}
	}

	auto HolesOn = [&](int32 Facade, float Grow) -> TArray<FBox2D>
	{
		TArray<FBox2D> Out;
		for (const FExtHole& H : Holes)
		{
			if (H.Facade == Facade)
			{
				Out.Add(FBox2D(FVector2D(H.U0 - Grow, H.Z0 - Grow), FVector2D(H.U1 + Grow, H.Z1 + Grow)));
			}
		}
		return Out;
	};

	// ---- Gables and roofs ---------------------------------------------------------------------
	const float WingRidgeEastX = (EastFace + (EastFace - 830.f)) * 0.5f;   // -5
	const float WingValleyX = EastFace - 830.f;                           // -420
	const float WingRidgeWestX = (WingValleyX + BlockEastFace) * 0.5f;    // -835
	const float BlockRidgeX = (WestFace + BlockEastFace) * 0.5f;          // -1770
	TArray<FExtGable> Gables = {
		{ BlockSouth, WestFace, BlockEastFace, BlockEavesZ, BlockRidgeX },
		{ BlockNorth, -BlockEastFace, -WestFace, BlockEavesZ, -BlockRidgeX },
		{ WingNorth, -EastFace, -WingValleyX, WingEavesZ, -WingRidgeEastX },
		{ WingNorth, -WingValleyX, -BlockEastFace, WingEavesZ, -WingRidgeWestX },
		{ WingSouth, WingValleyX, EastFace, WingEavesZ, WingRidgeEastX },
		{ WingSouth, BlockEastFace, WingValleyX, WingEavesZ, WingRidgeWestX },
		// The cross gable over the entrance.
		{ WingEast, -FrontDoorY - 210.f, -FrontDoorY + 210.f, WingEavesZ, -FrontDoorY, true },
	};
	const TArray<FExtRoof> RoofList = {
		{ FVector2D(BlockRidgeX, SouthFace), FVector2D(BlockRidgeX, NorthFace), (BlockEastFace - WestFace) * 0.5f, BlockEavesZ, ExtEavesOver, ExtEavesOver, ExtRakeOver, ExtRakeOver },
		{ FVector2D(WingRidgeEastX, WingSouthFace), FVector2D(WingRidgeEastX, NorthFace), (EastFace - WingValleyX) * 0.5f, WingEavesZ, 0.f, ExtEavesOver, ExtRakeOver, ExtRakeOver },
		{ FVector2D(WingRidgeWestX, WingSouthFace), FVector2D(WingRidgeWestX, NorthFace), (WingValleyX - BlockEastFace) * 0.5f, WingEavesZ, 0.f, 0.f, ExtRakeOver, ExtRakeOver },
		// The cross gable's roof runs back until it is under the wing's: its ridge meets the slope there.
		{ FVector2D(EastFace, FrontDoorY), FVector2D(EastFace - 210.f, FrontDoorY), 210.f, WingEavesZ, 30.f, 30.f, ExtRakeOver, 0.f },
	};

	// ---- Walls ------------------------------------------------------------------------------
	for (int32 i = 0; i < Facades.Num(); ++i)
	{
		const FExtFacade& F = Facades[i];
		const TArray<FBox2D> Pieces = RoomWalls::CutAround(HolesOn(i, 0.f), F.U0, F.U1, F.Z0, F.Z1);
		for (const FBox2D& P : Pieces)
		{
			const FVector Pts[] = { F.At(P.Min.X, P.Min.Y, Skin), F.At(P.Max.X, P.Min.Y, Skin), F.At(P.Max.X, P.Max.Y, Skin), F.At(P.Min.X, P.Max.Y, Skin) };
			Walls.Poly(Brick, Pts, F.N, F.Along(), FVector::UpVector, ExtBrick.TexelSizeCm);
		}
		// The arched windows' heads: the brick between the arch and the rectangle cut for it.
		for (const FExtHole& H : Holes)
		{
			if (H.Facade != i || H.Kind != EExtHole::Arched)
			{
				continue;
			}
			const float R = H.Radius();
			const float Spring = H.Z1 - R;
			const int32 Steps = 16;
			for (const float Side : { -1.f, 1.f })
			{
				TArray<FVector> Pts;
				const float CornerU = H.Mid() + Side * R;
				for (int32 k = 0; k <= Steps; ++k)
				{
					const float A = HALF_PI * k / Steps;
					Pts.Add(F.At(H.Mid() + Side * R * FMath::Cos(A), Spring + R * FMath::Sin(A), Skin));
				}
				Pts.Add(F.At(CornerU, H.Z1, Skin));
				// Fanned from the corner: every triangle has the corner as its apex.
				TArray<FVector> Fan = { Pts.Last() };
				for (int32 k = 0; k < Pts.Num() - 1; ++k)
				{
					Fan.Add(Pts[k]);
				}
				Walls.Poly(Brick, Fan, F.N, F.Along(), FVector::UpVector, ExtBrick.TexelSizeCm);
			}
		}
	}
	for (const FExtGable& G : Gables)
	{
		const FExtFacade& F = Facades[G.Facade];
		const FVector Pts[] = { F.At(G.U0, G.Z, Skin), F.At(G.U1, G.Z, Skin), F.At(G.ApexU, G.RidgeZ(), Skin) };
		Walls.Poly(Brick, Pts, F.N, F.Along(), FVector::UpVector, ExtBrick.TexelSizeCm);
	}

	// ---- Dressings round the openings -------------------------------------------------------
	auto WallBox = [&](FExtMesh& Mesh, UMaterialInterface* Mat, const FExtFacade& F, float U0, float U1, float Z0, float Z1, float Depth0, float Depth1, float Texel)
	{
		const FVector C = F.At((U0 + U1) * 0.5f, (Z0 + Z1) * 0.5f, (Depth0 + Depth1) * 0.5f);
		const FQuat Q = ExtOnWall(F);
		Mesh.Box(Mat, C, Q, FVector(U1 - U0, Depth1 - Depth0, Z1 - Z0), Texel);
	};
	// A band round an arch between two radii, from one depth to another: its face, its soffit and
	// its back edge.
	auto ArchBand = [&](FExtMesh& Mesh, UMaterialInterface* Mat, const FExtFacade& F, float MidU, float Spring, float R0, float R1, float Depth0, float Depth1, float Texel)
	{
		const int32 Steps = 20;
		for (int32 k = 0; k < Steps; ++k)
		{
			const float A0 = PI * k / Steps, A1 = PI * (k + 1) / Steps;
			auto P = [&](float R, float A, float Depth) { return F.At(MidU + R * FMath::Cos(A), Spring + R * FMath::Sin(A), Depth); };
			const FVector Face[] = { P(R0, A0, Depth1), P(R1, A0, Depth1), P(R1, A1, Depth1), P(R0, A1, Depth1) };
			Mesh.Poly(Mat, Face, F.N, F.Along(), FVector::UpVector, Texel);
			const float AM = (A0 + A1) * 0.5f;
			const FVector Radial = F.Along() * FMath::Cos(AM) + FVector::UpVector * FMath::Sin(AM);
			const FVector Inner[] = { P(R0, A0, Depth0), P(R0, A0, Depth1), P(R0, A1, Depth1), P(R0, A1, Depth0) };
			Mesh.PolyWorld(Mat, Inner, -Radial, Texel);
			const FVector Outer[] = { P(R1, A0, Depth0), P(R1, A1, Depth0), P(R1, A1, Depth1), P(R1, A0, Depth1) };
			Mesh.PolyWorld(Mat, Outer, Radial, Texel);
		}
	};
	const float SurroundFront = Skin + ExtSurroundProud;
	for (const FExtHole& H : Holes)
	{
		const FExtFacade& F = Facades[H.Facade];
		const bool bDoor = H.Kind == EExtHole::Door;
		const bool bArched = H.Kind == EExtHole::Arched;
		const float W = bDoor ? 22.f : (F.bTurret ? 12.f : ExtSurround);
		const float Back = bDoor ? 0.f : ExtRevealDepth;
		const float Spring = bArched ? H.Z1 - H.Radius() : H.Z1;
		// The reveals, in stone, from the brick's face back to the frame.
		WallBox(Dress, Stone, F, H.U0 - 2.f, H.U0, H.Z0, Spring, Back, Skin, ExtStone.TexelSizeCm);
		WallBox(Dress, Stone, F, H.U1, H.U1 + 2.f, H.Z0, Spring, Back, Skin, ExtStone.TexelSizeCm);
		if (bArched)
		{
			ArchBand(Dress, Stone, F, H.Mid(), Spring, H.Radius(), H.Radius() + 2.f, Back, Skin, ExtStone.TexelSizeCm);
		}
		else
		{
			WallBox(Dress, Stone, F, H.U0 - 2.f, H.U1 + 2.f, H.Z1, H.Z1 + 2.f, Back, Skin, ExtStone.TexelSizeCm);
		}
		// The architrave: jambs, and a head (or an arch with a keystone).
		WallBox(Dress, Stone, F, H.U0 - W, H.U0, H.Z0, Spring, Skin - 1.f, SurroundFront, ExtStone.TexelSizeCm);
		WallBox(Dress, Stone, F, H.U1, H.U1 + W, H.Z0, Spring, Skin - 1.f, SurroundFront, ExtStone.TexelSizeCm);
		if (bArched)
		{
			ArchBand(Dress, Stone, F, H.Mid(), Spring, H.Radius(), H.Radius() + W, Skin - 1.f, SurroundFront, ExtStone.TexelSizeCm);
			WallBox(Dress, Stone, F, H.Mid() - 9.f, H.Mid() + 9.f, H.Z1 - 4.f, H.Z1 + W + 8.f, Skin - 1.f, SurroundFront + 2.f, ExtStone.TexelSizeCm);
			// The hood mould over the arch, and its stops at the springing.
			ArchBand(Dress, Stone, F, H.Mid(), Spring, H.Radius() + W + 2.f, H.Radius() + W + 10.f, Skin - 1.f, SurroundFront + 6.f, ExtStone.TexelSizeCm);
			for (const float Side : { -1.f, 1.f })
			{
				const float U = H.Mid() + Side * (H.Radius() + W + 6.f);
				WallBox(Dress, Stone, F, U - 5.f, U + 5.f, Spring - 22.f, Spring, Skin - 1.f, SurroundFront + 6.f, ExtStone.TexelSizeCm);
			}
		}
		else
		{
			WallBox(Dress, Stone, F, H.U0 - W, H.U1 + W, H.Z1, H.Z1 + W, Skin - 1.f, SurroundFront, ExtStone.TexelSizeCm);
			if (!F.bTurret)
			{
				// A label over the head: a hood mould with its ends turned down, Gothic Revival's
				// dripstone.
				const float L0 = H.U0 - W - 8.f, L1 = H.U1 + W + 8.f;
				WallBox(Dress, Stone, F, L0, L1, H.Z1 + W + 1.f, H.Z1 + W + 10.f, Skin - 1.f, SurroundFront + 6.f, ExtStone.TexelSizeCm);
				for (const float U : { L0 + 4.f, L1 - 4.f })
				{
					WallBox(Dress, Stone, F, U - 4.f, U + 4.f, H.Z1 + W - 24.f, H.Z1 + W + 1.f, Skin - 1.f, SurroundFront + 6.f, ExtStone.TexelSizeCm);
				}
			}
		}
		if (bDoor)
		{
			// The step out of the door is the porch's; over the door, a cornice on the architrave.
			WallBox(Dress, Stone, F, H.U0 - W - 6.f, H.U1 + W + 6.f, H.Z1 + W, H.Z1 + W + 9.f, Skin - 1.f, SurroundFront + 8.f, ExtStone.TexelSizeCm);
			continue;
		}
		// The sill, sloped a little to throw the rain off.
		{
			const FVector C = F.At(H.Mid(), H.Z0 - 5.f, (Back + Skin + 9.f) * 0.5f);
			const FQuat Q = ExtOnWall(F) * FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(-4.f));
			Dress.Box(Stone, C, Q, FVector(H.U1 - H.U0 + W * 2.f + 10.f, Skin + 9.f - Back, 9.f), ExtStone.TexelSizeCm);
		}
		if (H.Kind != EExtHole::Blind)
		{
			continue;
		}

		// A blind window: a sash with dark glass behind it, and its shutters closed over it.
		WallBox(Dress, Dark, F, H.U0, H.U1, H.Z0, H.Z1, Back - 4.f, Back - 3.f, 100.f);
		WallBox(Dress, DarkGlass, F, H.U0, H.U1, H.Z0, H.Z1, Back - 2.f, Back - 1.5f, 100.f);
		for (const float U : { H.U0 + 3.f, H.Mid(), H.U1 - 3.f })
		{
			WallBox(Dress, Trim, F, U - 3.f, U + 3.f, H.Z0, H.Z1, Back - 1.5f, Back + 1.5f, ExtStone.TexelSizeCm);
		}
		for (const float Z : { H.Z0 + 3.f, (H.Z0 + H.Z1) * 0.5f, H.Z1 - 3.f })
		{
			WallBox(Dress, Trim, F, H.U0, H.U1, Z - 3.f, Z + 3.f, Back - 1.5f, Back + 1.5f, ExtStone.TexelSizeCm);
		}
		const FExtShutterModel& M = ExtShutters[H.Shutter];
		FRandomStream Leaf(H.Seed);
		const float Roll = Leaf.FRand();
		const float OpenAngle = Leaf.FRandRange(68.f, 100.f);
		for (const float Side : { -1.f, 1.f })
		{
			const bool bRight = Side > 0.f;
			float Angle = 0.f;
			if (Roll > 0.68f && Roll < 0.88f && bRight)
			{
				Angle = OpenAngle;
			}
			if (Roll >= 0.88f && !bRight)
			{
				continue; // gone
			}
			const float HingeU = bRight ? H.U1 - 2.f : H.U0 + 2.f;
			const FVector Hinge = F.At(HingeU, H.Z0, Back + 3.5f);
			const FQuat Q = ExtOnWall(F, bRight ? -Angle : Angle);
			if (UStaticMeshComponent* Mesh = Build.Prop(M.Name, Hinge, Q.Rotator(), 0.f, false))
			{
				if (bRight)
				{
					Mesh->SetRelativeScale3D(FVector(-1.f, 1.f, 1.f));
				}
				ExtDressSlots(Mesh, { { TEXT("Shutter"), ShutterPaint }, { TEXT("Iron"), Iron }, { TEXT("Shadow"), Dark } });
			}
		}
	}

	// ---- Plinth, string course, corbel table, quoins ------------------------------------------
	for (int32 i = 0; i < Facades.Num(); ++i)
	{
		const FExtFacade& F = Facades[i];
		const float Ext = F.bTurret ? 0.f : 8.f;
		auto Band = [&](float Z0, float Z1, float Proud, float Grow)
		{
			if (Z1 <= F.Z0 || Z0 >= F.Z1)
			{
				return;
			}
			for (const FBox2D& P : RoomWalls::CutAround(HolesOn(i, Grow), F.U0 - Ext, F.U1 + Ext, FMath::Max(Z0, F.Z0), FMath::Min(Z1, F.Z1)))
			{
				WallBox(Dress, Stone, F, P.Min.X, P.Max.X, P.Min.Y, P.Max.Y, Skin - 1.f, Skin + Proud, ExtStone.TexelSizeCm);
			}
		};
		Band(F.Z0, ExtPlinthTop, 3.f, ExtSurround + 2.f);
		Band(ExtPlinthTop, ExtPlinthTop + 5.f, 1.5f, ExtSurround + 2.f);
		Band(ExtStringZ0, ExtStringZ1, 5.f, ExtSurround + 2.f);
		// Under the eaves (and along the foot of a gable): a brick band and a row of corbels.
		Band(F.Z1 - 26.f, F.Z1 - 4.f, 3.f, 0.f);
		if (F.bCorbels || F.bTurret)
		{
			for (float U = F.U0 + 20.f; U < F.U1 - 10.f; U += 36.f)
			{
				WallBox(Dress, Brick, F, U - 7.f, U + 7.f, F.Z1 - 22.f, F.Z1, Skin, Skin + 9.f, ExtBrick.TexelSizeCm);
			}
		}
	}
	// Quoins at the outside corners: alternate long and short stones on each face.
	struct FExtCorner { FVector2D At; FVector A, B; float Z0, Z1; };
	const FExtCorner Corners[] = {
		{ FVector2D(WestFace, SouthFace), XN, YP, ExtWallFoot, BlockEavesZ },
		{ FVector2D(WestFace, NorthFace), XN, YN, ExtWallFoot, BlockEavesZ },
		{ FVector2D(BlockEastFace, SouthFace), XP, YP, ExtWallFoot, BlockEavesZ },
		{ FVector2D(BlockEastFace, NorthFace), XP, YN, WingEavesZ, BlockEavesZ },
		{ FVector2D(EastFace, WingSouthFace), XP, YP, ExtWallFoot, WingEavesZ },
	};
	for (const FExtCorner& C : Corners)
	{
		int32 Course = 0;
		for (float Z = C.Z0; Z < C.Z1 - 1.f; Z += 28.f, ++Course)
		{
			const float Z1 = FMath::Min(Z + 27.f, C.Z1);
			for (const bool bFirst : { true, false })
			{
				const FVector N = bFirst ? C.A : C.B;
				const FVector Other = bFirst ? C.B : C.A;
				const float Len = ((Course % 2 == 0) == bFirst) ? 42.f : 24.f;
				// From the corner's outer edge back along the face (away from the other face).
				const FVector Corner(C.At, 0.f);
				const FVector From = Corner + Other * (Skin + 3.f);
				const FVector To = Corner - Other * Len;
				const FVector Mid = (From + To) * 0.5f + N * ((Skin - 1.f + Skin + 3.f) * 0.5f);
				const FVector Size(FMath::Abs(FVector::DotProduct(From - To, FVector(1.f, 0.f, 0.f))) + FMath::Abs(N.X) * 4.f,
					FMath::Abs(FVector::DotProduct(From - To, FVector(0.f, 1.f, 0.f))) + FMath::Abs(N.Y) * 4.f, Z1 - Z);
				Dress.Box(Stone, FVector(Mid.X, Mid.Y, (Z + Z1) * 0.5f), FQuat::Identity, Size, ExtStone.TexelSizeCm);
			}
		}
	}

	// ---- Roofs ------------------------------------------------------------------------------
	auto AddRoof = [&](const FExtRoof& R)
	{
		const FVector2D D2 = (R.B - R.A).GetSafeNormal();
		const FVector D(D2, 0.f);
		const FVector P(-D2.Y, D2.X, 0.f);
		const float RidgeZ = R.EavesZ + R.HalfSpan * R.Pitch();
		const FVector R0 = FVector(R.A, RidgeZ) - D * R.RakeA;
		const FVector R1 = FVector(R.B, RidgeZ) + D * R.RakeB;
		for (const float S : { -1.f, 1.f })
		{
			const float Over = S > 0.f ? R.OverRight : R.OverLeft;
			const FVector Out = P * S * (R.HalfSpan + Over);
			const FVector Drop(0.f, 0.f, -(R.HalfSpan + Over) * R.Pitch());
			const FVector E0 = R0 + Out + Drop, E1 = R1 + Out + Drop;
			const FVector Up(0.f, 0.f, ExtRoofThick);
			const FVector N = (P * S * R.Pitch() + FVector::UpVector).GetSafeNormal();
			const FVector Top[] = { R0 + Up, R1 + Up, E1 + Up, E0 + Up };
			Roofs.PolyWorld(Slate, Top, N, ExtSlate.TexelSizeCm);
			const FVector Under[] = { R0, E0, E1, R1 };
			Roofs.PolyWorld(Trim, Under, -N, RoomSurfaces::RoughWood.TexelSizeCm);
			// The fascia along the eaves and the verge boards up the rakes.
			const FVector Fascia[] = { E0, E1, E1 + Up, E0 + Up };
			Roofs.Poly(Trim, Fascia, P * S, D, FVector::UpVector, RoomSurfaces::RoughWood.TexelSizeCm);
			const FVector VergeA[] = { R0, R0 + Up, E0 + Up, E0 };
			Roofs.PolyWorld(Trim, VergeA, -D, RoomSurfaces::RoughWood.TexelSizeCm);
			const FVector VergeB[] = { R1, E1, E1 + Up, R1 + Up };
			Roofs.PolyWorld(Trim, VergeB, D, RoomSurfaces::RoughWood.TexelSizeCm);
			// A half-round iron gutter under the fascia.
			if (Over > 1.f)
			{
				const FVector GutterMid = (E0 + E1) * 0.5f + P * S * 6.f + FVector(0.f, 0.f, 2.f);
				Build.Cyl(GutterMid, FRotationMatrix::MakeFromZ(D).Rotator(), FVector(13.f, 13.f, (E1 - E0).Size() + 4.f), Iron, false);
			}
		}
		// The ridge: a roll of tiles, a square stood on its corner.
		const FVector RidgeMid = (R0 + R1) * 0.5f + FVector(0.f, 0.f, ExtRoofThick + 2.f);
		const FQuat RidgeQ = FQuat(FRotationMatrix::MakeFromX(D)) * FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(45.f));
		Roofs.Box(Pot, RidgeMid, RidgeQ, FVector((R1 - R0).Size(), 15.f, 15.f), 100.f);
	};
	for (const FExtRoof& R : RoofList)
	{
		AddRoof(R);
	}

	// A carved bargeboard under a gable's two rakes: its top edge along the roof's underside, its
	// lower edge cusped. Apex is the top of the board's front face at the ridge.
	auto Bargeboard = [&](const FVector& Apex, const FVector& Along, const FVector& N, float Reach, float Tan, float Depth, float Period)
	{
		const float Thick = 4.f;
		for (const float Side : { -1.f, 1.f })
		{
			const float Step = 3.f;
			for (float W = 0.f; W < Reach - 0.1f; W += Step)
			{
				const float W1 = FMath::Min(W + Step, Reach);
				const FVector TopA = Apex + Along * (Side * W) - FVector(0.f, 0.f, W * Tan);
				const FVector TopB = Apex + Along * (Side * W1) - FVector(0.f, 0.f, W1 * Tan);
				const FVector BotA = TopA - FVector(0.f, 0.f, Depth - ExtScallop(W, Period, Period * 0.37f));
				const FVector BotB = TopB - FVector(0.f, 0.f, Depth - ExtScallop(W1, Period, Period * 0.37f));
				for (const float Dp : { 0.f, -Thick })
				{
					const FVector O = N * Dp;
					const FVector Pts[] = { BotA + O, BotB + O, TopB + O, TopA + O };
					Dress.Poly(Trim, Pts, Dp == 0.f ? N : -N, Along, FVector::UpVector, RoomSurfaces::RoughWood.TexelSizeCm);
				}
				const FVector Edge[] = { BotA - N * Thick, BotB - N * Thick, BotB, BotA };
				Dress.PolyWorld(Trim, Edge, -FVector::UpVector, RoomSurfaces::RoughWood.TexelSizeCm);
			}
		}
	};

	// Bargeboards on every gable, and a finial where they meet.
	for (const FExtGable& G : Gables)
	{
		const FExtFacade& F = Facades[G.Facade];
		const float Half = (G.U1 - G.U0) * 0.5f;
		const float Reach = Half + (G.Facade == WingEast ? 30.f : ExtEavesOver);
		const float RidgeZ = G.RidgeZ();
		const float Front = ExtRakeOver - 1.f, BackD = ExtRakeOver - 5.f;
		Bargeboard(F.At(G.ApexU, RidgeZ, Front), F.Along(), F.N, Reach, ExtTan, 30.f, 30.f);
		const FVector Apex = F.At(G.ApexU, RidgeZ + 2.f, (Front + BackD) * 0.5f);
		Build.Prop(TEXT("exterior_finial"), Apex, F.N.Rotation(), 0.f, false);

		if (G.bVent)
		{
			// A lancet in the gable: a blind louvred vent in a stone frame.
			const float Z0 = G.Z + (RidgeZ - G.Z) * 0.28f;
			const float H = FMath::Min(110.f, (RidgeZ - G.Z) * 0.32f);
			const float W = G.Facade == WingEast ? 22.f : 30.f;
			WallBox(Dress, Stone, F, G.ApexU - W - 10.f, G.ApexU + W + 10.f, Z0 - 10.f, Z0 + H + 10.f, Skin - 1.f, Skin + 3.f, ExtStone.TexelSizeCm);
			WallBox(Dress, Dark, F, G.ApexU - W, G.ApexU + W, Z0, Z0 + H, Skin + 3.f, Skin + 3.2f, 100.f);
			for (float Z = Z0 + 6.f; Z < Z0 + H - 4.f; Z += 9.f)
			{
				const FVector C = F.At(G.ApexU, Z, Skin + 4.5f);
				Dress.Box(Trim, C, ExtOnWall(F) * FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(35.f)), FVector(W * 2.f, 1.2f, 7.f), RoomSurfaces::RoughWood.TexelSizeCm, true);
			}
			WallBox(Dress, Stone, F, G.ApexU - W - 14.f, G.ApexU + W + 14.f, Z0 - 18.f, Z0 - 10.f, Skin - 1.f, Skin + 8.f, ExtStone.TexelSizeCm);
		}
	}

	// ---- The turret's roof -----------------------------------------------------------------
	{
		const float Over = 26.f;
		const float Ap = ExtTurretApothem + Over;
		const float TanT = FMath::Tan(FMath::DegreesToRadians(ExtTurretPitchDeg));
		const float EaveZ = TurretEavesZ - Over * TanT;
		const FVector Tip(ExtTurretCentre, TurretEavesZ + ExtTurretApothem * TanT + ExtRoofThick);
		const float HalfEdge = Ap * FMath::Tan(PI / ExtTurretSides);
		for (int32 k = 0; k < ExtTurretSides; ++k)
		{
			const float A = FMath::DegreesToRadians(TurretAngles[k]);
			const FVector N2(FMath::Cos(A), FMath::Sin(A), 0.f);
			const FVector T2(-N2.Y, N2.X, 0.f);
			const FVector Mid = FVector(ExtTurretCentre, EaveZ + ExtRoofThick) + N2 * Ap;
			const FVector L = Mid - T2 * HalfEdge, Rr = Mid + T2 * HalfEdge;
			const FVector N = (N2 * TanT + FVector::UpVector).GetSafeNormal();
			const FVector Pts[] = { L, Rr, Tip };
			Roofs.PolyWorld(Slate, Pts, N, ExtSlate.TexelSizeCm);
			const FVector Down(0.f, 0.f, -ExtRoofThick);
			const FVector Under[] = { L + Down, Tip + Down, Rr + Down };
			Roofs.PolyWorld(Trim, Under, -N, RoomSurfaces::RoughWood.TexelSizeCm);
			const FVector Fascia[] = { L + Down, Rr + Down, Rr, L };
			Roofs.Poly(Trim, Fascia, N2, T2, FVector::UpVector, RoomSurfaces::RoughWood.TexelSizeCm);
		}
		Build.Prop(TEXT("exterior_spire"), Tip - FVector(0.f, 0.f, 6.f), FRotator(0.f, AStormForestActor::MoonDirection().Rotation().Yaw, 0.f), 0.f, false);
	}

	// ---- Chimneys ---------------------------------------------------------------------------
	auto Chimney = [&](const FVector2D& Centre, const FVector2D& Size, float Z0, float Z1, int32 Pots)
	{
		const FVector C(Centre, (Z0 + Z1) * 0.5f);
		Dress.Box(Brick, C, FQuat::Identity, FVector(Size.X, Size.Y, Z1 - Z0), ExtBrick.TexelSizeCm);
		// The oversailing courses at the top, and the cap.
		Dress.Box(Brick, FVector(Centre, Z1 - 18.f), FQuat::Identity, FVector(Size.X + 10.f, Size.Y + 10.f, 9.f), ExtBrick.TexelSizeCm);
		Dress.Box(Brick, FVector(Centre, Z1 - 30.f), FQuat::Identity, FVector(Size.X + 5.f, Size.Y + 5.f, 7.f), ExtBrick.TexelSizeCm);
		Dress.Box(Stone, FVector(Centre, Z1 + 4.f), FQuat::Identity, FVector(Size.X + 8.f, Size.Y + 8.f, 8.f), ExtStone.TexelSizeCm);
		const bool bAlongX = Size.X >= Size.Y;
		for (int32 i = 0; i < Pots; ++i)
		{
			const float T = Pots == 1 ? 0.f : (i / float(Pots - 1) - 0.5f) * ((bAlongX ? Size.X : Size.Y) - 40.f);
			const FVector At(Centre.X + (bAlongX ? T : 0.f), Centre.Y + (bAlongX ? 0.f : T), Z1 + 8.f);
			if (UStaticMeshComponent* Mesh = Build.Prop(TEXT("exterior_chimney_pot"), At, FRotator(0.f, Random.FRandRange(0.f, 360.f), Random.FRandRange(-2.f, 2.f)), Random.FRandRange(62.f, 74.f), false))
			{
				ExtDressSlots(Mesh, { { TEXT("Pot"), Pot }, { TEXT("Shadow"), Dark } });
			}
		}
	};
	// The living room's, up the west front; the library's, up the west block's east face; the
	// kitchen range's, through the roof; and two on the wing's ridges.
	{
		const float Y = 1410.f;
		Dress.Box(Brick, FVector(WestFace - 30.f, Y, (ExtWallFoot + 300.f) * 0.5f), FQuat::Identity, FVector(64.f, 170.f, 300.f - ExtWallFoot), ExtBrick.TexelSizeCm);
		Dress.Box(Stone, FVector(WestFace - 34.f, Y, 300.f), FQuat(FVector(0.f, 1.f, 0.f), FMath::DegreesToRadians(-30.f)), FVector(40.f, 176.f, 12.f), ExtStone.TexelSizeCm);
		Chimney(FVector2D(WestFace - 22.f, Y), FVector2D(52.f, 130.f), 300.f, 1150.f, 2);
		Dress.Box(Brick, FVector(BlockEastFace + 30.f, Y, (ExtWallFoot + 300.f) * 0.5f), FQuat::Identity, FVector(64.f, 160.f, 300.f - ExtWallFoot), ExtBrick.TexelSizeCm);
		Dress.Box(Stone, FVector(BlockEastFace + 34.f, Y, 300.f), FQuat(FVector(0.f, 1.f, 0.f), FMath::DegreesToRadians(30.f)), FVector(40.f, 166.f, 12.f), ExtStone.TexelSizeCm);
		Chimney(FVector2D(BlockEastFace + 22.f, Y), FVector2D(52.f, 124.f), 300.f, 1150.f, 2);
		Chimney(FVector2D(-1950.f, -390.f), FVector2D(70.f, 150.f), 900.f, 1260.f, 3);
		Chimney(FVector2D(WingRidgeWestX, 200.f), FVector2D(150.f, 64.f), 860.f, 1110.f, 3);
		Chimney(FVector2D(WingRidgeEastX, 760.f), FVector2D(130.f, 60.f), 860.f, 1090.f, 2);
	}

	// ---- Downpipes --------------------------------------------------------------------------
	auto Downpipe = [&](const FVector2D& At, float Top)
	{
		const float Foot = Forest->GroundHeight(At.X, At.Y);
		Build.Cyl(FVector(At, (Top + Foot) * 0.5f), FRotator::ZeroRotator, FVector(10.f, 10.f, Top - Foot), Iron, false);
		Build.Box(FVector(At, Top + 8.f), FRotator::ZeroRotator, FVector(24.f, 24.f, 22.f), Iron, false);
		for (float Z = Foot + 120.f; Z < Top - 40.f; Z += 180.f)
		{
			Build.Cyl(FVector(At, Z), FRotator::ZeroRotator, FVector(13.f, 13.f, 4.f), Iron, false);
		}
	};
	const float Pipe = Skin + 9.f;
	Downpipe(FVector2D(EastFace + Pipe, WingSouthFace - 40.f), WingEavesZ - 50.f);
	Downpipe(FVector2D(EastFace + Pipe, NorthFace + 290.f), WingEavesZ - 50.f);
	Downpipe(FVector2D(WingValleyX, WingSouthFace + Pipe), WingEavesZ - 10.f);
	Downpipe(FVector2D(WingValleyX, NorthFace - Pipe), WingEavesZ - 10.f);
	Downpipe(FVector2D(WestFace + 40.f, SouthFace + Pipe), BlockEavesZ - 50.f);
	Downpipe(FVector2D(BlockEastFace - 40.f, SouthFace + Pipe), BlockEavesZ - 50.f);
	Downpipe(FVector2D(WestFace - Pipe, NorthFace + 40.f), BlockEavesZ - 50.f);

	// ---- The porch --------------------------------------------------------------------------
	{
		const float Front = EastFace + 244.f;
		const float Y0 = FrontDoorY - 210.f, Y1 = FrontDoorY + 210.f;
		const float Deck = GroundFloorZ;
		const float Ground = Forest->GroundHeight(Front + 60.f, FrontDoorY);
		Dress.Box(Stone, FVector((EastFace + Front) * 0.5f, FrontDoorY, (Deck + ExtWallFoot) * 0.5f), FQuat::Identity, FVector(Front - EastFace, Y1 - Y0, Deck - ExtWallFoot), ExtStone.TexelSizeCm);
		Dress.Box(Stone, FVector((EastFace + Front) * 0.5f + 2.f, FrontDoorY, Deck - 3.f), FQuat::Identity, FVector(Front - EastFace + 6.f, Y1 - Y0 + 6.f, 6.f), ExtStone.TexelSizeCm);
		// Steps down to the drive, as many as the ground needs.
		const int32 Steps = FMath::Max(1, FMath::RoundToInt((Deck - Ground) / 16.f));
		const float Rise = (Deck - Ground) / (Steps + 1);
		for (int32 i = 0; i < Steps; ++i)
		{
			const float Top = Deck - Rise * (i + 1);
			const float X0 = Front + 34.f * i, X1 = Front + 34.f * (i + 1) + 4.f;
			Dress.Box(Stone, FVector((X0 + X1) * 0.5f, FrontDoorY, (Top + ExtWallFoot) * 0.5f), FQuat::Identity, FVector(X1 - X0, 300.f, Top - ExtWallFoot), ExtStone.TexelSizeCm);
		}
		// Posts at the front corners, half posts against the wall, a beam round on them.
		const float BeamZ = Deck + 270.f;
		const float PostX = Front - 20.f;
		for (const FVector2D& Post : { FVector2D(PostX, Y0 + 20.f), FVector2D(PostX, Y1 - 20.f), FVector2D(EastFace + Skin + 4.f, Y0 + 20.f), FVector2D(EastFace + Skin + 4.f, Y1 - 20.f) })
		{
			if (UStaticMeshComponent* Mesh = Build.Prop(TEXT("exterior_porch_post"), FVector(Post, Deck), FRotator::ZeroRotator, 0.f, false))
			{
				ExtDressSlots(Mesh, { { TEXT("Trim"), Trim } });
			}
		}
		Dress.Box(Trim, FVector(PostX, FrontDoorY, BeamZ + 11.f), FQuat(FRotator(0.f, 90.f, 0.f)), FVector(Y1 - Y0, 20.f, 22.f), RoomSurfaces::RoughWood.TexelSizeCm, true);
		for (const float Y : { Y0 + 20.f, Y1 - 20.f })
		{
			Dress.Box(Trim, FVector((EastFace + PostX) * 0.5f, Y, BeamZ + 11.f), FQuat::Identity, FVector(PostX - EastFace, 18.f, 22.f), RoomSurfaces::RoughWood.TexelSizeCm, true);
		}
		struct FBracket { FVector2D At; float Yaw; };
		const FBracket Brackets[] = {
			{ FVector2D(PostX, Y0 + 30.f), 90.f }, { FVector2D(PostX, Y1 - 30.f), -90.f },
			{ FVector2D(PostX - 10.f, Y0 + 20.f), 180.f }, { FVector2D(PostX - 10.f, Y1 - 20.f), 180.f },
		};
		for (const FBracket& B : Brackets)
		{
			if (UStaticMeshComponent* Mesh = Build.Prop(TEXT("exterior_porch_bracket"), FVector(B.At, BeamZ), FRotator(0.f, B.Yaw, 0.f), 0.f, false))
			{
				ExtDressSlots(Mesh, { { TEXT("Trim"), Trim } });
			}
		}
		// The roof: a low gable from the wall to the front, its ridge under the corridor's window sill,
		// a pediment over the steps with a cusped bargeboard and a finial.
		const float RoofFront = Front + 34.f;
		const float PorchHalf = (Y1 - Y0) * 0.5f + 20.f;
		const float PorchTan = FMath::Tan(FMath::DegreesToRadians(20.f));
		const float PorchEaves = BeamZ + 2.f;
		const FExtRoof PorchRoof{ FVector2D(EastFace + Skin, FrontDoorY), FVector2D(RoofFront, FrontDoorY), PorchHalf, PorchEaves, 0.f, 0.f, 0.f, 0.f, PorchTan };
		AddRoof(PorchRoof);
		const float PorchRidge = PorchEaves + PorchHalf * PorchTan;
		ensureMsgf(PorchRidge + ExtRoofThick < 60.f, TEXT("The porch roof runs up into the corridor window's sill"));
		{
			// The tympanum: boards, set back under the rake.
			const float X = RoofFront - 10.f;
			const FVector Pts[] = { FVector(X, FrontDoorY - PorchHalf + 12.f, PorchEaves + 4.f), FVector(X, FrontDoorY + PorchHalf - 12.f, PorchEaves + 4.f), FVector(X, FrontDoorY, PorchRidge - 2.f) };
			Dress.Poly(Trim, Pts, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector::UpVector, RoomSurfaces::RoughWood.TexelSizeCm);
			// A sunburst of battens on it.
			for (int32 k = 1; k < 8; ++k)
			{
				const float A = PI * k / 8.f;
				const FVector From(X + 1.f, FrontDoorY, PorchEaves + 6.f);
				const float Len = FMath::Min((PorchHalf - 14.f) / FMath::Max(FMath::Abs(FMath::Cos(A)), 0.01f), (PorchRidge - PorchEaves - 10.f) / FMath::Max(FMath::Sin(A), 0.01f)) * 0.9f;
				const FVector To = From + FVector(0.f, FMath::Cos(A), FMath::Sin(A)) * Len;
				Dress.Box(Trim, (From + To) * 0.5f, FQuat(FRotationMatrix::MakeFromX(To - From)), FVector((To - From).Size(), 3.f, 3.f), RoomSurfaces::RoughWood.TexelSizeCm, true);
			}
			Bargeboard(FVector(RoofFront - 1.f, FrontDoorY, PorchRidge), FVector(0.f, 1.f, 0.f), FVector(1.f, 0.f, 0.f), PorchHalf, PorchTan, 20.f, 22.f);
			Build.Prop(TEXT("exterior_finial"), FVector(RoofFront - 3.f, FrontDoorY, PorchRidge + 4.f), FRotator::ZeroRotator, 120.f, false);
		}
		// A fretted valance under the front beam, between the posts.
		for (float Y = Y0 + 32.f; Y < Y1 - 32.f - 0.1f; Y += 3.f)
		{
			const float Ya = Y, Yb = FMath::Min(Y + 3.f, Y1 - 32.f);
			const float BotA = BeamZ - 16.f + ExtScallop(Ya - Y0, 24.f, 9.f), BotB = BeamZ - 16.f + ExtScallop(Yb - Y0, 24.f, 9.f);
			for (const float X : { PostX + 8.f, PostX + 5.f })
			{
				const FVector Pts[] = { FVector(X, Ya, BotA), FVector(X, Yb, BotB), FVector(X, Yb, BeamZ), FVector(X, Ya, BeamZ) };
				Dress.Poly(Trim, Pts, FVector(X > PostX + 6.f ? 1.f : -1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector::UpVector, RoomSurfaces::RoughWood.TexelSizeCm);
			}
		}

		// The door, shut, and boarded over from outside: the boards that were across it in the hall.
		// Behind it, the dark of the passage, so no crack round a leaf shows the night through the house.
		Dress.Box(Dark, FVector(EastFace - 24.f, FrontDoorY, GroundFloorZ + DoorUnitHeight * 0.5f), FQuat::Identity, FVector(4.f, DoorUnitWidth, DoorUnitHeight), 100.f);
		const FVector DoorAt(EastFace - 10.f, FrontDoorY, GroundFloorZ);
		if (UStaticMeshComponent* Door = Build.Prop(TEXT("exterior_front_door"), DoorAt, FRotator(0.f, -90.f, 0.f), 0.f, false))
		{
			DressFrontDoor(Door, this);
		}
		struct FPlank { float Z; float Roll; float Length; float Height; };
		const FPlank Planks[] = {
			{ 40.f, 5.f, 236.f, 17.f }, { 78.f, -7.f, 228.f, 15.f }, { 118.f, 3.f, 240.f, 18.f }, { 152.f, 14.f, 244.f, 15.f },
			{ 190.f, -4.f, 232.f, 16.f }, { 226.f, -11.f, 238.f, 14.f },
		};
		const FExtFacade& F = Facades[WingEast];
		for (int32 i = 0; i < UE_ARRAY_COUNT(Planks); ++i)
		{
			const FPlank& Pl = Planks[i];
			const float Dp = SurroundFront + 4.f + (i % 2) * 2.4f;
			const FVector C = F.At(-FrontDoorY + (i % 3 - 1) * 4.f, GroundFloorZ + Pl.Z, Dp);
			const FQuat Q = ExtOnWall(F) * FQuat(FVector(0.f, 1.f, 0.f), FMath::DegreesToRadians(Pl.Roll));
			Dress.Box(Boards, C, Q, FVector(Pl.Length, 2.4f, Pl.Height), RoomSurfaces::RoughWood.TexelSizeCm, true);
			for (const float Side : { -1.f, 1.f })
			{
				const FVector Nail = C + Q.GetAxisX() * Side * (Pl.Length * 0.5f - 9.f) + F.N * 1.4f;
				Build.Sph(Nail, 1.6f, Iron);
			}
		}
	}

	// ---- The ground round the house: lawn gone to rough grass, and the gravel drive -------------
	// Laid a little over the forest's own ground, which is a coarser sheet and would come up through
	// a lawn laid on it exactly: ten centimetres clears its interpolation.
	{
		const float Lift = 10.f;
		auto G = [&](float X, float Y) { return FVector(X, Y, Forest->GroundHeight(X, Y) + Lift); };
		const FVector Up = FVector::UpVector;
		const FVector UAx(1.f, 0.f, 0.f), VAx(0.f, 1.f, 0.f);

		// A ring round the house's box, out from its walls to an edge that wanders and is never
		// straight, laid in rings and spokes rather than square cells: cells cut the edge into steps.
		const FVector2D Min(WestFace, NorthFace), Max(EastFace, SouthFace);
		const FVector2D Mid = (Min + Max) * 0.5f;
		const int32 Spokes = 360;
		const int32 Rings = 12;
		auto InnerPoint = [&](float Angle)
		{
			const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			const float Tx = Dir.X != 0.f ? ((Dir.X > 0.f ? Max.X : Min.X) - Mid.X) / Dir.X : BIG_NUMBER;
			const float Ty = Dir.Y != 0.f ? ((Dir.Y > 0.f ? Max.Y : Min.Y) - Mid.Y) / Dir.Y : BIG_NUMBER;
			return Mid + Dir * FMath::Min(Tx, Ty);
		};
		auto RingPoint = [&](int32 Spoke, int32 Ring)
		{
			const float Angle = 2.f * PI * Spoke / Spokes;
			const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			const float Reach = 1750.f + 450.f * FMath::PerlinNoise1D(Spoke * 0.07f) + 90.f * FMath::PerlinNoise1D(Spoke * 0.23f + 7.f);
			const float T = float(Ring) / Rings;
			return InnerPoint(Angle) - Dir * 5.f + Dir * (Reach * T * T * 0.35f + Reach * T * 0.65f);
		};
		for (int32 S = 0; S < Spokes; ++S)
		{
			for (int32 R = 0; R < Rings; ++R)
			{
				const FVector2D A = RingPoint(S, R), B = RingPoint((S + 1) % Spokes, R), C = RingPoint((S + 1) % Spokes, R + 1), D = RingPoint(S, R + 1);
				const FVector Pts[] = { G(A.X, A.Y), G(B.X, B.Y), G(C.X, C.Y), G(D.X, D.Y) };
				GroundSheet.Poly(Lawn, Pts, Up, UAx, VAx, ExtLawn.TexelSizeCm);
			}
		}
		// Inside the box but outside the walls: the corner south of the wing, east of the west block.
		const float Step = 100.f;
		for (float X = BlockEastFace; X < EastFace - 0.1f; X += Step)
		{
			for (float Y = WingSouthFace; Y < SouthFace - 0.1f; Y += Step)
			{
				const float X1 = FMath::Min(X + Step, EastFace), Y1 = FMath::Min(Y + Step, SouthFace);
				const FVector Pts[] = { G(X, Y), G(X1, Y), G(X1, Y1), G(X, Y1) };
				GroundSheet.Poly(Lawn, Pts, Up, UAx, VAx, ExtLawn.TexelSizeCm);
			}
		}

		// The drive: from the porch steps out east along the ride towards the moon, its edges
		// wandering where the grass has come in over the gravel. A strip, so its edges are smooth.
		const float DriveX0 = EastFace + 244.f;
		const float DriveX1 = EastFace + 2900.f;
		for (float X = DriveX0; X < DriveX1 - 0.1f; X += 50.f)
		{
			const float X1 = FMath::Min(X + 50.f, DriveX1);
			auto Edge = [&](float At, float Side)
			{
				const float Half = 185.f + 18.f * FMath::PerlinNoise1D(At / 260.f + Side * 3.1f) + FMath::Max(0.f, 220.f - (At - DriveX0)) * 0.35f;
				return FrontDoorY + Side * Half + 20.f * FMath::PerlinNoise1D(At / 900.f);
			};
			const FVector Pts[] = { G(X, Edge(X, -1.f)) + Up * 1.5f, G(X1, Edge(X1, -1.f)) + Up * 1.5f, G(X1, Edge(X1, 1.f)) + Up * 1.5f, G(X, Edge(X, 1.f)) + Up * 1.5f };
			GroundSheet.Poly(Gravel, Pts, Up, UAx, VAx, ExtGravel.TexelSizeCm);
		}
	}

	Walls.Commit(this, ExteriorRoot, TEXT("Walls"));
	Dress.Commit(this, ExteriorRoot, TEXT("Dressings"));
	Roofs.Commit(this, ExteriorRoot, TEXT("Roofs"));
	UProceduralMeshComponent* GroundMesh = GroundSheet.Commit(this, ExteriorRoot, TEXT("Ground"));

	// ---- Ivy --------------------------------------------------------------------------------
	{
		UMaterialInterface* IvyParent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_exterior_ivy.MI_exterior_ivy"));
		UStaticMesh* Plane = FRoomShapes::Plane();
		if (IvyParent && Plane)
		{
			UInstancedStaticMeshComponent* Cards[4] = {};
			for (int32 c = 0; c < 4; ++c)
			{
				UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(IvyParent, this);
				Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.34f, 0.36f, 0.32f));
				Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(0.5f, 0.5f, 0.f, 1.f));
				Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor((c % 2) * 0.5f, (c / 2) * 0.5f, 0.f, 1.f));
				Materials.Add(Mat);
				Cards[c] = Build.Instances(Plane, Mat);
				Cards[c]->SetReceivesDecals(false);
			}
			// A patch climbs from its foot, widening as it goes, its outline torn by noise.
			struct FIvy { int32 Facade; float U; float Z0; float Z1; float Half0; float Half1; };
			TArray<FIvy> Patches = {
				{ WingEast, -WingSouthFace + 40.f, ExtWallFoot, 300.f, 60.f, 150.f },
				{ WingSouth, EastFace - 50.f, ExtWallFoot, 250.f, 60.f, 120.f },
				{ WingEast, -FrontDoorY + 300.f, ExtWallFoot, 40.f, 40.f, 90.f },
				{ WingEast, -FrontDoorY - 300.f, ExtWallFoot, 30.f, 35.f, 80.f },
				{ BlockSouth, WestFace + 70.f, ExtWallFoot, 520.f, 70.f, 150.f },
				{ BlockWest, SouthFace - 70.f, ExtWallFoot, 520.f, 70.f, 190.f },
				{ BlockNorth, -WestFace - 80.f, ExtWallFoot, 420.f, 60.f, 140.f },
				{ BlockEastLow, -SouthFace + 60.f, ExtWallFoot, 420.f, 60.f, 130.f },
				{ WingSouth, -1090.f, ExtWallFoot, 380.f, 90.f, 200.f },
				{ WingNorth, 850.f, ExtWallFoot, 160.f, 70.f, 130.f },
			};
			for (int32 k = 0; k < ExtTurretSides; ++k)
			{
				const float A = TurretAngles[k];
				if (A > -110.f && A < 20.f)
				{
					const float Top = 640.f - FMath::Abs(A + 45.f) * 7.f;
					const int32 Index = FlatFacades + k;
					Patches.Add({ Index, (Facades[Index].U0 + Facades[Index].U1) * 0.5f, ExtWallFoot, Top, 70.f, 70.f });
				}
			}
			for (const FIvy& P : Patches)
			{
				const FExtFacade& F = Facades[P.Facade];
				const TArray<FBox2D> Glass = [&]()
				{
					TArray<FBox2D> Out;
					for (const FExtHole& H : Holes)
					{
						if (H.Facade == P.Facade && H.Kind != EExtHole::Blind)
						{
							Out.Add(FBox2D(FVector2D(H.U0 + 6.f, H.Z0 - 4.f), FVector2D(H.U1 - 6.f, H.Z1 + 10.f)));
						}
					}
					return Out;
				}();
				const float Area = (P.Z1 - P.Z0) * (P.Half0 + P.Half1);
				const int32 Count = FMath::RoundToInt(Area / 1300.f);
				for (int32 i = 0; i < Count; ++i)
				{
					const float T = FMath::Pow(Random.FRand(), 0.8f);
					const float Z = FMath::Lerp(P.Z0, P.Z1, T);
					const float Half = FMath::Lerp(P.Half0, P.Half1, T);
					const float U = P.U + Random.FRandRange(-1.f, 1.f) * Half;
					const float Edge = FMath::Abs(U - P.U) / Half;
					const float Spin = Random.FRandRange(-28.f, 28.f);
					const float Out = Random.FRandRange(2.f, 13.f);
					const float Size = Random.FRandRange(0.75f, 1.25f);
					const int32 Kind = Random.RandRange(0, 1);
					const float Tilt = Random.FRandRange(-12.f, 12.f);
					const float Ground = Forest->GroundHeight(F.At(U, 0.f, 0.f).X, F.At(U, 0.f, 0.f).Y);
					// The outline: thinning towards the edge and the top, noisy.
					const float Keep = 1.f - FMath::SmoothStep(0.55f, 1.f, Edge + 0.35f * FMath::PerlinNoise2D(FVector2D(U, Z) / 90.f)) - FMath::SmoothStep(0.85f, 1.f, T) * 0.6f;
					if (Keep < 0.25f || Z < Ground - 5.f || U < F.U0 - 10.f || U > F.U1 + 10.f || Z > F.Z1 - 10.f)
					{
						continue;
					}
					bool bOnGlass = false;
					for (const FBox2D& G : Glass)
					{
						bOnGlass |= G.IsInside(FVector2D(U, Z));
					}
					if (bOnGlass)
					{
						continue;
					}
					const bool bEdge = Edge > 0.65f || T > 0.8f;
					UInstancedStaticMeshComponent* Ism = Cards[(bEdge ? 2 : 0) + Kind];
					const FVector Right = F.Along().RotateAngleAxis(Spin, F.N);
					const FVector Down = (-FVector::UpVector).RotateAngleAxis(Spin, F.N);
					FVector Normal = F.N.RotateAngleAxis(Tilt, Right);
					const FMatrix M(Right, FVector::CrossProduct(Normal, Right), Normal, FVector::ZeroVector);
					const FVector At = F.At(U, Z, Skin + Out);
					Ism->AddInstance(FTransform(FQuat(M), At, FVector(ExtIvyCard / 100.f * Size)), /*bWorldSpace*/ true);
				}
			}
		}
	}

	// ---- Age: the dirt a wet century leaves on a house --------------------------------------
	// Projected (M_RoomDecal), so it has no edge: water run black down the brick from every sill and
	// from under the eaves, and green damp up the plinth from the ground.
	{
		const FLinearColor Streak(0.36f, 0.38f, 0.30f);
		const FLinearColor Algae(0.30f, 0.55f, 0.32f);
		auto WallStain = [&](const FExtFacade& F, float U, float Z, float W, float H, const FLinearColor& Tint, float Opacity)
		{
			Build.Stain(RoomSurfaces::Damp, F.At(U, Z, Skin + 3.f), (-F.N).Rotation(), FVector2D(W, H), Tint, Opacity, 0.9f);
		};
		for (const FExtHole& H : Holes)
		{
			if (H.Kind == EExtHole::Door)
			{
				continue;
			}
			const FExtFacade& F = Facades[H.Facade];
			const float Width = H.U1 - H.U0;
			const float Drop = Random.FRandRange(70.f, 150.f);
			const float Opacity = Random.FRandRange(0.45f, 0.75f);
			const float Shift = Random.FRandRange(-0.15f, 0.15f) * Width;
			WallStain(F, H.Mid() + Shift, H.Z0 - 12.f - Drop * 0.5f, Width * 0.8f, Drop, Streak, Opacity);
		}
		for (int32 i = 0; i < FlatFacades; ++i)
		{
			const FExtFacade& F = Facades[i];
			for (float U = F.U0 + 60.f; U < F.U1 - 40.f; U += 220.f)
			{
				const float W = Random.FRandRange(160.f, 300.f);
				const float H = Random.FRandRange(60.f, 140.f);
				const float Opacity = Random.FRandRange(0.4f, 0.7f);
				const float Jitter = Random.FRandRange(-40.f, 40.f);
				if (F.Z1 - F.Z0 > 200.f)
				{
					WallStain(F, U + Jitter, F.Z1 - 26.f - H * 0.5f, W, H, Streak, Opacity);
				}
				const float AW = Random.FRandRange(180.f, 320.f);
				const float AH = Random.FRandRange(50.f, 110.f);
				const float AO = Random.FRandRange(0.4f, 0.75f);
				if (F.Z0 < ExtPlinthTop)
				{
					const float Ground = Forest->GroundHeight(F.At(U, 0.f, 0.f).X, F.At(U, 0.f, 0.f).Y);
					WallStain(F, U - Jitter, Ground + AH * 0.4f, AW, AH, Algae, AO);
				}
			}
		}
	}

	// Everything out here: lit by the moon as well as the storm, and touching nothing.
	TArray<UPrimitiveComponent*> Parts;
	GetComponents<UPrimitiveComponent>(Parts);
	for (UPrimitiveComponent* Part : Parts)
	{
		Part->SetLightingChannels(true, true, false);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->bAffectDistanceFieldLighting = false;
	}
	// And every window's sash, glass and curtains, or from outside each one is an empty black hole.
	// Not the forest: under the moon its trees and its ground came out white, a wood under snow.
	for (TActorIterator<AStormWindowActor> It(GetWorld()); It; ++It)
	{
		TArray<UPrimitiveComponent*> WindowParts;
		It->GetComponents<UPrimitiveComponent>(WindowParts);
		for (UPrimitiveComponent* Part : WindowParts)
		{
			if (!Part->IsA<UInstancedStaticMeshComponent>())
			{
				Part->SetLightingChannels(true, true, false);
			}
		}
	}
	// The ground stays off the moon's channel, like the forest's: moonlit, the open ground came out
	// pale as snow against the wood round it, and the lawn's edge showed as a line.
	if (GroundMesh)
	{
		GroundMesh->SetLightingChannels(true, false, false);
	}
	Moonlight->SetVisibility(false);
	SkyFill->SetVisibility(false);
}
