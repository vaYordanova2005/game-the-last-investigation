using UnrealBuildTool;
using System.Collections.Generic;

public class TheLastInvestigationTarget : TargetRules
{
	public TheLastInvestigationTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TheLastInvestigation");
	}
}
