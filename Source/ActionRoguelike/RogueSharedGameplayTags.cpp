#include "RogueSharedGameplayTags.h"

namespace RogueSharedGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Attribute_CurrentHealth, "Attribute.CurrentHealth");
	UE_DEFINE_GAMEPLAY_TAG(Attribute_MaxHealth, "Attribute.MaxHealth");
	
	UE_DEFINE_GAMEPLAY_TAG(Event_OnHit, "GameplayEvent.OnHit");

	UE_DEFINE_GAMEPLAY_TAG(Mover_Climb, "Mover.Climb");
	UE_DEFINE_GAMEPLAY_TAG(Mover_Climb_Sustained, "Mover.Climb.Sustained");
	UE_DEFINE_GAMEPLAY_TAG(Mover_Climb_MantleUp, "Mover.Climb.MantleUp");
	UE_DEFINE_GAMEPLAY_TAG(Mover_Climb_MantleDown, "Mover.Climb.MantleDown");
	UE_DEFINE_GAMEPLAY_TAG(Mover_Climb_CornerTurn, "Mover.Climb.CornerTurn");
}