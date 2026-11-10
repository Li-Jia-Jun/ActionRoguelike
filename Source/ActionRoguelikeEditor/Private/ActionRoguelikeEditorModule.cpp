// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

// Editor-only module. Hosts offline anim-tooling (Animation Modifiers) that writes runtime metadata onto assets.
// No custom module class is needed - the default implementation is enough.
IMPLEMENT_MODULE(FDefaultModuleImpl, ActionRoguelikeEditor);
