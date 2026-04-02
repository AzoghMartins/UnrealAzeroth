using UnrealBuildTool;

public class UnrealAzeroth : ModuleRules
{
    public UnrealAzeroth(ReadOnlyTargetRules Target) : base(Target)
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
                "DeveloperSettings",
                "Engine",
                "Json",
                "Projects"
            });
    }
}
