#include "StormForestActor.h"
#include "StormWindowActor.h"
#include "RoomBuildLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	// How far out everything stands, from the middle of the house.
	const float ForestSkyRadius = 14000.f;
	const float ForestWallRadius[2] = { 7400.f, 9600.f };
	const float ForestWallHeight[2] = { 2000.f, 2600.f };
	const float ForestGroundReach = 11000.f;

	// The open ground between the walls and the first trees, and where the wood is at full density.
	const float ForestLawn = 1500.f;
	const float ForestThick = 3600.f;

	// The night air's own grey, which a tree takes on with distance (M_RoomFoliage: Haze), and how
	// many times over a strike brightens it.
	const FLinearColor ForestHaze(0.040f, 0.052f, 0.080f);
	const float ForestHazeFlash = 7.f;
	// How far off a tree has taken on all of it. Short: from the ground floor nothing but trees is
	// seen through a window, and at the honest distance they were one black sheet.
	const float ForestHazeDistance = 4500.f;
	const float ForestTreeReach = 7000.f;

	/** A tree model and how often it is planted: among conifers, among broadleaves. */
	struct FForestTree
	{
		const TCHAR* Name;
		float Conifer;
		float Broadleaf;
		/** Trunks closer together than this do not happen: roughly the crown's radius. */
		float Room;
	};

	// Tools/make_forest.py: TREES.
	const FForestTree ForestTrees[] = {
		{ TEXT("forest_spruce_a"), 0.30f, 0.05f, 300.f },
		{ TEXT("forest_spruce_b"), 0.28f, 0.06f, 300.f },
		{ TEXT("forest_spruce_c"), 0.14f, 0.07f, 220.f },
		{ TEXT("forest_pine_a"), 0.12f, 0.08f, 330.f },
		{ TEXT("forest_pine_b"), 0.10f, 0.08f, 330.f },
		{ TEXT("forest_oak_a"), 0.02f, 0.26f, 520.f },
		{ TEXT("forest_oak_b"), 0.02f, 0.18f, 480.f },
		{ TEXT("forest_beech_a"), 0.02f, 0.22f, 420.f },
	};

	/** Nothing outside may shade the storm's light or be traced by anything indoors. */
	void ForestQuiet(UPrimitiveComponent* Part)
	{
		if (!Part)
		{
			return;
		}
		Part->SetCastShadow(false);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetReceivesDecals(false);
		Part->bAffectDynamicIndirectLighting = false;
		Part->bAffectDistanceFieldLighting = false;
		Part->bVisibleInRayTracing = false;
	}

	UProceduralMeshComponent* ForestSheet(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
	{
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), Name));
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		ForestQuiet(Mesh);
		return Mesh;
	}
}

AStormForestActor::AStormForestActor()
{
	PrimaryActorTick.bCanEverTick = false;

	ForestRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ForestRoot"));
	SetRootComponent(ForestRoot);
	ForestRoot->SetMobility(EComponentMobility::Movable);
}

FVector AStormForestActor::MoonDirection()
{
	// Low enough to be seen through a window from across a room, high enough to clear the far wood.
	return FRotator(17.f, 13.f, 0.f).Vector();
}

float AStormForestActor::DistanceToHouse(float X, float Y) const
{
	const float Dx = FMath::Max3(static_cast<float>(House.Min.X) - X, 0.f, X - static_cast<float>(House.Max.X));
	const float Dy = FMath::Max3(static_cast<float>(House.Min.Y) - Y, 0.f, Y - static_cast<float>(House.Max.Y));
	return FMath::Sqrt(Dx * Dx + Dy * Dy);
}

float AStormForestActor::GroundHeight(float X, float Y) const
{
	float Height = GroundZ;
	float Bank = 0.f;
	if (bBank)
	{
		// West of the house the ground is a bank, up to under the stair window: the cellar runs out
		// beneath it, with its ceiling over the level of the ground everywhere else. The bank lies
		// full height against the whole west wall, comes down to the east in the corner north of
		// the hall, and at the south end is drawn back to the west before it comes down, so that
		// what is in front of the living room's windows is level ground. That last stretch is
		// beside the living room's west wall, four metres short of the south front: the cellar
		// stops well north of it (the wine cellar's south wall is eight metres from that front).
		const float SouthFront = static_cast<float>(House.Max.Y);
		const float South = FMath::SmoothStep(SouthFront - 400.f, SouthFront, Y);
		const float Edge = FMath::Lerp(BankX + 250.f, BankX - 100.f, South);
		Bank = 1.f - FMath::SmoothStep(Edge - 300.f, Edge, X);
		Height += (BankZ - GroundZ) * Bank;
	}
	// Away from the walls the ground rolls; against them it is as level as a house needs. The bank
	// does not roll at all: the slab that keeps the storm out of the cellar lies a hand under it
	// (BuildGround), and has the cellar's vault a hand under that.
	const float Away = DistanceToHouse(X, Y);
	float Roll = FMath::PerlinNoise2D(FVector2D(X, Y) / 2600.f) * 150.f * FMath::SmoothStep(1200.f, 5000.f, Away);
	Roll += FMath::PerlinNoise2D(FVector2D(X + 913.f, Y - 377.f) / 420.f) * 14.f * FMath::SmoothStep(150.f, 900.f, Away);
	return Height + Roll * (1.f - Bank);
}

void AStormForestActor::Build()
{
	bBuilt = true;

	// The house is wherever its storm windows are: there is one in every outside wall, so the box
	// round them is the house's footprint out to the middle of its walls.
	float LowestWindow = TNumericLimits<float>::Max();
	float WestWindowZ = TNumericLimits<float>::Max();
	bool bLead = false;
	for (TActorIterator<AStormWindowActor> It(GetWorld()); It; ++It)
	{
		const FVector3f At(It->GetActorLocation());
		const FVector Out = It->GetActorForwardVector();
		House += FVector2D(At.X, At.Y);
		LowestWindow = FMath::Min(LowestWindow, At.Z);
		if (Out.X < -0.7f)
		{
			bBank = true;
			BankX = At.X;
			WestWindowZ = FMath::Min(WestWindowZ, At.Z);
		}
		if (It->GetOwner() == GetOwner() || *It == GetOwner())
		{
			LeadWindow = FVector(At);
			bLead = true;
		}
	}
	if (!House.bIsValid)
	{
		return;
	}
	if (!bLead)
	{
		LeadWindow = FVector(House.Max.X, House.GetCenter().Y, LowestWindow);
	}
	House = House.ExpandBy(12.f);
	HouseCentre = House.GetCenter();
	GroundZ = LowestWindow - GroundDrop;
	BankZ = WestWindowZ - GroundDrop;
	BankX -= 12.f;
	// A west window on the ground floor's own level is no bank.
	bBank = bBank && BankZ > GroundZ + 1.f;

	BuildGround();
	BuildTrees();
	BuildFarWall();
	BuildSky();
}

void AStormForestActor::BuildGround()
{
	FRoomBuilder Build(this, ForestRoot);
	UMaterialInstanceDynamic* Earth = Build.Flat(FLinearColor(0.017f, 0.021f, 0.013f), 1.f);
	// The sheet wears the forest's material, for its haze: every vertex samples one point of the
	// atlas's bark, which makes it a flat colour that fades into the night air as the trees do.
	UMaterialInterface* FoliageBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomFoliage.M_RoomFoliage"));
	if (FoliageBase)
	{
		GroundMaterial = UMaterialInstanceDynamic::Create(FoliageBase, this);
		GroundMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.16f, 0.24f, 0.13f));
		GroundMaterial->SetVectorParameterValue(TEXT("Wind"), FLinearColor(0.f, 0.f, 0.f));
		GroundMaterial->SetScalarParameterValue(TEXT("Flutter"), 0.f);
		GroundMaterial->SetScalarParameterValue(TEXT("HazeDistance"), ForestHazeDistance);
	}

	// Grid lines every three and a half metres, and one along each wall of the house, so the cells
	// under the house can be left out exactly and nothing of the ground stands inside a room.
	auto Lines = [](float Centre, float Low, float High)
	{
		TArray<float> Out;
		const float Step = 350.f;
		for (float V = Centre - ForestGroundReach; V <= Centre + ForestGroundReach + 1.f; V += Step)
		{
			if (FMath::Abs(V - Low) > 40.f && FMath::Abs(V - High) > 40.f)
			{
				Out.Add(V);
			}
		}
		Out.Add(Low);
		Out.Add(High);
		Out.Sort();
		return Out;
	};
	const TArray<float> Xs = Lines(HouseCentre.X, House.Min.X, House.Max.X);
	const TArray<float> Ys = Lines(HouseCentre.Y, House.Min.Y, House.Max.Y);

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Triangles;
	Vertices.Reserve(Xs.Num() * Ys.Num());
	for (const float Y : Ys)
	{
		for (const float X : Xs)
		{
			Vertices.Add(FVector(X, Y, GroundHeight(X, Y)));
			const float Dx = GroundHeight(X + 50.f, Y) - GroundHeight(X - 50.f, Y);
			const float Dy = GroundHeight(X, Y + 50.f) - GroundHeight(X, Y - 50.f);
			Normals.Add(FVector(-Dx, -Dy, 100.f).GetSafeNormal());
			UVs.Add(FVector2D(0.75f, 0.875f));
		}
	}
	for (int32 j = 0; j + 1 < Ys.Num(); ++j)
	{
		for (int32 i = 0; i + 1 < Xs.Num(); ++i)
		{
			const FVector2D Middle((Xs[i] + Xs[i + 1]) * 0.5f, (Ys[j] + Ys[j + 1]) * 0.5f);
			if (House.IsInside(Middle))
			{
				continue;
			}
			const int32 A = j * Xs.Num() + i;
			const int32 B = A + 1;
			const int32 C = A + Xs.Num();
			const int32 D = C + 1;
			Triangles.Append({ A, C, B, B, C, D });
		}
	}
	UProceduralMeshComponent* Ground = ForestSheet(this, ForestRoot, TEXT("Ground"));
	Ground->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision*/ false);
	Ground->SetMaterial(0, GroundMaterial ? static_cast<UMaterialInterface*>(GroundMaterial) : Earth);
	Ground->RegisterComponent();
	AddInstanceComponent(Ground);

	if (bBank)
	{
		// The one thing out here that casts a shadow. The cellar runs out west under the bank, and
		// without solid earth over it the storm's shadowed lights came down through the ground and
		// lit the cellar corridor from above at every strike (the 10-05 note). A generated sheet
		// is a surface, not earth; this is the earth, lying just under it where the bank is whole.
		const float East = BankX - 8.f;
		const float West = HouseCentre.X - ForestGroundReach;
		const float South = House.Max.Y - 420.f;
		const float North = HouseCentre.Y - ForestGroundReach;
		// Thin, and no lower than the old ground's underside (twenty under the bank's top): the wine
		// cellar's vault was built up to that.
		if (UStaticMeshComponent* Slab = Build.Box(FVector((East + West) * 0.5f, (North + South) * 0.5f, BankZ - 10.f), FRotator::ZeroRotator,
			FVector(East - West, South - North, 15.f), Earth, /*bBlockingCollision*/ false))
		{
			Slab->SetCastShadow(true);
			Slab->SetReceivesDecals(false);
		}
	}
}

void AStormForestActor::BuildTrees()
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomFoliage.M_RoomFoliage"));
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("M_RoomFoliage not found: run Tools/build_art.py -ArtStage=storm"));
		return;
	}
	// The atlas is painted at daylight values; a wood at night, under cloud, is a few per cent.
	TreeMaterial = UMaterialInstanceDynamic::Create(Base, this);
	TreeMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.20f, 0.23f, 0.21f));
	TreeMaterial->SetScalarParameterValue(TEXT("HazeDistance"), ForestHazeDistance);

	FRoomBuilder Build(this, ForestRoot);
	TArray<UInstancedStaticMeshComponent*> Kinds;
	for (const FForestTree& Tree : ForestTrees)
	{
		UInstancedStaticMeshComponent* Kind = Build.Instances(FRoomShapes::Prop(Tree.Name), TreeMaterial);
		ForestQuiet(Kind);
		Kinds.Add(Kind);
	}

	FRandomStream Random(18840926);
	const FVector Moon = MoonDirection();
	const FVector2D MoonWay = FVector2D(Moon.X, Moon.Y).GetSafeNormal();

	struct FPlanted
	{
		FVector2D At;
		float Room;
	};
	TArray<FPlanted> Planted;
	const int32 Wanted = 250;
	for (int32 Try = 0; Try < 6000 && Planted.Num() < Wanted; ++Try)
	{
		// Every number is drawn before any test, so a rejected place does not shift the next.
		const float X = HouseCentre.X + Random.FRandRange(-1.f, 1.f) * (House.GetExtent().X + ForestTreeReach);
		const float Y = HouseCentre.Y + Random.FRandRange(-1.f, 1.f) * (House.GetExtent().Y + ForestTreeReach);
		const float Chance = Random.FRand();
		const float Pick = Random.FRand();
		const float Scale = Random.FRandRange(0.82f, 1.28f);
		const float Yaw = Random.FRandRange(0.f, 360.f);
		const float Lean = Random.FRandRange(-2.5f, 2.5f);

		const float Away = DistanceToHouse(X, Y);
		if (Away < ForestLawn || Away > ForestTreeReach)
		{
			continue;
		}
		// A few trees stand out on the open ground; the wood proper begins further back.
		if (Chance > 0.04f + 0.96f * FMath::SmoothStep(ForestLawn, ForestThick, Away))
		{
			continue;
		}
		// A ride cut through the wood, from the bedroom window towards where the moon stands: the
		// drive up to the house, and the reason the moon is seen from the room he wakes in.
		const FVector2D FromWindow = FVector2D(X, Y) - FVector2D(LeadWindow.X, LeadWindow.Y);
		const float Along = FVector2D::DotProduct(FromWindow, MoonWay);
		const float Aside = FMath::Abs(FVector2D::CrossProduct(FromWindow, MoonWay));
		if (Along > 0.f && Aside < 500.f + Along * 0.2f)
		{
			continue;
		}

		// Stands of one kind: conifers here, broadleaves there, mixed between.
		const float Stand = FMath::PerlinNoise2D(FVector2D(X, Y) / 2100.f);
		const float Conifer = FMath::Clamp(Stand * 2.4f + 0.55f, 0.f, 1.f);
		float Total = 0.f;
		for (const FForestTree& Tree : ForestTrees)
		{
			Total += FMath::Lerp(Tree.Broadleaf, Tree.Conifer, Conifer);
		}
		int32 Index = 0;
		float Running = 0.f;
		for (; Index < UE_ARRAY_COUNT(ForestTrees) - 1; ++Index)
		{
			Running += FMath::Lerp(ForestTrees[Index].Broadleaf, ForestTrees[Index].Conifer, Conifer) / Total;
			if (Pick < Running)
			{
				break;
			}
		}

		const float Room = ForestTrees[Index].Room * Scale;
		bool bCrowded = false;
		for (const FPlanted& Other : Planted)
		{
			if (FVector2D::DistSquared(Other.At, FVector2D(X, Y)) < FMath::Square((Room + Other.Room) * 0.5f))
			{
				bCrowded = true;
				break;
			}
		}
		if (bCrowded || !Kinds[Index])
		{
			continue;
		}
		Planted.Add({ FVector2D(X, Y), Room });
		// Rooted a hand's depth in: a trunk that ends on the grass is a pole stood on a lawn.
		Kinds[Index]->AddInstance(FTransform(FRotator(Lean, Yaw, Lean * 0.6f), FVector(X, Y, GroundHeight(X, Y) - 12.f), FVector(Scale)), /*bWorldSpace*/ true);
	}
}

void AStormForestActor::BuildFarWall()
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomFoliage.M_RoomFoliage"));
	UTexture2D* Strip = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/T_forest_ring.T_forest_ring"));
	if (!Base || !Strip)
	{
		return;
	}
	// Two rings of painted wood behind the modelled trees: the nearer one darker against the
	// further, taller one, which stands on the rising ground behind.
	for (int32 Ring = 0; Ring < 2; ++Ring)
	{
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Base, this);
		Mat->SetTextureParameterValue(TEXT("FoliageMap"), Strip);
		Mat->SetVectorParameterValue(TEXT("Tint"), Ring == 0 ? FLinearColor(0.20f, 0.23f, 0.21f) : FLinearColor(0.15f, 0.18f, 0.18f));
		// Painted trees do not bend: no wind on a wall.
		Mat->SetVectorParameterValue(TEXT("Wind"), FLinearColor(0.f, 0.f, 0.f));
		Mat->SetScalarParameterValue(TEXT("Flutter"), 0.f);
		Mat->SetScalarParameterValue(TEXT("HazeDistance"), ForestHazeDistance);
		WallMaterials.Add(Mat);

		const int32 Segments = 96;
		const float Repeats = Ring == 0 ? 3.f : 4.f;
		const float Radius = ForestWallRadius[Ring];
		const float Foot = GroundZ - 60.f + Ring * 250.f;
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<int32> Triangles;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float Angle = 2.f * PI * i / Segments;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
			const FVector At = FVector(HouseCentre.X, HouseCentre.Y, Foot) + Out * Radius;
			const float U = Repeats * i / Segments + Ring * 0.37f;
			Vertices.Add(At);
			Vertices.Add(At + FVector(0.f, 0.f, ForestWallHeight[Ring]));
			Normals.Add(-Out);
			Normals.Add(-Out);
			UVs.Add(FVector2D(U, 1.f));
			UVs.Add(FVector2D(U, 0.f));
			if (i > 0)
			{
				const int32 A = (i - 1) * 2;
				Triangles.Append({ A, A + 1, A + 2, A + 2, A + 1, A + 3 });
			}
		}
		UProceduralMeshComponent* Wall = ForestSheet(this, ForestRoot, TEXT("FarWall"));
		Wall->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision*/ false);
		Wall->SetMaterial(0, Mat);
		Wall->RegisterComponent();
		AddInstanceComponent(Wall);
	}
}

void AStormForestActor::BuildSky()
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoomSky.M_RoomSky"));
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("M_RoomSky not found: run Tools/build_art.py -ArtStage=storm"));
		return;
	}
	const FVector Centre(HouseCentre.X, HouseCentre.Y, GroundZ);
	SkyMaterial = UMaterialInstanceDynamic::Create(Base, this);
	SkyMaterial->SetVectorParameterValue(TEXT("Centre"), FLinearColor(Centre));
	SkyMaterial->SetVectorParameterValue(TEXT("MoonDir"), FLinearColor(MoonDirection()));
	// The fog between here and the dome takes about half of it; this is what gets it back.
	SkyMaterial->SetScalarParameterValue(TEXT("Brightness"), 4.f);

	// A dome from just under the horizon to the zenith. The material works from the direction of
	// each pixel, not from these UVs, so the mesh only has to be round enough not to show.
	const int32 Around = 72;
	const int32 Up = 18;
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Triangles;
	for (int32 j = 0; j <= Up; ++j)
	{
		const float Elevation = FMath::DegreesToRadians(FMath::Lerp(-9.f, 90.f, j / static_cast<float>(Up)));
		for (int32 i = 0; i <= Around; ++i)
		{
			const float Angle = 2.f * PI * i / Around;
			const FVector Out(FMath::Cos(Elevation) * FMath::Cos(Angle), FMath::Cos(Elevation) * FMath::Sin(Angle), FMath::Sin(Elevation));
			Vertices.Add(Centre + Out * ForestSkyRadius);
			Normals.Add(-Out);
			UVs.Add(FVector2D(i / static_cast<float>(Around), 1.f - j / static_cast<float>(Up)));
			if (i > 0 && j > 0)
			{
				const int32 D = j * (Around + 1) + i;
				const int32 C = D - 1;
				const int32 B = D - (Around + 1);
				const int32 A = B - 1;
				Triangles.Append({ A, C, B, B, C, D });
			}
		}
	}
	UProceduralMeshComponent* Sky = ForestSheet(this, ForestRoot, TEXT("Sky"));
	Sky->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision*/ false);
	Sky->SetMaterial(0, SkyMaterial);
	Sky->RegisterComponent();
	AddInstanceComponent(Sky);
}

void AStormForestActor::PlaceStrike(FRandomStream& Random, bool bGroundStrike, float ForcedYaw)
{
	// Drawn whether or not there is a sky to show it on: the storm's stream must not depend on the art.
	const bool bNearLead = Random.FRand() < 0.55f;
	const float Side = Random.FRandRange(-55.f, 55.f);
	const float Anywhere = Random.FRandRange(-180.f, 180.f);
	const float Elevation = Random.FRandRange(14.f, 30.f);
	const int32 Cell = bGroundStrike ? Random.RandRange(0, 2) : 3;
	const float Flip = Random.FRand() < 0.5f ? -1.f : 1.f;
	const float Crawl = Random.FRandRange(0.24f, 0.38f);
	if (!bBuilt)
	{
		Build();
	}
	if (!SkyMaterial)
	{
		return;
	}
	const float Yaw = FMath::IsNaN(ForcedYaw) ? (bNearLead ? Side : Anywhere) : ForcedYaw;
	const FVector Direction = FRotator(Elevation, Yaw, 0.f).Vector();
	SkyMaterial->SetVectorParameterValue(TEXT("FlashDir"), FLinearColor(Direction));
	UE_LOG(LogTemp, Verbose, TEXT("Storm strike: yaw %.1f elevation %.1f cell %d ground %d"), Yaw, Elevation, Cell, bGroundStrike ? 1 : 0);
	// A ground strike's picture runs from the strike down three halves of BoltSize (its origin is a
	// quarter of the way down the cell): sized so its foot is just under the horizon.
	SkyMaterial->SetScalarParameterValue(TEXT("BoltSize"), bGroundStrike ? FMath::Sin(FMath::DegreesToRadians(Elevation + 4.f)) / 1.5f : Crawl);
	SkyMaterial->SetScalarParameterValue(TEXT("BoltFlip"), Flip);
	SkyMaterial->SetVectorParameterValue(TEXT("BoltCell"), FLinearColor((Cell % 2) * 0.5f, (Cell / 2) * 0.5f, 0.f));
}

void AStormForestActor::UpdateStorm(float Flash, bool bChannel, float Gust)
{
	if (!bBuilt)
	{
		Build();
	}
	if (SkyMaterial)
	{
		if (!FMath::IsNearlyEqual(Flash, LastFlash, 0.002f))
		{
			SkyMaterial->SetScalarParameterValue(TEXT("Flash"), Flash);
			const FLinearColor Haze = ForestHaze * (1.f + Flash * ForestHazeFlash);
			if (TreeMaterial)
			{
				TreeMaterial->SetVectorParameterValue(TEXT("Haze"), Haze);
			}
			for (UMaterialInstanceDynamic* Wall : WallMaterials)
			{
				Wall->SetVectorParameterValue(TEXT("Haze"), Haze);
			}
			if (GroundMaterial)
			{
				GroundMaterial->SetVectorParameterValue(TEXT("Haze"), Haze);
			}
			LastFlash = Flash;
		}
		const float Channel = bChannel ? 1.f : 0.f;
		if (Channel != LastChannel)
		{
			SkyMaterial->SetScalarParameterValue(TEXT("Bolt"), Channel);
			LastChannel = Channel;
		}
	}
	if (TreeMaterial && !FMath::IsNearlyEqual(Gust, LastGust, 0.004f))
	{
		// The wind blows the way the rain falls (AStormWindowActor::TickRain): towards -Y. Z is how
		// far a treetop is carried, in centimetres.
		TreeMaterial->SetVectorParameterValue(TEXT("Wind"), FLinearColor(0.25f, -0.97f, FMath::Lerp(16.f, 105.f, Gust)));
		TreeMaterial->SetScalarParameterValue(TEXT("Flutter"), FMath::Lerp(1.5f, 6.f, Gust));
		LastGust = Gust;
	}
}
