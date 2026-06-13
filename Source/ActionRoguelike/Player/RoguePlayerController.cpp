


#include "RoguePlayerController.h"
#include "Interact/RogueInteractionComponent.h"
#include "EnhancedInputComponent.h"

ARoguePlayerController::ARoguePlayerController()
{
	InteractionComponent = CreateDefaultSubobject<URogueInteractionComponent>(TEXT("InteractionComp"));
}

void ARoguePlayerController::StartInteract()
{
	if (bIsCharacterDead)
		return;
	
	if (!GetPawn()->InputEnabled())
		return;
	
	InteractionComponent->Interact();
}

void ARoguePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	EnhancedInput->BindAction(Input_Interact, ETriggerEvent::Triggered, this, &ARoguePlayerController::StartInteract);
}

void ARoguePlayerController::HandlePlayerDeath()
{
	bIsCharacterDead = true;
	
	InteractionComponent->DisableInteraction();
	
	UnPossess();
	ChangeState(NAME_Spectating);

	// Ensure the client synchronizes the state change.
	// ClientGotoState(NAME_Spectating);
}