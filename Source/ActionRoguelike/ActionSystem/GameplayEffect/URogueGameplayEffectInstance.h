// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RogueGameplayEffect.h"
#include "RogueGameplayEffectDurationPolicy.h"
#include "UObject/Object.h"
#include "URogueGameplayEffectInstance.generated.h"

class URogueGameplayEffect;
class URogueAttributeGameplayEffect;
class URogueDebuffGameplayEffect;
class URogueActionSystemComponent;

class URogueGameplayEffectInstance;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameplayEffectInstanceFinishedSignature, URogueGameplayEffectInstance*, Target);



/**
 * Runtime instance of a gameplay effect
 *	- Use Duration Policy Instance to handle timer
 *	- Call ASC to apply and remove effect
 *	- Send notification to ASC when finished
 */
UCLASS()
class ACTIONROGUELIKE_API URogueGameplayEffectInstance : public UObject
{
	GENERATED_BODY()
		
public:
	void Init(const URogueGameplayEffect* InTemplate, URogueActionSystemComponent* InOwnerActionSystemComponent, 
		const UObject* InSender, uint8 InStackIndex = 0);
	
	float GetTimeRemaining() const;
	
	void Start();
	
	bool Matches(const URogueGameplayEffectInstance& Other) const
	{
		return Template->EffectTag.MatchesTag(Other.Template->EffectTag);
	};

	FOnGameplayEffectInstanceFinishedSignature OnFinishedDelegate;
	
	bool operator==(const URogueGameplayEffectInstance& Other) const
	{
		return Template->EffectTag.MatchesTag(Other.Template->EffectTag);
	}
	
	// Infinite apply needs other systems to terminate instance, so an explicit terminate public function
	void Terminate()
	{
		Finish();
	}

protected:
	
	UFUNCTION()
	void Apply();
	
	UFUNCTION()
	void Finish();
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<const URogueGameplayEffect> Template;
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URogueActionSystemComponent> OwnerActionSystemComponent;
	
	UPROPERTY(VisibleAnywhere)
	const UObject* Sender;
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URogueGameplayEffectDurationPolicyInstance> DurationPolicyInstance;
	
	uint8 StackIndex;

	bool bFinished = false;

	friend class URogueActionSystemComponent;
};
