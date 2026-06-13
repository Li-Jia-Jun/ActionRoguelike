

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
	
	void HandlePlayerDeath();

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_Interact;

	virtual void SetupInputComponent() override;
	
	bool bIsCharacterDead;

private:
	TObjectPtr<URogueInteractionComponent> InteractionComponent;
};
