// Fill out your copyright notice in the Description page of Project Settings.


#include "RoguePickableItemBase.h"
#include  "Components/TextRenderComponent.h"
#include "Components/SphereComponent.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"

TAutoConsoleVariable<bool> CVarPickableItemDebugDrawing(TEXT("game.pickable.DebugDraw"), true,
	TEXT("Enable Pickable Item debug rendering. (0 = off, 1 = enabled)"),
	ECVF_Cheat);


ARoguePickableItemBase::ARoguePickableItemBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.SetTickFunctionEnable(true);
	
	CollisionSphereComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphereComp"));
	CollisionSphereComp->SetSphereRadius(100.0f);
	CollisionSphereComp->OnComponentBeginOverlap.AddDynamic(this, &ARoguePickableItemBase::OnActorBeginOverlap);
	RootComponent = CollisionSphereComp;
	
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(CollisionSphereComp);
}

void ARoguePickableItemBase::OnActorBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
	bool bFromSweep, const FHitResult & SweepResult)
{
	// All actors with URogueActionSystemComponent can pick up
	if (auto Component = OtherActor->GetComponentByClass<URogueActionSystemComponent>(); Component)
	{
		OnPickUp(OtherActor, Component);
	}
}

void ARoguePickableItemBase::OnPickUp(AActor* Actor, URogueActionSystemComponent* ActionSystemComponent)
{
	if (!ApplyEffects(ActionSystemComponent))
	{
		UE_LOG(LogTemp, Log, TEXT("Attribute item %s could not be consumed. Skip pick up."), *this->GetName());
		return;
	}
	
	if (PickUpSound != nullptr)
	{
		UGameplayStatics::PlaySound2D(this, PickUpSound);
	}
	
	UE_LOG(LogTemp, Log, TEXT("Attribute item %s picked up by actor: %s"), *this->GetName(), *Actor->GetName());
		
	Destroy();
}

bool ARoguePickableItemBase::ApplyEffects(URogueActionSystemComponent* ActionSystemComponent)
{
	bool bReceive = false;
	for (auto Effect : Effects)
	{
		// Any successful effect being received will mark the object consumed.
		// Effect instances will be GC when this function is out of scope.
		URogueGameplayEffectInstance* EffectInstance = nullptr;
		bReceive |= ActionSystemComponent->ApplyGameplayEffectToSelf(Effect, this, EffectInstance);
	}
	
	return bReceive;
}

void ARoguePickableItemBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
#if WITH_EDITORONLY_DATA
	
	// Have the name text to face the character
	if (bool bDebugDrawEnable = CVarPickableItemDebugDrawing.GetValueOnGameThread())
	{

		if (GetDistanceTo(GetWorld()->GetFirstPlayerController()->GetPawn()) < DebugDrawDistance)
		{
			FVector ThisLocation = GetActorLocation();
			FRotator PlayerRotation = GetWorld()->GetFirstPlayerController()->GetControlRotation();
			PlayerRotation.Roll = 0.0f;
			PlayerRotation.Pitch = 0.0f;
			FVector Offset = PlayerRotation.RotateVector(FVector(0.0f, DebugOffsetHorizontal, DebugOffsetVertical));
			
			DrawDebugString(GetWorld(), ThisLocation + Offset, DebugDisplayName, nullptr, 
				FColor::Yellow, 0.0f, false, 0.8f);
		}
	}
	
#endif
}
