#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ERogueGameplayAbilityInstancePolicy.h"
#include "FRogueGameplayAbilitySpec.generated.h"

class URogueGameplayAbility;
class URogueActionSystemComponent;


USTRUCT(BlueprintType)
struct FRogueGameplayAbilitySpec
{
	GENERATED_BODY()
	
	FRogueGameplayAbilitySpec()
	{
		InstancePolicy = ERogueGameplayAbilityInstancePolicy::eInstancePerActor;
	}
	
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<URogueGameplayAbility> AbilityClass;
	
	// Cache from Ability
	UPROPERTY()
	FGameplayTag AbilityTag;
	
	// Cache from Ability
	UPROPERTY()
	ERogueGameplayAbilityInstancePolicy InstancePolicy;
	
	// Cache from Ability
	UPROPERTY()
	TObjectPtr<URogueActionSystemComponent> OwnerActionSystemComponent;
	
	// Only used when InstancePolicy is set to InstancedPerActor
	UPROPERTY()
	TObjectPtr<URogueGameplayAbility> SingleSpawnedInstance;
};