#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FRogueGameplayEventData.generated.h"

USTRUCT(BlueprintType)
struct ACTIONROGUELIKE_API FRogueGameplayEventData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Event")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Event")
	TObjectPtr<UObject> SourceObject = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Event")
	TObjectPtr<UObject> TargetObject = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Event")
	float Value = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Event")
	FVector VectorValue = FVector::ZeroVector;
};
