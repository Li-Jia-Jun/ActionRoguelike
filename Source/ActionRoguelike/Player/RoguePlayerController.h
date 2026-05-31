

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RoguePlayerController.generated.h"

class URogueInteractionComponent;
class UInputAction;

/**
 * 
 */
UCLASS()
class ACTIONROGUELIKE_API ARoguePlayerController : public APlayerController
{
	GENERATED_BODY()
	

public:
	ARoguePlayerController();
	
	void StartInteract();

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_Interact;

	virtual void SetupInputComponent() override;

private:
	TObjectPtr<URogueInteractionComponent> InteractionComponent;
};
