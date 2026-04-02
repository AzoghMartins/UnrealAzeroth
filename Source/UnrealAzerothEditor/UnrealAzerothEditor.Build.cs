using UnrealBuildTool;

public class UnrealAzerothEditor : ModuleRules
{
    public UnrealAzerothEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core"
            });

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "CoreUObject",
                "Engine",
                "UnrealAzeroth",
                "UnrealEd"
            });
    }
}
