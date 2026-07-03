// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueAICharacter.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "UI/RogueAttributeBarWidgetComponent.h"
#include "RogueSharedGameplayTags.h"
#include "Core/RogueGameInstance.h"


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

void ARogueAICharacter::BeginPlay()
{
	Super::BeginPlay();
	
	ActionSystemComp->GameplayEventReceivedDelegate.AddDynamic(this, &ThisClass::OnHit);
	
	// Setup overlay effect
	HitOverlayMID = UMaterialInstanceDynamic::Create(GetMesh()->GetOverlayMaterial(), this);
	GetMesh()->SetOverlayMaterial(HitOverlayMID);
	GetMesh()->SetOverlayMaterialMaxDrawDistance(1);
	
	GetGameInstance<URogueGameInstance>()->AliveAIEnemies.Add(this);
}

void ARogueAICharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	
	GetGameInstance<URogueGameInstance>()->AliveAIEnemies.RemoveSingleSwap(this);
}

void ARogueAICharacter::OnHit(FGameplayTag EventTag, FRogueGameplayEventData Payload)
{
	if (!EventTag.MatchesTag(RogueSharedGameplayTags::Event_OnHit))
	{
		return;
	}
	
	GetMesh()->SetOverlayMaterialMaxDrawDistance(0);
	HitOverlayMID->SetScalarParameterValue(FName("HitTime"), GetWorld()->TimeSeconds);

	GetWorldTimerManager().SetTimer(OverlayTimerHandle, [this]()
	{
		GetMesh()->SetOverlayMaterialMaxDrawDistance(1);
	}, 1.0f, false);
}


