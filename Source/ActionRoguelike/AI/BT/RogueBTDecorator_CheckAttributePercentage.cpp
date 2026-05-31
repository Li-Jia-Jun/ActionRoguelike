#include "RogueBTDecorator_CheckAttributePercentage.h"
#include "GameFramework/Character.h"
#include "AIController.h"
#include "ActionSystem/RogueActionSystemComponent.h"


bool URogueBTDecorator_CheckAttributePercentage::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	ACharacter* ThisCharacter = Cast<ACharacter>(OwnerComp.GetAIOwner()->GetPawn());
	check(ThisCharacter);
	
	URogueActionSystemComponent* ThisASC = ThisCharacter->GetComponentByClass<URogueActionSystemComponent>();
	check(ThisASC);
	
	FRogueAttributeSetSnapshot AttributeSetSnapshot = ThisASC->TakeAttributeSnapshot();
	FAttributeNumericData CurrentAttribute;
	FAttributeNumericData MaxAttribute;
	if (!(UAttributeSetFunctionLibrary::FindAttributeDataByTag(AttributeSetSnapshot.Attributes, CurrentAttributeTag, CurrentAttribute) and
		UAttributeSetFunctionLibrary::FindAttributeDataByTag(AttributeSetSnapshot.Attributes, MaxAttributeTag, MaxAttribute)))
	{
		UE_LOG(LogBehaviorTree, Warning, TEXT("Failed to find attribute data for CurrentAttribute or MaxAttribute"));
		return false;
	}
	
	switch (CheckMode)
	{
		case EAttributeSCheckMode::Lower:
			return CurrentAttribute.CurrentValue < MaxAttribute.CurrentValue * Percentage;
		case EAttributeSCheckMode::Higher:
			return CurrentAttribute.CurrentValue > MaxAttribute.CurrentValue * Percentage;
		default:
			return false;
	}
}
