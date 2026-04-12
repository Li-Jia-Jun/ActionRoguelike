#pragma once
#include "CoreMinimal.h"
#include "ERogueGameplayAbilityInstancePolicy.generated.h"


UENUM(BlueprintType)
enum class ERogueGameplayAbilityInstancePolicy : uint8 
{ 
	eNotInstanced UMETA(DisplayName = "Not Instanced"), // TODO ban Blueprint from using it
	eInstancePerActor UMETA(DisplayName = "Instance Per Actor"),
	eInstancePerExecution UMETA(DisplayName = "Instance Per Execution"),
};
