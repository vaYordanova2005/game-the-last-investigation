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

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/**
	 * Everything carried in the left hand hangs off this: the lantern, the hand, the sleeve. It
	 * exists so the sway is applied once, to the whole arm, instead of having to be kept in sync
	 * across half a dozen parts.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<USceneComponent> LanternRig;

	/** The lantern itself, carried in view at the bottom-left of frame. */
	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<UStaticMeshComponent> LanternMesh;

	UPROPERTY(VisibleAnywhere, Category = "Lantern")
	TObjectPtr<UPointLightComponent> LanternLight;

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
};
