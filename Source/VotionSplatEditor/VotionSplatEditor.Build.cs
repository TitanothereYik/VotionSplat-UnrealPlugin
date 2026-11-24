using UnrealBuildTool;

public class VotionSplatEditor : ModuleRules
{
	public VotionSplatEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "UnrealEd", "AssetTools", "AssetRegistry",
			"Slate", "SlateCore", "VotionSplat", "EditorFramework"
		});
	}
}
