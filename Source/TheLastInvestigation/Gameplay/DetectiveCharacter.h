#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DetectiveCharacter.generated.h"

class UCameraComponent;
class UPointLightComponent;
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

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

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
};
