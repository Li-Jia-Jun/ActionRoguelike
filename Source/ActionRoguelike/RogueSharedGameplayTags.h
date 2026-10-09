#pragma once

#include "NativeGameplayTags.h"

namespace RogueSharedGameplayTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attribute_CurrentHealth);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attribute_MaxHealth);
	
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_OnHit);

	// Climb-family movement-mode tags - the climb analog of Mover's own Mover.IsOnGround / Mover.IsInAir. Each climb mode
	// carries exactly one CHILD tag; query the PARENT non-exact for "any climb mode" (URogueCharacterMoverComponent::
	// IsInClimbState), or a child exact for one mode (IsClimbing / IsMantling / IsMantlingDown / IsCornerTurning).
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mover_Climb);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mover_Climb_Sustained);   // sustained climb (URogueClimbMode)
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mover_Climb_MantleUp);    // top-out (URogueMantleMode)
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mover_Climb_MantleDown);  // climb down over an edge (URogueMantleDownMode)
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mover_Climb_CornerTurn);  // inner/outer corner turn (URogueCornerTurnMode)
}