using UnrealBuildTool;

public class HowCouldItBeYouEditorTarget : TargetRules
{
	public HowCouldItBeYouEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HowCouldItBeYou");
	}
}
