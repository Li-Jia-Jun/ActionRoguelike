// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueAICharacter.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "UI/RogueAttributeBarWidgetComponent.h"


// Sets default values
ARogueAICharacter::ARogueAICharacter()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	ActionSystemComp = CreateDefaultSubobject<URogueActionSystemComponent>("ActionSystemComp");
	
	HealthAttributeBarWidgetComp = CreateDefaultSubobject<URogueAttributeBarWidgetComponent>("HealthBarWidgetComp");
	HealthAttributeBarWidgetComp->SetupAttachment(RootComponent);
	HealthAttributeBarWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	HealthAttributeBarWidgetComp->SetDrawSize(FVector2D(100.0f, 15.0f));
}

// Called when the game starts or when spawned
void ARogueAICharacter::BeginPlay()
{
	Super::BeginPlay();
}


