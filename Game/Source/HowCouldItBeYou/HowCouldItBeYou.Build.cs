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
		// Core/HCGraphics: time of the graphics card per picture (automatic quality level on the first start)
		PrivateDependencyModuleNames.Add("RHI");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// The list of microphones (the same one the engine's voice capture uses)
			PublicSystemLibraries.Add("dsound.lib");
		}

		// Online play over the internet (docs/03 §6–7, Online/HCEos): EOS SDK + Epic's P2P net driver.
		// Epic ships SocketSubsystemEOS for Win64 only (not Linux), so Linux stays LAN/IP.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "EOSSDK", "EOSShared", "SocketSubsystemEOS" });
			PrivateDefinitions.Add("HC_WITH_EOS=1");
		}
		else
		{
			PrivateDefinitions.Add("HC_WITH_EOS=0");
		}
	}
}
