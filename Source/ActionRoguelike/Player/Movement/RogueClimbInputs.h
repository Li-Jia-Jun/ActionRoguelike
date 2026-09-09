// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MoverTypes.h"
#include "RogueClimbInputs.generated.h"

/**
 * Mover input for climbing (a custom boolean as climb intent).
 *
 * NOTE: not currently used. Climbing switched to contextual entry (move-into-wall), so entry/exit is driven by
 * FCharacterDefaultInputs (move intent + jump) in URogueClimbTransition. Kept as a ready-made input block for a
 * future explicit "manual grab / hold to stay attached" feature.
 */
USTRUCT(BlueprintType)
struct ACTIONROGUELIKE_API FRogueClimbInputs : public FMoverDataStructBase
{
	GENERATED_USTRUCT_BODY()

	// True while the player is holding the climb input (hold-to-climb; releasing exits the climb).
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	bool bWantsToClimb = false;

	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override
	{
		const FRogueClimbInputs& TypedAuthority = static_cast<const FRogueClimbInputs&>(AuthorityState);
		return bWantsToClimb != TypedAuthority.bWantsToClimb;
	}

	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float LerpFactor) override
	{
		// This function is called to interpolate between two frames,
		// and we snap the boolean to the closer endpoint.
		const FRogueClimbInputs& Source = static_cast<const FRogueClimbInputs&>((LerpFactor < 0.5f) ? From : To);
		bWantsToClimb = Source.bWantsToClimb;
	}

	virtual void Merge(const FMoverDataStructBase& From) override
	{
		// This function is called to merge inputs from multiple frames (e.g. when several sim ticks between 2 rendered frames),
		// and we want to capture intent for every frame, so we OR the values.
		const FRogueClimbInputs& TypedFrom = static_cast<const FRogueClimbInputs&>(From);
		bWantsToClimb |= TypedFrom.bWantsToClimb;
	}

	virtual FMoverDataStructBase* Clone() const override
	{
		return new FRogueClimbInputs(*this);
	}

	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override
	{
		Super::NetSerialize(Ar, Map, bOutSuccess);
		Ar.SerializeBits(&bWantsToClimb, 1); // Serialize our custom boolean value
		bOutSuccess = true;
		return true;
	}

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }

	virtual void ToString(FAnsiStringBuilderBase& Out) const override
	{
		Super::ToString(Out);
		Out.Appendf("bWantsToClimb: %i\n", bWantsToClimb);
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Super::AddReferencedObjects(Collector); }
};

// Override Ops Type Traits class to define custom struct behaviors
template<>
struct TStructOpsTypeTraits< FRogueClimbInputs > : public TStructOpsTypeTraitsBase2< FRogueClimbInputs >
{
	enum
	{
		WithNetSerializer = true,	// When serializing, call the derived NetSerialize() instead of the default one
		WithCopy = true				// When copying, use "=" instead of memcpy()
	};
};
