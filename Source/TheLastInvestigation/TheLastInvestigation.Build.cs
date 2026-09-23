using UnrealBuildTool;

public class TheLastInvestigation : ModuleRules
{
	public TheLastInvestigation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Slate",
			"SlateCore",
			"UMG",
			"RenderCore",
			"RHI",
			// The curtains are a generated surface, not an arrangement of boxes: a drape is a
			// curved sheet with a torn outline, and neither of those is a thing a primitive has.
			"ProceduralMeshComponent"
		});
	}
}
