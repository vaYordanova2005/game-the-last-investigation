#include "HallDoorActor.h"
#include "RoomBuildLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
	// -OpenDoors (see ADoorActor): swung wide into its room, so the room behind can be walked into.
	if (FParse::Param(FCommandLine::Get(), TEXT("OpenDoors")))
	{
		// A door that opens goes to exactly its own OpenYaw (wider would swing it through the casing).
		Setup.AjarYaw = Setup.OpenYaw > 0.f ? Setup.OpenYaw : 95.f;
		bOpened = Setup.OpenYaw > 0.f;
	}
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

	if (Setup.bIronBound)
	{
		BuildIronBound(Build, WoodMat, FrameMat, RustMat);
	}
	else
	{
		BuildPanels(Build, FrameMat, PanelMat);
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

	if (Setup.bIronBound)
	{
		return;
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

void AHallDoorActor::BuildPanels(FRoomBuilder& Build, UMaterialInterface* FrameMat, UMaterialInterface* PanelMat)
{
	const float W = Setup.Width;
	const float H = Setup.Height;
	const float LeafY = W * 0.5f;
	const float LeafZ = H * 0.5f;
	const float LeafHalf = 2.25f;
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

}

void AHallDoorActor::BuildIronBound(FRoomBuilder& Build, UMaterialInterface* WoodMat, UMaterialInterface* FrameMat, UMaterialInterface* RustMat)
{
	// Ledged boards: the leaf is the boards themselves, so all that is laid on it is the dark of
	// the joints between them, a ledge across the back, and the iron.
	const float W = Setup.Width;
	const float H = Setup.Height;
	const float LeafHalf = 2.25f;
	UMaterialInstanceDynamic* JointMat = Build.Flat(FLinearColor(0.006f, 0.005f, 0.004f), 1.f);
	UMaterialInstanceDynamic* StudMat = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.70f, 0.37f, 0.58f), 0.8f);
	const int32 Boards = 5;
	for (int32 i = 1; i < Boards; ++i)
	{
		const float Y = W * i / Boards + Random.FRandRange(-0.6f, 0.6f);
		for (const float Side : { 1.f, -1.f })
		{
			Build.Box(FVector(Side * (LeafHalf + 0.05f), Y, H * 0.5f), FRotator::ZeroRotator, FVector(0.4f, 0.8f, H - 2.f), JointMat, /*bBlockingCollision*/ false);
		}
	}
	// Each board a shade apart: they were not cut from one tree, and they did not weather as one.
	for (int32 i = 0; i < Boards; ++i)
	{
		const float Y = W * (i + 0.5f) / Boards;
		const float Shade = Random.FRandRange(0.82f, 1.12f);
		UMaterialInterface* Mat = Shade > 1.f ? FrameMat : WoodMat;
		// Pitched ninety, its length on local X, where the builder lays the larger repeat count: built
		// upright the grain of a board two metres long was smeared down it.
		Build.Box(FVector(LeafHalf + 0.15f, Y, H * 0.5f), FRotator(90.f, 0.f, 0.f), FVector(H - 4.f, W / Boards - 1.2f, 0.3f), Mat, /*bBlockingCollision*/ false);
	}
	// The ledges on the room side and a brace between them.
	for (const float Z : { 30.f, H - 34.f })
	{
		Build.Box(FVector(-(LeafHalf + 1.6f), W * 0.5f, Z), FRotator::ZeroRotator, FVector(3.2f, W - 6.f, 14.f), FrameMat, /*bBlockingCollision*/ false);
	}
	const float BraceLength = FMath::Sqrt(FMath::Square(W - 16.f) + FMath::Square(H - 92.f));
	const float BraceAngle = FMath::RadiansToDegrees(FMath::Atan2(H - 92.f, W - 16.f));
	Build.Box(FVector(-(LeafHalf + 1.4f), W * 0.5f, H * 0.5f - 2.f), FRotator(0.f, 0.f, BraceAngle), FVector(2.8f, BraceLength, 12.f), FrameMat, /*bBlockingCollision*/ false);

	// Three straps across both faces, nailed through: the corridor side's straps run the width; on
	// the room side the top and bottom ones are the hinges, running on past the leaf's edge into the
	// pintle on the jamb.
	for (const float Z : { 24.f, H * 0.5f, H - 26.f })
	{
		for (const float Side : { 1.f, -1.f })
		{
			const float X = Side * (LeafHalf + (Side > 0.f ? 0.7f : 3.6f));
			const float Length = W - 4.f;
			Build.Box(FVector(X, W * 0.5f, Z), FRotator(0.f, 0.f, Random.FRandRange(-0.4f, 0.4f)), FVector(0.8f, Length, 6.f), RustMat, /*bBlockingCollision*/ false);
			for (int32 k = 0; k < 7; ++k)
			{
				const float Y = 6.f + (W - 12.f) * k / 6.f;
				Build.Sph(FVector(X + Side * 0.5f, Y, Z + (k % 2 ? 1.4f : -1.4f)), 1.5f, StudMat);
			}
		}
	}
	for (const float Z : { 24.f, H - 26.f })
	{
		Build.Cyl(FVector(-(LeafHalf + 3.6f), 0.5f, Z), FRotator::ZeroRotator, FVector(4.2f, 4.2f, 9.f), RustMat, /*bBlockingCollision*/ false);
	}

	// The ring pull on its lock plate, both faces, and the keyhole under it.
	const float LockY = W - 14.f;
	const float LockZ = 100.f;
	UMaterialInstanceDynamic* VoidMat = Build.Flat(RoomPalette::Void, 1.f);
	for (const float Side : { 1.f, -1.f })
	{
		const float X = Side * (LeafHalf + 0.6f);
		Build.Box(FVector(X, LockY, LockZ - 4.f), FRotator::ZeroRotator, FVector(1.f, 12.f, 22.f), RustMat, /*bBlockingCollision*/ false);
		Build.Box(FVector(X + Side * 0.55f, LockY, LockZ - 11.f), FRotator::ZeroRotator, FVector(0.2f, 1.2f, 3.4f), VoidMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(X + Side * 1.4f, LockY, LockZ + 3.f), FRotator(90.f, 0.f, 0.f), FVector(2.4f, 2.4f, 2.f), RustMat, /*bBlockingCollision*/ false);
		// The ring hangs from the boss: a loop of sixteen short rods, hanging plumb.
		const float R = 5.f;
		const FVector Centre(X + Side * 2.6f, LockY, LockZ + 3.f - R - 1.f);
		for (int32 k = 0; k < 16; ++k)
		{
			const float A0 = 2.f * PI * k / 16.f;
			const float A1 = 2.f * PI * (k + 1) / 16.f;
			const FVector P0 = Centre + FVector(0.f, FMath::Cos(A0) * R, FMath::Sin(A0) * R);
			const FVector P1 = Centre + FVector(0.f, FMath::Cos(A1) * R, FMath::Sin(A1) * R);
			Build.Cyl((P0 + P1) * 0.5f, FRotationMatrix::MakeFromZ(P1 - P0).Rotator(), FVector(1.f, 1.f, FVector::Dist(P0, P1) + 0.3f), RustMat, /*bBlockingCollision*/ false);
		}
	}
}

void AHallDoorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (SwingTime >= 0.f)
	{
		// Pushed: a heavy leaf on hinges that have not turned in years, so it starts slow, goes, and
		// runs out of way against nothing.
		SwingTime += DeltaTime;
		const float Duration = 2.2f;
		const float Alpha = FMath::Clamp(SwingTime / Duration, 0.f, 1.f);
		const float Eased = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.4f);
		Swing->SetRelativeRotation(FRotator(0.f, FMath::Lerp(Setup.AjarYaw, Setup.OpenYaw, Eased), 0.f));
		if (Alpha >= 1.f)
		{
			SwingTime = -1.f;
			SetActorTickEnabled(false);
		}
		return;
	}

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
	if (bOpened)
	{
		return;
	}
	if (Setup.OpenYaw > Setup.AjarYaw)
	{
		bOpened = true;
		RattleTime = -1.f;
		SwingTime = 0.f;
	}
	else
	{
		RattleTime = 0.f;
	}
	SetActorTickEnabled(true);
}

FText AHallDoorActor::GetInteractPrompt(const AActor* /*Interactor*/) const
{
	// No words on the doors while the house is being designed: only a door that will open says
	// so, and only until it has.
	const bool bCanOpen = !bOpened && Setup.OpenYaw > Setup.AjarYaw;
	return bCanOpen ? FText::FromString(TEXT("[E] Open door")) : FText::GetEmpty();
}
