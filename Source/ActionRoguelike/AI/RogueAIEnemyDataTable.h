#pragma once

// #include "CoreMinimal.h"
#include "RogueAIEnemyDataTable.generated.h"


class ARogueAICharacter;

USTRUCT(BlueprintType)
struct FAIEnemySpawnData : public FTableRowBase
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftClassPtr<ARogueAICharacter> EnemyClass;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float SpawnCost = 0.0f;
};