// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueGameplayEffect.h"
#include "Misc/DataValidation.h"
#include "RogueGameplayEffectDurationPolicy.h"
#include "ActionSystem/RogueActionSystemComponent.h"

// static TAutoConsoleVariable<int32> CVarShowPickableNames(
// 	TEXT("Debug.ShowPickableNames"),
// 	1, // Default to 1 (On)
// 	TEXT("Toggles 3D debug names over pickable items.\n")
// 	TEXT("0: Hide, 1: Show")
// );

EDataValidationResult URogueAttributeGameplayEffect::IsDataValid(class FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	
	if (not (DurationPolicy.GetScriptStruct() == FRogueGameplayEffectInstantApply::StaticStruct() or DurationPolicy.GetScriptStruct() == FRogueGameplayEffectPeriodicApply::StaticStruct()))
	{
		Context.AddError(FText::FromString(TEXT("Attribute effect duration policy must be either Instant Apply or Periodic Apply")));
		Result = EDataValidationResult::Invalid;
	}
	
	return Result;
}

EDataValidationResult URogueDebuffGameplayEffect::IsDataValid(class FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	
	if (not (DurationPolicy.GetScriptStruct() == FRogueGameplayEffectDurationApply::StaticStruct() or DurationPolicy.GetScriptStruct() == FRogueGameplayEffectInfiniteTimeApply::StaticStruct()))
	{
		Context.AddError(FText::FromString(TEXT("Debuff effect duration policy must be either Duration Apply or Infinite Time Apply")));
		Result = EDataValidationResult::Invalid;
	}
	
	return Result;
}


#if WITH_EDITOR
void URogueDebuffGameplayEffect::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	
	// Set all Debuff Data tags to this effect tag
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(URogueDebuffGameplayEffect, Debuffs))
	{
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
		{
			for (auto Debuff : Debuffs)
			{
				Debuff.Tag = EffectTag;
			}
		}
	}
}
#endif
