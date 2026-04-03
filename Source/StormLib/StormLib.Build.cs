using System.IO;
using UnrealBuildTool;

public class StormLib : ModuleRules
{
    public StormLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.CPlusPlus;
        PCHUsage = PCHUsageMode.NoPCHs;

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

        if (Target.Platform != UnrealTargetPlatform.Linux)
        {
            throw new BuildException("UnrealAzeroth currently expects the Linux StormLib package layout.");
        }

        string ThirdPartyRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "StormLib"));
        string IncludeDir = Path.Combine(ThirdPartyRoot, "include");
        string LibraryDir = Path.Combine(ThirdPartyRoot, "lib", "Linux");

        PublicIncludePaths.Add(IncludeDir);
        PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, "libstorm.so"));
        PrivateRuntimeLibraryPaths.Add(LibraryDir);

        RuntimeDependencies.Add(Path.Combine(LibraryDir, "libstorm.so"));
        RuntimeDependencies.Add(Path.Combine(LibraryDir, "libstorm.so.9"));
        RuntimeDependencies.Add(Path.Combine(LibraryDir, "libstorm.so.9.22.0"));
        RuntimeDependencies.Add(Path.Combine(LibraryDir, "libtomcrypt.so.1"));
        RuntimeDependencies.Add(Path.Combine(LibraryDir, "libtomcrypt.so.1.0.1"));
        RuntimeDependencies.Add(Path.Combine(LibraryDir, "libtommath.so.1"));
        RuntimeDependencies.Add(Path.Combine(LibraryDir, "libtommath.so.1.2.1"));
    }
}
