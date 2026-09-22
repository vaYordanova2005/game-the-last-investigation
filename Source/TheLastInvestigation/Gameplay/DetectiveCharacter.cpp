#include "DetectiveCharacter.h"
#include "DetectiveLanternComponent.h"
#include "InteractionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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

	// The lantern is carried low and to the left, the way you hold one you are walking with, and
	// far enough forward that it sits in the corner of frame instead of filling it.
	LanternMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LanternMesh"));
	LanternMesh->SetupAttachment(LanternRig);
	LanternMesh->SetRelativeLocation(FVector(63.f, -28.f, -45.f));
	LanternMesh->SetRelativeRotation(FRotator(0.f, 16.f, 0.f));
	LanternMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The lantern must not cast a shadow. Its own flame is *inside* it, so a shadow-casting body
	// puts the light source behind solid geometry and the lamp lights nothing at all — a bright
	// object in a black room. Suppressing it is not a cheat; the housing is glass and pierced
	// metal, and the shadow it would really throw is the barred pattern, not a sealed blob.
	LanternMesh->SetCastShadow(false);
	LanternMesh->SetMobility(EComponentMobility::Movable);

	LanternLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LanternLight"));
	// Parented to the lantern, sitting where its flame is, so light and object never drift apart.
	LanternLight->SetupAttachment(LanternMesh);
	// Candelas are a real photometric unit — UE's own point-light default is 8. Values in the
	// thousands (the legacy "unitless" scale) blow every surface to pure white.
	LanternLight->SetIntensityUnits(ELightUnits::Candelas);
	LanternLight->SetLightColor(FLinearColor(1.f, 0.72f, 0.34f)); // warm gold — the lantern's colour cue, and the cold storm's opposite
	// Reaches across the room but falls off hard, so the near floor and the wall the detective is
	// facing are readable while the far corners stay dark and have to be walked into.
	LanternLight->SetAttenuationRadius(1100.f);
	LanternLight->SetSourceRadius(6.f);
	LanternLight->SetCastShadows(true);
	LanternLight->SetMobility(EComponentMobility::Movable);

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

	// Loaded at BeginPlay rather than with a constructor ObjectFinder: the mesh is produced by the
	// art pipeline, and a missing one must leave the game playable (an invisible lantern that
	// still lights the room) rather than fail to start.
	if (UStaticMesh* LanternAsset = FRoomShapes::Prop(RoomProps::Lantern))
	{
		LanternMesh->SetStaticMesh(LanternAsset);

		// Scaled from its own bounds to a real lantern's height, so it does not matter what units
		// the model was authored in.
		const float NaturalHeight = LanternAsset->GetBoundingBox().GetSize().Z;
		if (NaturalHeight > KINDA_SMALL_NUMBER)
		{
			LanternMesh->SetRelativeScale3D(FVector(27.f / NaturalHeight));
		}

		// Put the flame inside the lantern body rather than at its origin.
		LanternLight->SetRelativeLocation(FVector(0.f, 0.f, NaturalHeight * 0.45f));
	}

	BuildLanternArm();

	LanternController->Initialize(LanternLight);
}

void ADetectiveCharacter::BuildLanternArm()
{
	// A hand and a sleeve, from primitives. There is no rigged character in this project and no
	// animation budget, but an unheld lamp floating in the corner of frame is worse than a crude
	// arm: the lantern is the detective's one point of contact with the world, and the eye needs
	// to see him holding it. It lives at the edge of the frame, lit from below by its own flame,
	// where a plain silhouette is all that survives anyway.
	FRoomBuilder Build(this, LanternRig);

	UMaterialInstanceDynamic* CoatMat = Build.Flat(RoomPalette::Coat, 0.88f);
	UMaterialInstanceDynamic* CuffMat = Build.Flat(RoomPalette::Coat * 3.2f, 0.8f);
	UMaterialInstanceDynamic* SkinMat = Build.Flat(RoomPalette::Skin, 0.62f);

	// Elbow off the bottom-left of frame, wrist above the lantern: the forearm crosses the corner
	// of the picture diagonally rather than pointing straight at the viewer.
	// Both points are pushed away from the camera compared with where they would sit on a real
	// body: at arm's length from a 90 degree lens, a forearm of honest thickness fills a quarter
	// of the frame in solid black. Far enough forward, it reads as an arm and leaves the room
	// visible past it.
	const FVector Elbow(34.f, -46.f, -44.f);
	const FVector Wrist(57.f, -31.f, -23.f);
	const FVector Forearm = Wrist - Elbow;
	const FRotator ForearmAim = FRotationMatrix::MakeFromZ(Forearm.GetSafeNormal()).Rotator();

	Build.Cyl(Elbow + Forearm * 0.5f, ForearmAim, FVector(10.5f, 10.f, Forearm.Size() * 1.1f), CoatMat, /*bBlockingCollision*/ false);
	Build.Cyl(Wrist - Forearm.GetSafeNormal() * 4.f, ForearmAim, FVector(9.5f, 9.f, 7.f), CuffMat, /*bBlockingCollision*/ false);

	// The hand: a back, a heel, four fingers curled over the bail and a thumb closing on them.
	Build.Box(Wrist + FVector(2.f, 1.f, 0.f), FRotator(-18.f, 14.f, 6.f), FVector(9.f, 7.f, 5.f), SkinMat, /*bBlockingCollision*/ false);
	for (int32 Finger = 0; Finger < 4; ++Finger)
	{
		// Curled around a bail that runs across the hand, so each finger is a short bar laid
		// sideways, shortest at the outside the way a real grip tapers.
		const float Across = -3.6f + Finger * 2.6f;
		const float Drop = -2.4f + FMath::Abs(1.5f - Finger) * 0.6f;
		Build.Cyl(Wrist + FVector(5.5f + Across * 0.35f, 1.f, Drop - 1.f), FRotator(0.f, 0.f, 90.f),
			FVector(2.3f, 2.3f, 6.2f - FMath::Abs(1.5f - Finger) * 0.5f), SkinMat, /*bBlockingCollision*/ false);
	}
	Build.Cyl(Wrist + FVector(4.f, -3.2f, -3.2f), FRotator(28.f, 0.f, 62.f), FVector(2.7f, 2.7f, 7.2f), SkinMat, /*bBlockingCollision*/ false);

	// None of it casts a shadow, for the same reason the lantern does not: the flame is a hand's
	// width below the fingers, so a shadow-casting hand paints itself across the whole ceiling.
	TArray<USceneComponent*> RigParts;
	LanternRig->GetChildrenComponents(/*bIncludeAllDescendants*/ true, RigParts);
	for (USceneComponent* Part : RigParts)
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part))
		{
			Primitive->SetCastShadow(false);
		}
	}
}

void ADetectiveCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateLanternSway(DeltaTime);
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
