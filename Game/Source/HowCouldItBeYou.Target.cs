using UnrealBuildTool;

public class HowCouldItBeYouTarget : TargetRules
{
	public HowCouldItBeYouTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HowCouldItBeYou");
	}
}
