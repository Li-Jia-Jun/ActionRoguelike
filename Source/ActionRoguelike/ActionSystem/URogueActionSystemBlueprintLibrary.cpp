// Fill out your copyright notice in the Description page of Project Settings.


#include "URogueActionSystemBlueprintLibrary.h"
#include "ActionSystem/RogueActionSystemComponent.h"


void UURogueActionSystemBlueprintLibrary::SendGameplayEventToActor(AActor* TargetActor, FGameplayTag EventTag, const FRogueGameplayEventData& Payload)
{
	if (URogueActionSystemComponent* ASC = TargetActor->GetComponentByClass<URogueActionSystemComponent>())
	{
		ASC->HandleGameplayEvent(EventTag, Payload);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s does not have a ASC. SendGameplayEventToActor() fails."), *TargetActor->GetName());
	}
}