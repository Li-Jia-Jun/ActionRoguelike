// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Character.h"
#include "Significance/RogueSignificanceInterface.h"
#include "RogueAICharacter.generated.h"


class UWidgetComponent;
class URogueActionSystemComponent;
class URogueAttributeBarWidgetComponent;

UCLASS(Abstract)
class ACTIONROGUELIKE_API ARogueAICharacter : public ACharacter, public IRogueSignificanceInterface
{
	GENERATED_BODY()

public:
	ARogueAICharacter();

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//~ Begin IRogueSignificanceInterface
	virtual void SignificanceLODChanged(int32 NewLOD) override;
	//~ End IRogueSignificanceInterface

protected:

	UFUNCTION()
	void OnHit(FGameplayTag EventTag, FRogueGameplayEventData Payload);

	/**
	 * Tag used to register with the significance manager. Registration only happens when this Tag
	 * is listed in URogueSignificanceDeveloperSettings, so it doubles as the per-class opt-in.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Significance")
	FName SignificanceTag;

	/** Latest LOD assigned by the significance manager. */
	int32 SignificanceLOD = 0;

	void RegisterWithSignificanceManager();
	void UnregisterFromSignificanceManager();

	/** Configures URO to pick its rate from the (significance-forced) mesh LOD instead of screen size. */
	void HandleAnimUpdateRateParamsCreated(struct FAnimUpdateRateParameters* Params);
	
	/** Tracks whether we actually registered, so EndPlay only unregisters when needed. */
	bool bRegisteredWithSignificance = false;
	
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Action System")
	URogueActionSystemComponent* ActionSystemComp;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	URogueAttributeBarWidgetComponent* HealthAttributeBarWidgetComp;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HitOverlayMID;
	FTimerHandle OverlayTimerHandle;
};
