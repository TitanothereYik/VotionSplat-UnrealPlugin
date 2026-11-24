using System.IO;
using UnrealBuildTool;

public class VotionSplat : ModuleRules
{
	public VotionSplat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[] { "RenderCore", "RHI", "Renderer", "Projects" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("EditorFramework");
		}

		string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source", "Runtime", "Renderer", "Private"));
		PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source", "Runtime", "Renderer", "Internal"));

		// The LUT is staged once tools/unreal/bake_inverse_view_lut.py has written it.
		string Lut = Path.Combine(PluginDirectory, "Resources", "VotionSplat_InverseView.bin");
		if (File.Exists(Lut))
		{
			RuntimeDependencies.Add("$(PluginDir)/Resources/VotionSplat_InverseView.bin", StagedFileType.NonUFS);
		}
	}
}
