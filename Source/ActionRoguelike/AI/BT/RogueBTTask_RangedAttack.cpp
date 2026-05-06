// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueBTTask_RangedAttack.h"
#include "RogueGameTypes.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Character.h"
#include "Projectile/RogueProjectileBase.h"

URogueBTTask_RangedAttack::URogueBTTask_RangedAttack()
{
	TargetActorKey.SelectedKeyName = NAME_TargetActor;
}

EBTNodeResult::Type URogueBTTask_RangedAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ACharacter* ThisCharacter = Cast<ACharacter>(OwnerComp.GetAIOwner()->GetPawn());
	check(ThisCharacter);
	
	AActor* TargetActor = Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(TargetActorKey.SelectedKeyName));
	if (!IsValid(TargetActor))
	{
		return EBTNodeResult::Failed;
	}
	
	// Spawn a projectile from owner muzzle to the target actor
	FVector SpawnLocation = ThisCharacter->GetMesh()->GetSocketLocation(MuzzleSocketName);
	
	FRotator SpawnRotation = (TargetActor->GetActorLocation() - ThisCharacter->GetActorLocation()).Rotation();
	
	// Bullet spread
	SpawnRotation.Yaw += FMath::FRandRange(-BulletSpread, BulletSpread);
	SpawnRotation.Pitch += FMath::FRandRange(0, BulletSpread);
	
	FActorSpawnParameters SpawnParams;
	SpawnParams.Instigator = ThisCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(ProjectileClass, SpawnLocation, SpawnRotation, SpawnParams);
	
	return SpawnedActor? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
