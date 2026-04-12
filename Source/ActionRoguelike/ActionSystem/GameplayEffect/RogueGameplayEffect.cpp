// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueGameplayEffect.h"
#include "ActionSystem/RogueActionSystemComponent.h"

static TAutoConsoleVariable<int32> CVarShowPickableNames(
	TEXT("Debug.ShowPickableNames"),
	1, // Default to 1 (On)
	TEXT("Toggles 3D debug names over pickable items.\n")
	TEXT("0: Hide, 1: Show")
);

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
