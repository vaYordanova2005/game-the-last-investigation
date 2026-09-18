#include "StormWindowActor.h"
#include "RoomBuildLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FLinearColor LightningColor(0.72f, 0.82f, 1.f); // cold blue-white
}

AStormWindowActor::AStormWindowActor()
{
	PrimaryActorTick.bCanEverTick = true;

	StormRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StormRoot"));
	SetRootComponent(StormRoot);
	StormRoot->SetMobility(EComponentMobility::Movable);

	StormAmbientLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("StormAmbientLight"));
	StormAmbientLight->SetupAttachment(StormRoot);
	StormAmbientLight->SetMobility(EComponentMobility::Movable);
	StormAmbientLight->SetIntensity(0.25f); // lux — overcast midnight, just enough to shape the window
	StormAmbientLight->SetLightColor(FLinearColor(0.42f, 0.52f, 0.78f));
	StormAmbientLight->SetCastShadows(true);

	LightningLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("LightningLight"));
	LightningLight->SetupAttachment(StormRoot);
	LightningLight->SetMobility(EComponentMobility::Movable);
	LightningLight->SetIntensity(0.f);
	LightningLight->SetLightColor(LightningColor);
	LightningLight->SetCastShadows(true); // the whole point: hard, long shadows thrown across the room

	LightningGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("LightningGlow"));
	LightningGlow->SetupAttachment(StormRoot);
	LightningGlow->SetMobility(EComponentMobility::Movable);
	LightningGlow->SetIntensityUnits(ELightUnits::Candelas);
	LightningGlow->SetIntensity(0.f);
	LightningGlow->SetLightColor(LightningColor);
	LightningGlow->SetAttenuationRadius(2400.f);
	LightningGlow->SetCastShadows(false); // pure fill; the directional light already owns the shadows
}

void AStormWindowActor::Configure(const FStormWindowSetup& InSetup)
{
	Setup = InSetup;
}

float AStormWindowActor::GetWindGust() const
{
	// One shared noise curve: a slow swell with a faster ripple on top, so gusts arrive in waves
	// instead of jittering. Remapped from Perlin's [-1,1] into [0,1].
	const float Swell = FMath::PerlinNoise1D(ElapsedTime * 0.23f);
	const float Ripple = FMath::PerlinNoise1D(ElapsedTime * 1.15f + 31.7f);
	return FMath::Clamp((Swell * 0.72f + Ripple * 0.28f) * 0.5f + 0.5f, 0.f, 1.f);
}

void AStormWindowActor::BeginPlay()
{
	Super::BeginPlay();

	Random.Initialize(20260918);

	BuildWindow();
	BuildOutsideWorld();
	BuildRain();

	const float WindowCenterZ = (Setup.SillHeight + Setup.TopHeight) * 0.5f;

	// Aimed down and across, from outside, so the flash rakes through the opening and stretches
	// every object's shadow along the floor toward the far corner.
	const FRotator StrikeAim(-32.f, 165.f, 0.f);
	StormAmbientLight->SetRelativeRotation(StrikeAim);
	LightningLight->SetRelativeRotation(StrikeAim);
	LightningGlow->SetRelativeLocation(FVector(220.f, 0.f, WindowCenterZ + 60.f));

	TimeUntilNextStrike = Random.FRandRange(1.2f, 2.4f); // one early strike, while the player is still getting oriented
}

void AStormWindowActor::BuildWindow()
{
	FRoomBuilder Build(this, StormRoot);

	UMaterialInstanceDynamic* FrameMat = Build.Material(RoomPalette::RottenWood, 0.95f);
	UMaterialInstanceDynamic* GlassMat = Build.Material(RoomPalette::GlassShard, 0.25f);
	UMaterialInstanceDynamic* ClothMat = Build.Material(RoomPalette::Cloth, 0.98f);

	const float Height = Setup.TopHeight - Setup.SillHeight;
	const float CenterZ = (Setup.SillHeight + Setup.TopHeight) * 0.5f;
	const float HalfWidth = Setup.OpeningWidth * 0.5f;
	const float FrameX = Setup.WallThickness * 0.5f + 3.f;

	// Frame: sill, head, two jambs, and a cross mullion splitting it into four panes.
	Build.Box(FVector(FrameX, 0.f, Setup.SillHeight + 5.f), FRotator::ZeroRotator, FVector(14.f, Setup.OpeningWidth + 20.f, 12.f), FrameMat);
	Build.Box(FVector(FrameX, 0.f, Setup.TopHeight - 4.f), FRotator::ZeroRotator, FVector(12.f, Setup.OpeningWidth + 20.f, 10.f), FrameMat);
	Build.Box(FVector(FrameX, -HalfWidth - 4.f, CenterZ), FRotator::ZeroRotator, FVector(12.f, 10.f, Height), FrameMat);
	Build.Box(FVector(FrameX, HalfWidth + 4.f, CenterZ), FRotator::ZeroRotator, FVector(12.f, 10.f, Height), FrameMat);
	Build.Box(FVector(FrameX, 0.f, CenterZ), FRotator::ZeroRotator, FVector(8.f, 7.f, Height), FrameMat);
	Build.Box(FVector(FrameX, 0.f, CenterZ), FRotator::ZeroRotator, FVector(8.f, Setup.OpeningWidth, 7.f), FrameMat);

	// Cracked glass. BasicShapeMaterial is opaque, so a full pane would black out the storm the
	// player is meant to be watching. Instead the panes are modelled as what is *left* of the
	// glass: jagged slivers clinging to the frame edges, with the middle blown out. Reads as a
	// broken window and keeps the view. A real translucent, rain-streaked glass material needs
	// the Material Editor and is noted as a follow-up.
	for (int32 PaneY = 0; PaneY < 2; ++PaneY)
	{
		for (int32 PaneZ = 0; PaneZ < 2; ++PaneZ)
		{
			const float PaneCenterY = (PaneY == 0 ? -1.f : 1.f) * HalfWidth * 0.5f;
			const float PaneCenterZ = CenterZ + (PaneZ == 0 ? -1.f : 1.f) * Height * 0.25f;

			const int32 ShardCount = Random.RandRange(2, 4);
			for (int32 i = 0; i < ShardCount; ++i)
			{
				const float ShardWidth = Random.FRandRange(14.f, 34.f);
				const float ShardHeight = Random.FRandRange(10.f, 30.f);
				// Pushed out toward the pane's own corner, so the hole stays in the middle.
				const FVector ShardLocation(
					FrameX - 1.f,
					PaneCenterY + FMath::Sign(PaneCenterY) * Random.FRandRange(HalfWidth * 0.18f, HalfWidth * 0.42f),
					PaneCenterZ + (PaneZ == 0 ? -1.f : 1.f) * Random.FRandRange(Height * 0.08f, Height * 0.2f));
				Build.Box(ShardLocation, FRotator(Random.FRandRange(-25.f, 25.f), 0.f, 0.f), FVector(1.5f, ShardWidth, ShardHeight), GlassMat, /*bBlockingCollision*/ false);
			}
		}
	}

	// Torn curtains, hung inside the room, moving with the wind coming through the broken panes.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float SideSign = Side == 0 ? -1.f : 1.f;

		USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("CurtainPivot")));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(StormRoot, FAttachmentTransformRules::KeepRelativeTransform);
		// Pivot sits at the rail, so the curtain swings from its top edge like real hanging cloth.
		Pivot->SetRelativeLocation(FVector(-Setup.WallThickness * 0.5f - 12.f, SideSign * (HalfWidth - 12.f), Setup.TopHeight + 6.f));
		Pivot->RegisterComponent();
		AddInstanceComponent(Pivot);

		FRoomBuilder CurtainBuild(this, Pivot);
		const float CurtainHeight = Height + 30.f;

		// Three ragged vertical strips of differing length: a curtain that has been torn, not cut.
		for (int32 Strip = 0; Strip < 3; ++Strip)
		{
			const float StripLength = CurtainHeight * Random.FRandRange(0.55f, 1.f);
			CurtainBuild.Box(
				FVector(Random.FRandRange(-2.f, 2.f), SideSign * -(Strip * 16.f), -StripLength * 0.5f),
				FRotator(0.f, 0.f, Random.FRandRange(-4.f, 4.f)),
				FVector(2.f, 15.f, StripLength),
				ClothMat,
				/*bBlockingCollision*/ false);
		}

		Curtains.Add(Pivot);
	}
}

void AStormWindowActor::BuildOutsideWorld()
{
	FRoomBuilder Build(this, StormRoot);

	UMaterialInstanceDynamic* SkyMat = Build.Material(RoomPalette::NightSky, 1.f);
	UMaterialInstanceDynamic* GroundMat = Build.Material(FLinearColor(0.020f, 0.022f, 0.018f), 1.f);
	UMaterialInstanceDynamic* TrunkMat = Build.Material(FLinearColor(0.022f, 0.019f, 0.016f), 1.f);
	UMaterialInstanceDynamic* LeafMat = Build.Material(RoomPalette::Foliage, 1.f);

	// A backdrop far enough out that it never enters the lantern's reach — it exists so the player
	// sees storm-lit distance through the window instead of the empty void past the level.
	Build.Box(FVector(3200.f, 0.f, 900.f), FRotator::ZeroRotator, FVector(40.f, 6000.f, 3600.f), SkyMat, /*bBlockingCollision*/ false);
	Build.Box(FVector(1600.f, 0.f, -40.f), FRotator::ZeroRotator, FVector(3400.f, 6000.f, 40.f), GroundMat, /*bBlockingCollision*/ false);

	// Treeline. Trunk plus stacked cones; each tree gets its own pivot at the base so it can bend
	// in the wind rather than slide.
	const int32 TreeCount = 9;
	for (int32 i = 0; i < TreeCount; ++i)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("TreePivot")));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(StormRoot, FAttachmentTransformRules::KeepRelativeTransform);
		Pivot->SetRelativeLocation(FVector(Random.FRandRange(550.f, 2200.f), Random.FRandRange(-1400.f, 1400.f), -40.f));
		Pivot->RegisterComponent();
		AddInstanceComponent(Pivot);

		FRoomBuilder TreeBuild(this, Pivot);
		const float TreeHeight = Random.FRandRange(500.f, 900.f);
		TreeBuild.Cyl(FVector(0.f, 0.f, TreeHeight * 0.5f), FRotator::ZeroRotator, FVector(Random.FRandRange(26.f, 44.f), Random.FRandRange(26.f, 44.f), TreeHeight), TrunkMat, /*bBlockingCollision*/ false);

		const int32 CanopyLayers = Random.RandRange(2, 4);
		for (int32 Layer = 0; Layer < CanopyLayers; ++Layer)
		{
			const float LayerFraction = 1.f - Layer * 0.22f;
			TreeBuild.Add(
				FRoomShapes::Cone(),
				FVector(0.f, 0.f, TreeHeight * (0.55f + Layer * 0.18f)),
				FRotator::ZeroRotator,
				FVector(TreeHeight * 0.62f * LayerFraction, TreeHeight * 0.62f * LayerFraction, TreeHeight * 0.45f * LayerFraction),
				LeafMat,
				/*bBlockingCollision*/ false);
		}

		Trees.Add(Pivot);
		TreePhases.Add(Random.FRandRange(0.f, 100.f));
	}
}

void AStormWindowActor::BuildRain()
{
	FRoomBuilder Build(this, StormRoot);

	// Rain as instanced slivers rather than a particle system: Niagara systems are Content Browser
	// assets and this project builds everything from code. A few hundred streaks in the window's
	// cone of view is enough — the player only ever sees rain framed by the opening.
	UMaterialInstanceDynamic* RainMat = Build.Material(RoomPalette::Rain, 0.1f, 0.f);
	RainInstances = Build.Instances(FRoomShapes::Cube(), RainMat);
	if (!RainInstances)
	{
		return;
	}

	RainPositions.Reserve(RainDropCount);
	RainSpeeds.Reserve(RainDropCount);

	for (int32 i = 0; i < RainDropCount; ++i)
	{
		const FVector Position(Random.FRandRange(120.f, 1500.f), Random.FRandRange(-900.f, 900.f), Random.FRandRange(-40.f, 800.f));
		RainPositions.Add(Position);
		RainSpeeds.Add(Random.FRandRange(1300.f, 2000.f));

		// Slanted along the wind and stretched into a streak — the shape a falling drop makes on screen.
		const FTransform DropTransform(FRotator(0.f, 0.f, -18.f), Position, FVector(0.012f, 0.012f, Random.FRandRange(0.28f, 0.62f)));
		RainInstances->AddInstance(DropTransform);
	}
}

void AStormWindowActor::TickCurtains(float DeltaTime)
{
	const float Gust = GetWindGust();

	for (int32 i = 0; i < Curtains.Num(); ++i)
	{
		USceneComponent* Pivot = Curtains[i];
		if (!Pivot)
		{
			continue;
		}

		// Cloth lags the gust and overshoots slightly. Sampling the same noise at a different rate
		// gives that without a cloth sim.
		const float Lag = FMath::PerlinNoise1D(ElapsedTime * 0.9f + i * 13.3f) * 0.5f + 0.5f;
		const float Swing = FMath::Lerp(1.5f, 16.f, Gust) * FMath::Lerp(0.6f, 1.f, Lag);
		const float Sway = FMath::Sin(ElapsedTime * 1.4f + i * 2.1f) * 2.5f;
		Pivot->SetRelativeRotation(FRotator(0.f, Sway, Swing * (i == 0 ? 1.f : -1.f)));
	}
}

void AStormWindowActor::TickTrees(float DeltaTime)
{
	const float Gust = GetWindGust();

	for (int32 i = 0; i < Trees.Num(); ++i)
	{
		USceneComponent* Pivot = Trees[i];
		if (!Pivot)
		{
			continue;
		}

		const float Phase = TreePhases.IsValidIndex(i) ? TreePhases[i] : 0.f;
		// Violent, not decorative: a heavy lean driven by the gust plus a fast whip on top.
		const float Lean = FMath::Lerp(2.f, 13.f, Gust) * (0.7f + 0.3f * FMath::Sin(ElapsedTime * 0.8f + Phase));
		const float Whip = FMath::Sin(ElapsedTime * 3.3f + Phase) * FMath::Lerp(0.8f, 4.f, Gust);
		Pivot->SetRelativeRotation(FRotator(0.f, 0.f, -(Lean + Whip)));
	}
}

void AStormWindowActor::TickRain(float DeltaTime)
{
	if (!RainInstances || RainPositions.Num() == 0)
	{
		return;
	}

	const float Gust = GetWindGust();
	const float WindDrift = FMath::Lerp(180.f, 620.f, Gust);
	const float Slant = FMath::Lerp(-10.f, -30.f, Gust);

	TArray<FTransform> Transforms;
	Transforms.Reserve(RainPositions.Num());

	for (int32 i = 0; i < RainPositions.Num(); ++i)
	{
		FVector& Position = RainPositions[i];
		Position.Z -= RainSpeeds[i] * DeltaTime;
		Position.Y -= WindDrift * DeltaTime;

		// Recycle from the top once a drop lands or blows out of the band, keeping the count fixed.
		if (Position.Z < -40.f || Position.Y < -950.f)
		{
			Position.X = Random.FRandRange(120.f, 1500.f);
			Position.Y = Random.FRandRange(-200.f, 950.f);
			Position.Z = Random.FRandRange(700.f, 900.f);
		}

		Transforms.Add(FTransform(FRotator(0.f, 0.f, Slant), Position, FVector(0.012f, 0.012f, 0.45f)));
	}

	RainInstances->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}

void AStormWindowActor::BeginStrike()
{
	// Each strike is a short burst. The first sub-flash is the brightest; the rest are the
	// flickering afterbeats that make lightning feel like a discharge rather than a light switch.
	SubFlashesRemaining = Random.RandRange(2, 5);
	StrikeIntensity = Random.FRandRange(StrikeLux.X, StrikeLux.Y);
	bSubFlashOn = true;
	SubFlashTimer = Random.FRandRange(0.05f, 0.12f);

	// Nudge the aim so consecutive strikes throw shadows in visibly different directions.
	const FRotator Aim(Random.FRandRange(-42.f, -22.f), Random.FRandRange(150.f, 200.f), 0.f);
	LightningLight->SetRelativeRotation(Aim);

	// PlayThunder() belongs here — deliberately unwired until a free-licensed thunder cue is
	// sourced, matching the storm-audio decision already logged for this room.
}

void AStormWindowActor::TickLightning(float DeltaTime)
{
	if (SubFlashesRemaining > 0)
	{
		SubFlashTimer -= DeltaTime;
		if (SubFlashTimer <= 0.f)
		{
			if (bSubFlashOn)
			{
				// Gap between afterbeats.
				bSubFlashOn = false;
				SubFlashTimer = Random.FRandRange(0.04f, 0.14f);
				--SubFlashesRemaining;
				if (SubFlashesRemaining <= 0)
				{
					TimeUntilNextStrike = Random.FRandRange(StrikeInterval.X, StrikeInterval.Y);
				}
			}
			else
			{
				bSubFlashOn = true;
				SubFlashTimer = Random.FRandRange(0.03f, 0.1f);
				StrikeIntensity *= Random.FRandRange(0.45f, 0.8f); // afterbeats fall off
			}
		}
	}
	else
	{
		TimeUntilNextStrike -= DeltaTime;
		if (TimeUntilNextStrike <= 0.f)
		{
			BeginStrike();
		}
	}

	// Snaps on instantly but decays over a few frames, so the room dims back down instead of
	// cutting to black and the flash leaves an afterimage.
	const float Target = (SubFlashesRemaining > 0 && bSubFlashOn) ? 1.f : 0.f;
	FlashAlpha = (Target > FlashAlpha) ? Target : FMath::FInterpTo(FlashAlpha, Target, DeltaTime, 14.f);

	LightningLight->SetIntensity(FlashAlpha * StrikeIntensity);
	LightningGlow->SetIntensity(FlashAlpha * StrikeIntensity * 260.f);
}

void AStormWindowActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedTime += DeltaTime;

	TickLightning(DeltaTime);
	TickCurtains(DeltaTime);
	TickTrees(DeltaTime);
	TickRain(DeltaTime);
}
