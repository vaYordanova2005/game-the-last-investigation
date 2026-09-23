#include "HallDoorActor.h"
#include "RoomBuildLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"

AHallDoorActor::AHallDoorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	// Only while rattling: Interact switches it on, Tick switches it off when the knock dies.
	PrimaryActorTick.bStartWithTickEnabled = false;

	HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRoot"));
	SetRootComponent(HingeRoot);

	Swing = CreateDefaultSubobject<USceneComponent>(TEXT("Swing"));
	Swing->SetupAttachment(HingeRoot);
	Swing->SetMobility(EComponentMobility::Movable);
}

void AHallDoorActor::Configure(const FHallDoorSetup& InSetup)
{
	Setup = InSetup;
}

void AHallDoorActor::BeginPlay()
{
	Super::BeginPlay();

	Random.Initialize(Setup.Seed);
	BuildLeaf();
	Swing->SetRelativeRotation(FRotator(0.f, Setup.AjarYaw, 0.f));
}

void AHallDoorActor::BuildLeaf()
{
	FRoomBuilder Build(this, Swing);

	// Same construction as the bedroom door (ADoorActor), because it is the same joiner's work on
	// the same landing: bare weathered boards, the joinery a shade lighter so it catches an edge,
	// the fields a shade darker, rot climbing from the foot. The tint is what makes each one a
	// different door — how much finish it kept, how much water reached it.
	const FLinearColor Tint = Setup.WoodTint;
	UMaterialInstanceDynamic* WoodMat = Build.Surface(RoomSurfaces::RoughWood, Tint);
	UMaterialInstanceDynamic* FrameMat = Build.Surface(RoomSurfaces::RoughWood, Tint * 1.25f);
	UMaterialInstanceDynamic* PanelMat = Build.Surface(RoomSurfaces::RoughWood, Tint * 0.86f);
	UMaterialInstanceDynamic* RotMat = Build.Surface(RoomSurfaces::RoughWood, Tint * 0.72f);
	// Brass gone brown. green_metal_rust needs its corrected tint or it comes out green paint.
	UMaterialInstanceDynamic* BrassMat = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	UMaterialInstanceDynamic* RustMat = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.988f, 0.407f, 0.499f));
	UMaterialInstanceDynamic* VoidMat = Build.Flat(RoomPalette::Void, 1.f);

	const float W = Setup.Width;
	const float H = Setup.Height;
	const float LeafY = W * 0.5f;
	const float LeafZ = H * 0.5f;
	const float LeafHalf = 2.25f;

	// The leaf itself blocks; everything laid on it is decoration.
	Build.Box(FVector(0.f, LeafY, LeafZ), FRotator::ZeroRotator, FVector(LeafHalf * 2.f, W, H), WoodMat);

	// Both faces get the joinery. Local +X is the corridor, but a door that is ajar shows its
	// other face along the gap, and a bare slab there reads as a board leant in a doorway.
	auto OnBothFaces = [&](float Proud, float Y, float Z, const FVector& Size, UMaterialInterface* Mat, float Roll = 0.f)
	{
		for (const float Side : { 1.f, -1.f })
		{
			Build.Box(FVector(Side * (LeafHalf + Proud), Y, Z), FRotator(0.f, 0.f, Roll), Size, Mat, /*bBlockingCollision*/ false);
		}
	};

	const float Stile = 12.f;
	OnBothFaces(0.8f, Stile * 0.5f, LeafZ, FVector(1.6f, Stile, H - 4.f), FrameMat);
	OnBothFaces(0.8f, W - Stile * 0.5f, LeafZ, FVector(1.6f, Stile, H - 4.f), FrameMat);
	OnBothFaces(0.8f, LeafY, H - 7.f, FVector(1.6f, W - 4.f, 12.f), FrameMat);
	OnBothFaces(0.8f, LeafY, 9.f, FVector(1.6f, W - 4.f, 18.f), FrameMat);
	OnBothFaces(0.8f, LeafY, H * 0.47f, FVector(1.6f, W - 4.f, 15.f), FrameMat);

	// The fields. A six-panel door has a muntin down the middle and three rows; a four-panel has
	// two tall fields over two short ones.
	const int32 Rows = Setup.bSixPanel ? 3 : 2;
	const float FieldW = (W - Stile * 3.f) * 0.5f;
	const float LowerBottom = 18.f;
	const float LowerTop = H * 0.47f - 7.5f;
	const float UpperBottom = H * 0.47f + 7.5f;
	const float UpperTop = H - 13.f;
	OnBothFaces(0.8f, LeafY, LeafZ, FVector(1.6f, 10.f, H - 30.f), FrameMat);

	for (int32 Column = 0; Column < 2; ++Column)
	{
		const float FieldY = Stile + FieldW * 0.5f + Column * (FieldW + Stile);
		if (Rows == 2)
		{
			OnBothFaces(0.2f, FieldY, (LowerBottom + LowerTop) * 0.5f, FVector(1.f, FieldW - 2.f, LowerTop - LowerBottom - 2.f), PanelMat);
			OnBothFaces(0.2f, FieldY, (UpperBottom + UpperTop) * 0.5f, FVector(1.f, FieldW - 2.f, UpperTop - UpperBottom - 2.f), PanelMat);
		}
		else
		{
			const float Split = UpperBottom + (UpperTop - UpperBottom) * 0.42f;
			OnBothFaces(0.8f, FieldY, Split, FVector(1.6f, FieldW, 10.f), FrameMat);
			OnBothFaces(0.2f, FieldY, (LowerBottom + LowerTop) * 0.5f, FVector(1.f, FieldW - 2.f, LowerTop - LowerBottom - 2.f), PanelMat);
			OnBothFaces(0.2f, FieldY, (UpperBottom + Split - 5.f) * 0.5f, FVector(1.f, FieldW - 2.f, Split - 5.f - UpperBottom - 2.f), PanelMat);
			OnBothFaces(0.2f, FieldY, (Split + 5.f + UpperTop) * 0.5f, FVector(1.f, FieldW - 2.f, UpperTop - Split - 5.f - 2.f), PanelMat);
		}
	}

	// Rot at the foot, with a few blooms climbing from it.
	OnBothFaces(0.35f, LeafY, 10.f, FVector(1.f, W - 2.f, 18.f), RotMat);
	for (int32 i = 0; i < 4; ++i)
	{
		OnBothFaces(0.4f, Random.FRandRange(10.f, W - 10.f), Random.FRandRange(14.f, 44.f),
			FVector(1.f, Random.FRandRange(10.f, 24.f), Random.FRandRange(12.f, 30.f)), RotMat, Random.FRandRange(-10.f, 10.f));
	}

	if (Setup.bRotHole)
	{
		Build.Box(FVector(0.f, W * 0.3f, 34.f), FRotator(0.f, 0.f, 14.f), FVector(8.f, 12.f, 15.f), VoidMat, /*bBlockingCollision*/ false);
	}

	// Splits with the grain: hair-thin and flat against the face (see ADoorActor for why).
	for (int32 i = 0; i < 4; ++i)
	{
		const float Side = Random.FRand() < 0.7f ? 1.f : -1.f;
		Build.Box(FVector(Side * (LeafHalf + 0.45f), Random.FRandRange(8.f, W - 8.f), Random.FRandRange(40.f, H - 40.f)),
			FRotator(0.f, 0.f, Random.FRandRange(-3.f, 3.f)),
			FVector(0.5f, 0.9f, Random.FRandRange(30.f, 90.f)), VoidMat, /*bBlockingCollision*/ false);
	}

	// The furniture: a knob and its rose on each face, a keyhole escutcheon under it, and hinges
	// on the room side only — a door that opens away from you shows you no hinges.
	const float LockY = W - 8.f;
	const float LockZ = 98.f;
	for (const float Side : { 1.f, -1.f })
	{
		Build.Cyl(FVector(Side * (LeafHalf + 0.6f), LockY, LockZ), FRotator(90.f, 0.f, 0.f), FVector(6.5f, 6.5f, 1.2f), BrassMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(Side * (LeafHalf + 3.f), LockY, LockZ), FRotator(90.f, 0.f, 0.f), FVector(1.6f, 1.6f, 5.f), BrassMat, /*bBlockingCollision*/ false);
		Build.Sph(FVector(Side * (LeafHalf + 6.f), LockY, LockZ), 5.6f, BrassMat);
		Build.Box(FVector(Side * (LeafHalf + 0.5f), LockY, LockZ - 14.f), FRotator::ZeroRotator, FVector(1.f, 4.4f, 9.f), BrassMat, /*bBlockingCollision*/ false);
		Build.Box(FVector(Side * (LeafHalf + 1.05f), LockY, LockZ - 13.f), FRotator::ZeroRotator, FVector(0.2f, 0.9f, 3.2f), VoidMat, /*bBlockingCollision*/ false);
	}
	for (const float HingeZ : { 26.f, H - 30.f })
	{
		Build.Box(FVector(-(LeafHalf + 0.8f), 7.f, HingeZ), FRotator::ZeroRotator, FVector(1.5f, 16.f, 14.f), RustMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(-(LeafHalf + 1.6f), 0.5f, HingeZ), FRotator::ZeroRotator, FVector(5.f, 5.f, 16.f), RustMat, /*bBlockingCollision*/ false);
	}

	// Rust run down the corridor face from the escutcheon.
	for (int32 i = 0; i < 2; ++i)
	{
		const float Length = Random.FRandRange(18.f, 46.f);
		Build.Box(FVector(LeafHalf + 0.4f, LockY + Random.FRandRange(-2.f, 2.f), LockZ - 18.f - Length * 0.5f),
			FRotator::ZeroRotator, FVector(1.f, Random.FRandRange(1.5f, 3.f), Length), RustMat, /*bBlockingCollision*/ false);
	}
}

void AHallDoorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (RattleTime < 0.f)
	{
		return;
	}

	// Gives a degree and a half and knocks back against whatever is holding it: a locked door
	// against its bolt, an ajar one against whatever is on the other side.
	RattleTime += DeltaTime;
	const float Duration = 0.45f;
	const float Offset = RattleTime < Duration
		? FMath::Sin(RattleTime / Duration * PI * 3.f) * (1.f - RattleTime / Duration) * 1.5f
		: 0.f;
	Swing->SetRelativeRotation(FRotator(0.f, Setup.AjarYaw + Offset, 0.f));
	if (RattleTime >= Duration)
	{
		RattleTime = -1.f;
		SetActorTickEnabled(false);
	}
}

void AHallDoorActor::Interact(AActor* /*Interactor*/)
{
	RattleTime = 0.f;
	SetActorTickEnabled(true);
}

FText AHallDoorActor::GetInteractPrompt(const AActor* /*Interactor*/) const
{
	return FText::FromString(Setup.Prompt);
}
