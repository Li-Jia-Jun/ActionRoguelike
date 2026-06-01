#pragma once
#include "CoreMinimal.h"
#include "FRogueGameplayAbilityEndedData.generated.h"

USTRUCT(BlueprintType)
struct ACTIONROGUELIKE_API FRogueGameplayAbilityEndedData
{
	GENERATED_BODY()
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bIsSuccessful = false;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UObject* Object = nullptr;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Value = 0.0f;
};
