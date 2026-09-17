using UnrealBuildTool;
using System.Collections.Generic;

public class TheLastInvestigationEditorTarget : TargetRules
{
	public TheLastInvestigationEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TheLastInvestigation");
	}
}
