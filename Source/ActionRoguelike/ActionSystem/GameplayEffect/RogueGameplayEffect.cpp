// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueGameplayEffect.h"
#include "RogueGameplayEffectDurationPolicy.h"
#include "RogueGameplayEffectModifyPolicy.h"	

#if WITH_EDITOR
EDataValidationResult URogueGameplayEffect::IsDataValid(FDataValidationContext& Context) const
{
	// Debuff modify can only apply to duration policy
	if (ModifyPolicy.GetScriptStruct() == FRogueGameplayEffectDebuffModify::StaticStruct())
	{
		if (DurationPolicy.GetScriptStruct() != FRogueGameplayEffectDurationApply::StaticStruct())
		{
			UE_LOG(LogTemp, Error, TEXT("Debuff modify can only apply to duration policy."));
			return EDataValidationResult::Invalid;
		}
	}
	
	if (DurationPolicy.GetScriptStruct() == FRogueGameplayEffectInstantApply::StaticStruct())
	{
		if (TagsToGrant.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("TagsToGrant will be skipped for instant apply."));
		}
		
		if (TagsThisBlock.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("TagsThisBlock will be skipped for instant apply."));
		}
	}
	
	return EDataValidationResult::Valid;
}
#endif