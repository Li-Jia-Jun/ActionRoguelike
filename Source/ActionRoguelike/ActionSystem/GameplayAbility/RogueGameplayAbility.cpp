// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueGameplayAbility.h"
#include "ActionSystem/RogueActionSystemComponent.h"


bool URogueGameplayAbility::CanActivateAbility() const
{
	if (!OwnerActionSystemComponent->CanApplyGameplayEffect(CostEffect, this))
	{
		return false;
	}
	
	if (!OwnerActionSystemComponent->CanApplyGameplayEffect(CooldownEffect, this))
	{
		return false;
	}
	
	return true;
}

void URogueGameplayAbility::ActivateAbility()
{
	// Blueprint will impl 
}

bool URogueGameplayAbility::CommitAbility()
{
	if (!CanActivateAbility())
	{
		return false;
	}
	
	// Apply cost and cool down
	
	return true;
}

void URogueGameplayAbility::EndAbility()
{
	// Blueprint will impl
}
