// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MoverTypes.h"
#include "RogueTraversalInputs.generated.h"

/**
 * FRogueTraversalInputs: one unified per-frame Mover input block for the project's climb/traversal actions.
 *
 * One struct, one bool per discrete traversal button - mantle-down today; a later dash / corner-turn just adds a
 * field here rather than a whole new input struct (the engine's own FCharacterDefaultInputs bundles move + jump +
 * orientation the same way). Populated in ASPlayerCharacter::ProduceInput from the game-thread edge flags, consumed
 * by the traversal transitions (e.g. URogueMantleDownTransition). Implements the FMoverDataStructBase contract
 * (Merge / Interpolate / NetSerialize) so it rides the InputCmd alongside the stock inputs.
 */
USTRUCT(BlueprintType)
struct ACTIONROGUELIKE_API FRogueTraversalInputs : public FMoverDataStructBase
{
	GENERATED_USTRUCT_BODY()

	// One-frame edge: the player tapped the mantle-down button this frame. Read by URogueMantleDownTransition.
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	bool bWantsToMantleDown = false;

	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override
	{
		const FRogueTraversalInputs& TypedAuthority = static_cast<const FRogueTraversalInputs&>(AuthorityState);
		return bWantsToMantleDown != TypedAuthority.bWantsToMantleDown;
	}

	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float LerpFactor) override
	{
		// Buttons don't interpolate: snap to the closer endpoint.
		const FRogueTraversalInputs& Source = static_cast<const FRogueTraversalInputs&>((LerpFactor < 0.5f) ? From : To);
		bWantsToMantleDown = Source.bWantsToMantleDown;
	}

	virtual void Merge(const FMoverDataStructBase& From) override
	{
		// Several sim ticks between two rendered frames: OR so a one-frame press isn't dropped.
		const FRogueTraversalInputs& TypedFrom = static_cast<const FRogueTraversalInputs&>(From);
		bWantsToMantleDown |= TypedFrom.bWantsToMantleDown;
	}

	virtual FMoverDataStructBase* Clone() const override
	{
		return new FRogueTraversalInputs(*this);
	}

	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override
	{
		Super::NetSerialize(Ar, Map, bOutSuccess);
		Ar.SerializeBits(&bWantsToMantleDown, 1);
		bOutSuccess = true;
		return true;
	}

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }

	virtual void ToString(FAnsiStringBuilderBase& Out) const override
	{
		Super::ToString(Out);
		Out.Appendf("bWantsToMantleDown: %i\n", bWantsToMantleDown);
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Super::AddReferencedObjects(Collector); }
};

// Override Ops Type Traits class to define custom struct behaviors
template<>
struct TStructOpsTypeTraits< FRogueTraversalInputs > : public TStructOpsTypeTraitsBase2< FRogueTraversalInputs >
{
	enum
	{
		WithNetSerializer = true,	// When serializing, call the derived NetSerialize() instead of the default one
		WithCopy = true				// When copying, use "=" instead of memcpy()
	};
};
