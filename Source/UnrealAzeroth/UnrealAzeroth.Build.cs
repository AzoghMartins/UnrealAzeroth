using UnrealBuildTool;

public class UnrealAzeroth : ModuleRules
{
    public UnrealAzeroth(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            });

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "DeveloperSettings",
                "Engine",
                "Json",
                "Projects",
                "Sockets"
            });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
    }
}
