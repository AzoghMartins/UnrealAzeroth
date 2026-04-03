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
                "Engine",
                "ProceduralMeshComponent"
            });

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "DeveloperSettings",
                "Engine",
                "Json",
                "Projects",
                "Sockets",
                "StormLib"
            });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
    }
}
