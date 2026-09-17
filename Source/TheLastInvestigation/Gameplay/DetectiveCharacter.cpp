#include "DetectiveCharacter.h"
#include "DetectiveLanternComponent.h"
#include "InteractionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
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

	LanternLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LanternLight"));
	LanternLight->SetupAttachment(FirstPersonCamera);
	LanternLight->SetRelativeLocation(FVector(12.f, 8.f, -10.f));
	// Candelas are a real photometric unit — UE's own point-light default is 8. Values in the
	// thousands (the legacy "unitless" scale) blow every surface to pure white.
	LanternLight->SetIntensityUnits(ELightUnits::Candelas);
	LanternLight->SetLightColor(FLinearColor(1.f, 0.78f, 0.42f)); // warm gold — the lantern's colour cue
	LanternLight->SetAttenuationRadius(900.f);
	LanternLight->SetSourceRadius(4.f);
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
	LanternController->Initialize(LanternLight);
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
