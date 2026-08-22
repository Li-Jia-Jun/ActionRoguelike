// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueAttributeSet.h"
#include "RogueAttributeRelationship.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffectModifier.h"


void URogueAttributeSet::InitByTemplate(URogueAttributeSetTemplate* InTemplate)
{
	Template = InTemplate;
	Attributes = InTemplate->Attributes;
	Debuffs = TArray<FAttributeDebuffData>();
	AttributeRelationships = InTemplate->AttributeRelationships;
	
	RecalculateAttributes();
}

FRogueAttributeSetSnapshot URogueAttributeSet::TakeSnapshot() const
{
	FRogueAttributeSetSnapshot Snapshot;
	Snapshot.Attributes = Attributes;
	Snapshot.Debuffs = Debuffs;
	return Snapshot;
}


bool URogueAttributeSet::CanAffordModifiers(const TArray<FRogueGameplayEffectModifier> &InModifiers, FString& FailMessage) const
{
	// Make a copy for calculation
	TArray<FAttributeNumericData> AttributesCopy = Attributes;
	
	// Apply modifiers to current values
	for (auto& Modifier : InModifiers)
	{
		for (int i = 0; i < AttributesCopy.Num(); i++)
		{
			FAttributeNumericData& Attribute = AttributesCopy[i];
			if (Modifier.TargetAttribute.MatchesTag(Attribute.Tag))
			{
				ApplyModifierToAttribute<&FAttributeNumericData::CurrentValue>(Modifier, Attribute);
				
				if (Attribute.CurrentValue < 0)
				{
					FailMessage.Append("Not enough " + Attribute.Tag.ToString());
					return false;
				}
				
				// Modifiers not changing value also be treated as non-affordable
				if (FMath::IsNearlyEqual(Attributes[i].BaseValue, Attribute.CurrentValue))
				{
					return false;
				}
				
				break;
			}
		}
	}
	
	// Check if relationships are still valid
	for (const FRogueAttributeRelationship Relationship : AttributeRelationships)
	{
		if (!Relationship.bIsAffordableTest)
		{
			continue;
		}
		
		auto Attribute01 = AttributesCopy.FindByPredicate([Relationship](auto Attr) {return Attr.Tag.MatchesTag(Relationship.Attribute01);});
		auto Attribute02 = AttributesCopy.FindByPredicate([Relationship](auto Attr) {return Attr.Tag.MatchesTag(Relationship.Attribute02);});	
		if (Attribute01 && Attribute02)
		{
			switch (Relationship.eRelation)
			{
			case EAttributeSetRelation::eNotGreaterThan:
			case EAttributeSetRelation::eLessThan:
				if (Attribute01->CurrentValue > Attribute02->CurrentValue)
				{
					FailMessage.Append("Broken relationship: " + Attribute01->Tag.ToString() + " is greater than " + Attribute02->Tag.ToString());	
					return false;
				}
				break;
			case EAttributeSetRelation::eNotLessThan:
			case EAttributeSetRelation::eGreaterThan:
				if (Attribute01->CurrentValue < Attribute02->CurrentValue)
				{
					FailMessage.Append("Broken relationship: " + Attribute01->Tag.ToString() + " is less than " + Attribute02->Tag.ToString());
					return false;
				}
				break;
			}
		}
	}
	
	return true;
}

void URogueAttributeSet::ApplyModifiers(const TArray<FRogueGameplayEffectModifier> &InModifiers)
{
	for (auto& Modifier : InModifiers)
	{
		for (auto& Attribute : Attributes)
		{
			if (Modifier.TargetAttribute.MatchesTag(Attribute.Tag))
			{
				ApplyModifierToAttribute<&FAttributeNumericData::BaseValue>(Modifier, Attribute);
				Attribute.BaseValue = FMath::Max(Attribute.BaseValue, Attribute.MinValue);
				break;
			}
		}
	}
	
	RecalculateAttributes();
}

void URogueAttributeSet::ApplyDebuffs(const TArray<FAttributeDebuffData> &InDebuffs)
{
	Debuffs.Append(InDebuffs);
	RecalculateAttributes();
}

void URogueAttributeSet::RemoveDebuffs(const TArray<FAttributeDebuffData> &InDebuffs)
{
	Debuffs.RemoveAll([&](const FAttributeDebuffData& Data) 
	{
		return InDebuffs.Contains(Data);
	});
	
	RecalculateAttributes();
}

void URogueAttributeSet::RemoveDebuffsByTag(const FGameplayTag Tag)
{
	TArray<FAttributeDebuffData> DebuffToRemove;
	for (auto& Debuff : Debuffs)
	{
		if (Debuff.Tag.MatchesTag(Tag))
		{
			DebuffToRemove.Add(Debuff);
		}
	}
	
	RemoveDebuffs(DebuffToRemove);
}

void URogueAttributeSet::RecalculateAttributes()
{
	// Recalculate attribute current values by debuffs and relationships
	
	// 1. Reset current value to base value
	for (auto& AttributeItem : Attributes)
	{
		AttributeItem.CurrentValue = AttributeItem.BaseValue;
	}
	
	// 2. Apply debuff modifiers to current values
	for (auto& Debuff : Debuffs)
	{
		for (auto& Modifier : Debuff.Modifiers)
		{
			for (auto& Attribute : Attributes)
			{
				if (!Modifier.TargetAttribute.MatchesTag(Attribute.Tag))
					continue;
				
				ApplyModifierToAttribute<&FAttributeNumericData::CurrentValue>(Modifier, Attribute);
			}
		}
	}
	
	// 3. Apply relationship rules to current values
	for (const FRogueAttributeRelationship Relationship : AttributeRelationships)
	{
		auto Attribute01 = Attributes.FindByPredicate([Relationship](auto Attr) {return Attr.Tag.MatchesTag(Relationship.Attribute01);});
		auto Attribute02 = Attributes.FindByPredicate([Relationship](auto Attr) {return Attr.Tag.MatchesTag(Relationship.Attribute02);});
		if (Attribute01 && Attribute02)
		{
			ApplyRelationshipsToAttribute(Relationship, *Attribute01, *Attribute02);
		}
	}

	// 4. Clamp current values to their minimums
	for (auto& AttributeItem : Attributes)
	{
		AttributeItem.CurrentValue = FMath::Max(AttributeItem.CurrentValue, AttributeItem.MinValue);
	}
}

template <float FAttributeNumericData::* Value>
void URogueAttributeSet::ApplyModifierToAttribute(const FRogueGameplayEffectModifier& Modifier, 
	FAttributeNumericData& Attribute) const
{
	if (Modifier.Type == ModifierType::eFlatValue)
	{
		switch (Modifier.FlatValueOp)
		{
		case ERogueAttributeModifierOp::eAdd:
			Attribute.*Value += Modifier.FlatValue;
			break;
		case ERogueAttributeModifierOp::eSubtract:
			Attribute.*Value -= Modifier.FlatValue;
			break;
		case ERogueAttributeModifierOp::eMultiply:
			Attribute.*Value *= Modifier.FlatValue;
			break;
		default:
			break;
		}
	}
	else if (Modifier.Type == ModifierType::eOtherAttributeMagnitude)
	{
		FAttributeNumericData OtherAttribute;
		if (UAttributeSetFunctionLibrary::FindAttributeDataByTag(Attributes, Modifier.OtherAttributeTag, OtherAttribute))
		{
			Attribute.*Value = OtherAttribute.CurrentValue * Modifier.OtherAttributeMagnitude;
		}
	}
}

void URogueAttributeSet::ApplyRelationshipsToAttribute(const FRogueAttributeRelationship& Relationship, 
	FAttributeNumericData& Attribute01, FAttributeNumericData& Attribute02) const
{
	// Both base and current value of Attribute01 should be ruled by Attribute02 current
	switch (Relationship.eRelation)
	{
	case EAttributeSetRelation::eNotGreaterThan:
	case EAttributeSetRelation::eLessThan:
		Attribute01.BaseValue = FMath::Min(Attribute01.BaseValue, Attribute02.CurrentValue);
		Attribute01.CurrentValue = FMath::Min(Attribute01.CurrentValue, Attribute02.CurrentValue);
		break;
	case EAttributeSetRelation::eNotLessThan:
	case EAttributeSetRelation::eGreaterThan:
		Attribute01.BaseValue = FMath::Min(Attribute01.BaseValue, Attribute02.CurrentValue);
		Attribute01.CurrentValue = FMath::Max(Attribute01.CurrentValue, Attribute02.CurrentValue);
		break;	
	default:
		break;
	}
}