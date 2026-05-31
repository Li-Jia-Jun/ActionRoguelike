

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ActionRoguelike/Interact/RogueInteractionInterface.h"
#include "RogueItemChest.generated.h"

UCLASS()
class ACTIONROGUELIKE_API ARogueItemChest : public AActor, public IRogueInteractionInterface
{
	GENERATED_BODY()
	
protected:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BaseMeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LidMeshComponent;

	UPROPERTY(EditAnywhere, Category = "Animation")
	float AnimationSpeed = 30.0f;

	UPROPERTY(EditAnywhere, Category = "Animation")
	float AnimationTargetPitch = 120.f;

	float CurrentAnimationPitch = 0.0f;

protected:
	UFUNCTION(BlueprintImplementableEvent)
	void OnOpenLidAnimationFinish();
	
	void AnimateLid(float DeltaTime);

public:	
	ARogueItemChest();

	virtual void Interact_Implementation();

	virtual void Tick(float DeltaTime) override;	
};
