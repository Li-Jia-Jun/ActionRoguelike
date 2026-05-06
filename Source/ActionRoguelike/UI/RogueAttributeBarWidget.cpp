// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueAttributeBarWidget.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "Components/ProgressBar.h"


void URogueAttributeBarWidget::InitializeWithASC(URogueActionSystemComponent* InASC, 
	FGameplayTag InCurrentAttributeTag, FGameplayTag InMaxAttributeTag)
{
	ASC = InASC;
	CurrentAttributeTag = InCurrentAttributeTag;
	MaxAttributeTag = InMaxAttributeTag;
	
	FRogueAttributeSetSnapshot AttributeSetSnapshot =  ASC->TakeAttributeSnapshot();
	
	// Sanity check for CurrentHealth and MaxHealth object
	bool bHasCurrentAttribute = false;
	bool bHasMaxAttribute = false;
	for (auto Attribute : AttributeSetSnapshot.Attributes)
	{
		if (Attribute.Tag.MatchesTag(CurrentAttributeTag))
		{
			bHasCurrentAttribute = true;
		}
		else if (Attribute.Tag.MatchesTag(MaxAttributeTag))
		{
			bHasMaxAttribute = true;
		}
	}

	if (bHasCurrentAttribute && bHasMaxAttribute)
	{
		ASC->RegisterAttributeSetChangedCallback<URogueAttributeBarWidget>(this, &URogueAttributeBarWidget::OnAttributeSetChanged);
		OnAttributeSetChanged(AttributeSetSnapshot, AttributeSetSnapshot);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to bind AttributeBar widget to %s due to missing attributes: %s, %s"), 
			*ASC->GetOwner()->GetName(), *CurrentAttributeTag.ToString(), *MaxAttributeTag.ToString());
	}
}

void URogueAttributeBarWidget::OnAttributeSetChanged(FRogueAttributeSetSnapshot OldSnapshot, FRogueAttributeSetSnapshot NewSnapshot)
{
	FAttributeNumericData CurrentAttribute;
	UAttributeSetFunctionLibrary::FindAttributeDataByTag(NewSnapshot.Attributes, CurrentAttributeTag, CurrentAttribute);
	
	FAttributeNumericData MaxAttribute;
	UAttributeSetFunctionLibrary::FindAttributeDataByTag(NewSnapshot.Attributes, MaxAttributeTag, MaxAttribute);
	
	ProgressBar->SetPercent(CurrentAttribute.CurrentValue / MaxAttribute.CurrentValue);
}