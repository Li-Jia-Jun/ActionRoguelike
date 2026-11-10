// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ActionRoguelikeEditor : ModuleRules
{
	public ActionRoguelikeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ActionRoguelike",        // runtime metadata class URogueClimbContactMetaData
			"AnimationModifiers",     // UAnimationModifier base class (editor-only)
			"AnimationBlueprintLibrary", // UAnimPoseExtensions / FAnimPose (component-space pose sampling)
		});
	}
}
