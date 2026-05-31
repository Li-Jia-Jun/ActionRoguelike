#pragma once
#include "CoreMinimal.h"
#include "FRogueGameplayAbilityEndedData.generated.h"

USTRUCT(BlueprintType)
struct ACTIONROGUELIKE_API FRogueGameplayAbilityEndedData
{
	GENERATED_BODY()
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bIsSuccessful;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UObject* Object;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Value;
};
