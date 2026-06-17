// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueGameplayEffect.h"
#include "RogueGameplayEffectDurationPolicy.h"
#include "RogueGameplayEffectModifyPolicy.h"	


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
	
	return EDataValidationResult::Valid;
}