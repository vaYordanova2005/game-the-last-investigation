#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DetectiveCharacter.generated.h"

class UCameraComponent;
class USceneComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UDetectiveLanternComponent;
class UInteractionComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

/** First-person player character: the detective, lantern in hand, waking up in the locked room. */
UCLASS()
class ADetectiveCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADetectiveCharacter();

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	/** Set once the room key has been picked up; the door checks this. */
	UPROPERTY(BlueprintReadOnly, Category = "Investigation")
	bool bHasRoomKey = false;

private:
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleInteract(const FInputActionValue& Value);

	void BuildInputActions();

	/** The coat sleeve, cuff and hand that carry the lantern, built from primitives under the rig. */
	void BuildLanternArm();

	/** Stride bob and turn lag on the whole arm, so the lamp has weight rather than being welded to the camera. */
	void UpdateLanternSway(float DeltaTime);

	/** The lantern hangs from the fist on its bail and swings as a pendulum when he moves or turns. */
	void UpdateLanternSwing(float DeltaTime);

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/**
	 * Everything carried in the left hand hangs off this: the lantern, the hand, the sleeve. It
	 * exists so the sway is applied once, to the whole arm, instead of having to be kept in sync
	 * across half a dozen parts.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<USceneComponent> LanternRig;

	/**
	 * The lantern itself (Tools/make_lantern.py). Its origin is the top of its bail, which is where
	 * the fist closes on it, so it sits at the hand's grip point and hangs below it.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<UStaticMeshComponent> LanternMesh;

	/** His left forearm and hand (Tools/make_detective_hand.py), its origin at the same grip point. */
	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<UStaticMeshComponent> HandMesh;

	/** The candle flame inside the globe, its origin at the wick. Guttered by LanternController. */
	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<UStaticMeshComponent> FlameMesh;

	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<UPointLightComponent> LanternLight;

	/**
	 * The flame's light on the lantern itself and on the hand that holds it, which are on lighting
	 * channel 2 and out of LanternLight's reach. A candle a hand's height under the fist, at the
	 * candelas that light a room, burns the fist, the wax and the globe to white (inverse square):
	 * this one is the same flame at a few candelas, so they read the way the reference has them.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<UPointLightComponent> LanternNearLight;

	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<UDetectiveLanternComponent> LanternController;

	UPROPERTY(VisibleAnywhere, Category = "Interaction")
	TObjectPtr<UInteractionComponent> InteractionComponent;

	// Built at runtime in the constructor rather than as Content Browser Data Assets —
	// keeps input code-first, matching this project's no-editor-GUI workflow.
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;

	// Sway state. SwayLag is in centimetres of trailing offset, not an angle: the arm is heavy and
	// gets left behind by a fast turn, then swings back.
	float SwayPhase = 0.f;
	FVector SwayLag = FVector::ZeroVector;
	float PreviousControlYaw = 0.f;
	float PreviousControlPitch = 0.f;

	/** False until the turn-lag has seeded itself from a real control rotation. See UpdateLanternSway. */
	bool bSwayPrimed = false;

	// Swing state: how far the lantern's foot hangs off plumb (radians, world X and Y), and how fast.
	FVector2D SwingTilt = FVector2D::ZeroVector;
	FVector2D SwingRate = FVector2D::ZeroVector;
	FVector PreviousGrip = FVector::ZeroVector;
	FVector PreviousGripVelocity = FVector::ZeroVector;
	bool bSwingPrimed = false;

public:
	/** Where the fist holds the bail, in the camera's frame (cm): forward, right, up. */
	static constexpr float GripForward = 58.f;
	static constexpr float GripRight = -20.f;
	static constexpr float GripUp = -6.f;
	/** The hand's turn about the vertical: the bail's wire runs forward and a little left, the forearm back off the frame's lower-left corner. */
	static constexpr float HandYaw = -45.f;
	/** The wick's top under the bail's top: make_lantern.py, FLAME_ROOT (BAIL_TOP - WICK_TOP). */
	static constexpr float LanternFlameRoot = 24.7f;
	/** Bail top to the lantern's middle of mass, for the swing: about the middle of the globe. */
	static constexpr float LanternSwingLength = 22.f;
	/** LanternNearLight: candelas at an even burn, and how far it reaches (past the fist, short of anything else). */
	static constexpr float LanternNearCandelas = 6.f;
	static constexpr float LanternNearRadius = 45.f;
};
