

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RogueInteractionComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class ACTIONROGUELIKE_API URogueInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	URogueInteractionComponent();

	void Interact();
	
	void DisableInteraction()
	{
		bIsInteractionEnabled = false;
	};

protected:
	UPROPERTY(VisibleAnywhere, Category = "Interaction")
	float InteractionRadius = 500;
	
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractionPickActorLookWeight = 1.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractionPickActorDistanceWeight = 1.0f;

	UPROPERTY(VisibleAnywhere, Category = "Interaction")
	TObjectPtr<AActor> SelectedActor;
	
	bool bIsInteractionEnabled = true;

public:	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
