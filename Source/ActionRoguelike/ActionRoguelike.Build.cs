// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ActionRoguelike : ModuleRules
{
	public ActionRoguelike(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[] { "ActionRoguelike" }); // So include path don't have to include ActionRoguelike everytime

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "GameplayTags", "UMG", "AIModule", "NavigationSystem", "SignificanceManager", "Mover", "MotionWarping" });

		PrivateDependencyModuleNames.AddRange(new string[] { "EnhancedInput", "Niagara", "DeveloperSettings"});

		// Climb limb IK: the custom Control Rig unit (FRigUnit_RogueClimbLimbIK) and the engine's two-bone solve it builds on.
		PrivateDependencyModuleNames.AddRange(new string[] { "ControlRig", "RigVM", "AnimationCore" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
