#include "DetectiveCharacter.h"
#include "DetectiveLanternComponent.h"
#include "InteractionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "RoomBuildLibrary.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"

namespace
{
	/** The detective's lantern's metal (Tools/make_lantern.py): texel sizes match its SLOTS. */
	const FRoomSurface DetectiveLanternTin{ TEXT("rusty_metal_02"), 45.f };
	const FRoomSurface DetectiveLanternRust{ TEXT("rust_coarse_01"), 30.f };
}

ADetectiveCharacter::ADetectiveCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(34.f, 90.f);

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->MaxWalkSpeed = 220.f; // slow, tense pace — not a shooter sprint
	GetCharacterMovement()->JumpZVelocity = 0.f;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 74.f));
	FirstPersonCamera->bUsePawnControlRotation = true;

	// The rig the whole left arm hangs from, so the sway is applied in one place.
	LanternRig = CreateDefaultSubobject<USceneComponent>(TEXT("LanternRig"));
	LanternRig->SetupAttachment(FirstPersonCamera);
	LanternRig->SetMobility(EComponentMobility::Movable);

	// The lantern hangs from the left fist by the top of its bail, the way the reference photograph
	// has it: the hand a little left of and below the middle of the frame, the forearm running off
	// its lower-left corner, the lantern hanging under the fist. The lantern's origin is the top of
	// its bail and the hand's is the grip, so both sit at the same point.
	const FVector Grip(GripForward, GripRight, GripUp);
	const FRotator HandTurn(0.f, HandYaw, 0.f);

	LanternMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LanternMesh"));
	LanternMesh->SetupAttachment(LanternRig);
	LanternMesh->SetRelativeLocationAndRotation(Grip, HandTurn);
	LanternMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The lantern must not cast a shadow. Its own flame is *inside* it, so a shadow-casting body
	// puts the light source behind solid geometry and the lamp lights nothing at all: a bright
	// object in a black room. Suppressing it is not a cheat; the housing is glass and wire, and the
	// shadow it would really throw is the barred pattern, not a sealed blob.
	LanternMesh->SetCastShadow(false);
	LanternMesh->SetMobility(EComponentMobility::Movable);
	LanternMesh->SetLightingChannels(false, false, true);

	HandMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandMesh"));
	HandMesh->SetupAttachment(LanternRig);
	HandMesh->SetRelativeLocationAndRotation(Grip, HandTurn);
	HandMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// No shadow either: the flame is a hand's height under the fist, and a shadow-casting hand
	// paints itself across the whole ceiling.
	HandMesh->SetCastShadow(false);
	HandMesh->SetMobility(EComponentMobility::Movable);
	HandMesh->SetLightingChannels(false, false, true);

	FlameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlameMesh"));
	FlameMesh->SetupAttachment(LanternMesh);
	FlameMesh->SetRelativeLocation(FVector(0.f, 0.f, -LanternFlameRoot));
	FlameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlameMesh->SetCastShadow(false);
	FlameMesh->SetMobility(EComponentMobility::Movable);

	LanternLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LanternLight"));
	// Parented to the lantern, a centimetre up the candle's flame, so light and flame never drift apart.
	LanternLight->SetupAttachment(LanternMesh);
	LanternLight->SetRelativeLocation(FVector(0.f, 0.f, -LanternFlameRoot + 1.f));
	// Candelas are a real photometric unit: UE's own point-light default is 8. Values in the
	// thousands (the legacy "unitless" scale) blow every surface to pure white.
	LanternLight->SetIntensityUnits(ELightUnits::Candelas);
	LanternLight->SetLightColor(FLinearColor(1.f, 0.72f, 0.34f)); // warm gold: the lantern's colour cue, and the cold storm's opposite
	// Reaches across the room but falls off hard, so the near floor and the wall the detective is
	// facing are readable while the far corners stay dark and have to be walked into.
	LanternLight->SetAttenuationRadius(1100.f);
	// A candle flame is small, so its shadows are sharper than a big wick's.
	LanternLight->SetSourceRadius(2.5f);
	LanternLight->SetCastShadows(true);
	LanternLight->SetMobility(EComponentMobility::Movable);

	LanternNearLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LanternNearLight"));
	LanternNearLight->SetupAttachment(LanternMesh);
	LanternNearLight->SetRelativeLocation(FVector(0.f, 0.f, -LanternFlameRoot + 1.f));
	LanternNearLight->SetLightingChannels(false, false, true);
	LanternNearLight->SetIntensityUnits(ELightUnits::Candelas);
	LanternNearLight->SetLightColor(FLinearColor(1.f, 0.66f, 0.30f));
	LanternNearLight->SetAttenuationRadius(LanternNearRadius);
	LanternNearLight->SetSourceRadius(1.5f);
	LanternNearLight->SetCastShadows(false);
	LanternNearLight->SetAffectTranslucentLighting(true);
	LanternNearLight->SetVolumetricScatteringIntensity(0.f);
	LanternNearLight->SetMobility(EComponentMobility::Movable);

	LanternController = CreateDefaultSubobject<UDetectiveLanternComponent>(TEXT("LanternController"));
	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
}

void ADetectiveCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	BuildInputActions();
}

void ADetectiveCharacter::BuildInputActions()
{
	// Input Actions/Mapping Contexts are normally binary Data Assets authored in the Content
	// Browser. Built here at runtime instead so input stays fully code-first, matching the rest
	// of this project's no-editor-GUI workflow. Must happen outside the constructor: NewObject()
	// on a plain (non-component) UObject while still inside a UObject constructor is treated as
	// an invalid anonymous default-subobject creation and fatals — CreateDefaultSubobject is only
	// for actual components. PostInitializeComponents runs synchronously during spawn (unlike
	// BeginPlay, which can be deferred until the world's later BeginPlay dispatch), so the mapping
	// context and actions are guaranteed to exist before PossessedBy/SetupPlayerInputComponent run
	// — for a GameMode-spawned player, those fire immediately after spawn, before the world (and
	// this pawn's own deferred BeginPlay) has necessarily begun play.
	MoveAction = NewObject<UInputAction>(this, TEXT("IA_Move"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;

	LookAction = NewObject<UInputAction>(this, TEXT("IA_Look"));
	LookAction->ValueType = EInputActionValueType::Axis2D;

	InteractAction = NewObject<UInputAction>(this, TEXT("IA_Interact"));
	InteractAction->ValueType = EInputActionValueType::Boolean;

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Default"));

	// Move: X = forward/back, Y = right/left.
	MappingContext->MapKey(MoveAction, EKeys::W);
	{
		FEnhancedActionKeyMapping& S = MappingContext->MapKey(MoveAction, EKeys::S);
		S.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	}
	{
		FEnhancedActionKeyMapping& D = MappingContext->MapKey(MoveAction, EKeys::D);
		D.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this)); // raw X -> Y (right)
	}
	{
		FEnhancedActionKeyMapping& A = MappingContext->MapKey(MoveAction, EKeys::A);
		A.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this)); // raw X -> Y (right)
		A.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	}

	// Look: X = yaw, Y = pitch.
	MappingContext->MapKey(LookAction, EKeys::MouseX);
	{
		FEnhancedActionKeyMapping& MouseY = MappingContext->MapKey(LookAction, EKeys::MouseY);
		MouseY.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this)); // raw X -> Y (pitch)
		MouseY.Modifiers.Add(NewObject<UInputModifierNegate>(this)); // mouse up -> look up
	}

	MappingContext->MapKey(InteractAction, EKeys::E);
}

void ADetectiveCharacter::BeginPlay()
{
	Super::BeginPlay();

	BuildLanternArm();
}

void ADetectiveCharacter::BuildLanternArm()
{
	// The hand, the lantern and its flame are generated models (Tools/make_detective_hand.py,
	// Tools/make_lantern.py), loaded here rather than with a constructor ObjectFinder: they come out
	// of the art pipeline, and a missing one must leave the game playable (the light still burns).
	// The box-and-cylinder arm they replace read as a toy at arm's length from the lens.
	FRoomBuilder Build(this, LanternRig);
	UMaterialInstanceDynamic* FlameMat = nullptr;

	// For the models' own UVs, which are already in repeats of each slot's texel size.
	auto Sheet = [&Build](const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale)
	{
		UMaterialInstanceDynamic* Mat = Build.Surface(Set, Tint, RoughnessScale);
		if (Mat)
		{
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
		return Mat;
	};
	auto Dress = [](UStaticMeshComponent* Part, const TCHAR* Slot, UMaterialInterface* Mat)
	{
		if (Part && Mat && Part->GetMaterialIndex(FName(Slot)) != INDEX_NONE)
		{
			Part->SetMaterialByName(FName(Slot), Mat);
		}
	};

	if (UStaticMesh* Lantern = FRoomShapes::Prop(RoomProps::Lantern))
	{
		LanternMesh->SetStaticMesh(Lantern);
		// Galvanised tin gone to rust (rusty_metal_02, linear mean 0.42/0.29/0.14) and the fully
		// rusted wire of the frame (rust_coarse_01, 0.13/0.04/0.012), both taken down to a dark
		// brown about 0.06 albedo: a lantern this close to the eye and paler than that is a toy.
		Dress(LanternMesh, TEXT("Tin"), Sheet(DetectiveLanternTin, FLinearColor(0.17f, 0.16f, 0.20f), 0.9f));
		Dress(LanternMesh, TEXT("Rust"), Sheet(DetectiveLanternRust, FLinearColor(0.42f, 0.6f, 0.9f), 1.f));
		// The globe: clear glass a candle has been smoking for years. At the window's values a
		// globe this near the eye is milk-white porcelain (the wine glasses, 10-08).
		Dress(LanternMesh, TEXT("Glass"), Build.Glass(FLinearColor(0.035f, 0.031f, 0.025f), 0.06f, 0.05f));
		Dress(LanternMesh, TEXT("Wax"), Build.Flat(FLinearColor(0.30f, 0.25f, 0.17f), 0.55f));
		Dress(LanternMesh, TEXT("Wick"), Build.Flat(FLinearColor(0.01f, 0.009f, 0.008f), 0.9f));
	}
	if (UStaticMesh* Flame = FRoomShapes::Prop(RoomProps::LanternFlame))
	{
		FlameMesh->SetStaticMesh(Flame);
		FlameMat = Build.Emissive(FLinearColor(1.f, 0.56f, 0.2f), 40.f);
		Dress(FlameMesh, TEXT("Flame"), FlameMat);
	}
	if (UStaticMesh* Hand = FRoomShapes::Prop(RoomProps::DetectiveHand))
	{
		HandMesh->SetStaticMesh(Hand);
		// His own skin and his dark suit, as on the man on the hook: the textures MakeHuman ships,
		// taken down a little into the room's dark.
		struct FHandTint { const TCHAR* Slot; FLinearColor Tint; };
		const FHandTint Tints[] = {
			{ TEXT("Skin"), FLinearColor(0.55f, 0.50f, 0.47f) },
			{ TEXT("Suit"), FLinearColor(0.45f, 0.45f, 0.47f) },
		};
		for (const FHandTint& T : Tints)
		{
			const int32 Index = HandMesh->GetMaterialIndex(FName(T.Slot));
			if (Index == INDEX_NONE)
			{
				continue;
			}
			if (UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(HandMesh->GetMaterial(Index), this))
			{
				Mat->SetVectorParameterValue(TEXT("Tint"), T.Tint);
				HandMesh->SetMaterial(Index, Mat);
			}
		}
	}

	LanternController->Initialize(LanternLight, FlameMesh, FlameMat, LanternNearLight, LanternNearCandelas);
}

void ADetectiveCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateLanternSway(DeltaTime);
	UpdateLanternSwing(DeltaTime);
}

void ADetectiveCharacter::UpdateLanternSway(float DeltaTime)
{
	if (!LanternRig || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float MaxSpeed = FMath::Max(GetCharacterMovement()->MaxWalkSpeed, 1.f);
	const float Pace = FMath::Clamp(GetVelocity().Size2D() / MaxSpeed, 0.f, 1.f);

	// A carried lamp traces a flattened figure of eight: once across per stride, twice up and
	// down. At a standstill the same curve keeps running at a fraction of the size, which is the
	// difference between a man holding a lantern and a tripod holding one.
	SwayPhase += DeltaTime * FMath::Lerp(1.5f, 6.2f, Pace);
	const float Amplitude = FMath::Lerp(0.5f, 2.4f, Pace);
	const FVector Stride(
		0.f,
		FMath::Sin(SwayPhase) * Amplitude * 1.5f,
		FMath::Sin(SwayPhase * 2.f) * Amplitude);

	// Turn lag. The arm is heavy: a fast look drags it behind the camera and it swings back after.
	const FRotator Control = GetControlRotation();

	// Primed on the first frame the lantern is actually swayed, not at construction and not in
	// PossessedBy. The previous angles started at zero while the waking pose is yaw 128, pitch -7,
	// so the first tick measured a turn of a hundred and twenty-eight degrees in one frame: the
	// lag target slammed into its clamp and took half a second to crawl back. That is the frame
	// the detective opens his eyes on — the lantern was being thrown aside in it. Priming in
	// PossessedBy does not help either, because the game mode sets the waking rotation *after*
	// possession, so the pose would still arrive as a jump.
	if (!bSwayPrimed)
	{
		PreviousControlYaw = Control.Yaw;
		PreviousControlPitch = Control.Pitch;
		bSwayPrimed = true;
	}

	const float YawRate = FMath::UnwindDegrees(Control.Yaw - PreviousControlYaw) / DeltaTime;
	const float PitchRate = FMath::UnwindDegrees(Control.Pitch - PreviousControlPitch) / DeltaTime;
	PreviousControlYaw = Control.Yaw;
	PreviousControlPitch = Control.Pitch;

	const FVector LagTarget(
		0.f,
		FMath::Clamp(-YawRate * 0.022f, -7.f, 7.f),
		FMath::Clamp(PitchRate * 0.02f, -6.f, 6.f));
	SwayLag = FMath::VInterpTo(SwayLag, LagTarget, DeltaTime, 7.f);

	LanternRig->SetRelativeLocation(Stride + SwayLag);
	LanternRig->SetRelativeRotation(FRotator(SwayLag.Z * 0.7f, SwayLag.Y * 0.9f, -SwayLag.Y * 0.6f));
}

void ADetectiveCharacter::UpdateLanternSwing(float DeltaTime)
{
	if (!LanternMesh || !LanternRig || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// A lantern on a bail is a pendulum hung from the fist: when the hand moves it is left behind
	// and swings back past plumb, a little less each time. Driven by how the grip point
	// accelerates in the world (a step, a turn, the sway), in world X and Y, so the lantern hangs
	// plumb whatever the camera does, and looking down at it does not tip it.
	const FTransform Rig = LanternRig->GetComponentTransform();
	const FVector Grip = Rig.TransformPosition(FVector(GripForward, GripRight, GripUp));
	if (!bSwingPrimed)
	{
		PreviousGrip = Grip;
		PreviousGripVelocity = FVector::ZeroVector;
		bSwingPrimed = true;
	}
	const FVector Velocity = (Grip - PreviousGrip) / DeltaTime;
	FVector Accel = (Velocity - PreviousGripVelocity) / DeltaTime;
	PreviousGrip = Grip;
	PreviousGripVelocity = Velocity;
	// A teleport (the waking pose, a -RoomShot camera) is not a shove.
	if (Accel.Size() > 6000.f)
	{
		Accel = FVector::ZeroVector;
		PreviousGripVelocity = FVector::ZeroVector;
	}

	const float Stiffness = 980.f / LanternSwingLength;
	const float Damping = 2.2f;
	const FVector2D Drive(Accel.X / LanternSwingLength, Accel.Y / LanternSwingLength);
	SwingRate += (-Stiffness * SwingTilt - Damping * SwingRate - Drive) * DeltaTime;
	SwingTilt += SwingRate * DeltaTime;
	const float MaxTilt = 0.5f;
	if (SwingTilt.Size() > MaxTilt)
	{
		SwingTilt = SwingTilt.GetSafeNormal() * MaxTilt;
		SwingRate *= 0.5f;
	}

	// SwingTilt is where the foot has swung to; the lantern's up runs from there to the fist. The
	// bail's wire stays along the hand's.
	const FVector Up = FVector(-SwingTilt.X, -SwingTilt.Y, 1.f).GetSafeNormal();
	const FVector Wire = Rig.TransformVectorNoScale(FRotator(0.f, HandYaw, 0.f).Vector());
	LanternMesh->SetWorldRotation(FRotationMatrix::MakeFromZX(Up, Wire).Rotator());
}

void ADetectiveCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (const APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}
}

void ADetectiveCharacter::UnPossessed()
{
	// Mirror of PossessedBy: the context lives on the local player, which outlives this pawn.
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->RemoveMappingContext(MappingContext);
		}
	}

	Super::UnPossessed();
}

void ADetectiveCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADetectiveCharacter::HandleMove);
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADetectiveCharacter::HandleLook);
		EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &ADetectiveCharacter::HandleInteract);
	}
}

void ADetectiveCharacter::HandleMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();

	if (!FMath::IsNearlyZero(Axis.X))
	{
		AddMovementInput(GetActorForwardVector(), Axis.X);
	}
	if (!FMath::IsNearlyZero(Axis.Y))
	{
		AddMovementInput(GetActorRightVector(), Axis.Y);
	}
}

void ADetectiveCharacter::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ADetectiveCharacter::HandleInteract(const FInputActionValue& Value)
{
	InteractionComponent->InteractWithFocusedTarget();
}
