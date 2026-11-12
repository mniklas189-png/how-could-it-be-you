using UnrealBuildTool;

public class HowCouldItBeYou : ModuleRules
{
	public HowCouldItBeYou(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"ApplicationCore",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"DeveloperSettings",
			"EngineSettings",
			"NetCore",
			"UMG",
			"Slate",
			"SlateCore",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"Sockets",
			// Settings menu: microphone and headset choice, microphone level
			"Voice",
			"AudioMixer",
			"AudioMixerCore"
		});

		// HUD: the engine's white texture for soft-edged shapes on the canvas
		PrivateDependencyModuleNames.Add("RenderCore");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// The list of microphones (the same one the engine's voice capture uses)
			PublicSystemLibraries.Add("dsound.lib");
		}
	}
}
