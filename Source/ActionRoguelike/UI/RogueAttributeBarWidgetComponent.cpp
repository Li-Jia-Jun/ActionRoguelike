// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueAttributeBarWidgetComponent.h"
#include "RogueAttributeBarWidget.h"
#include "ActionSystem/RogueActionSystemComponent.h"


void URogueAttributeBarWidgetComponent::BeginPlay()
{
	Super::BeginPlay();
	
	URogueAttributeBarWidget* AttributeBarWidget = Cast<URogueAttributeBarWidget>(GetUserWidgetObject());
	if (!AttributeBarWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("%s does not have AttributeBarWidget assigned to AttributeBarWidgetComponent."), 
			*GetOwner()->GetName());
		return;
	}

	URogueActionSystemComponent* ASC = GetOwner()->FindComponentByClass<URogueActionSystemComponent>();
	if (!ASC)
	{
		UE_LOG(LogTemp, Error, TEXT("%s does not have an ActionSystemComponent. AttributeBarWidgetComponent will not function."), 
			*GetOwner()->GetName());
		return;
	}
	
	AttributeBarWidget->InitializeWithASC(ASC, CurrentAttributeTag, MaxAttributeTag);
	// AttributeBarWidget->AddToViewport();
}

