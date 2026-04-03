using System.IO;
using UnrealBuildTool;

public class StormLib : ModuleRules
{
    public StormLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.CPlusPlus;
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;

        if (Target.Platform != UnrealTargetPlatform.Linux)
        {
            throw new BuildException("UnrealAzeroth currently expects the Linux StormLib package layout.");
        }

        string ThirdPartyRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "StormLib"));
        string IncludeDir = Path.Combine(ThirdPartyRoot, "include");
        string SourceLibraryDir = Path.Combine(ThirdPartyRoot, "lib", "Linux");
        string RuntimeLibraryDir = Path.Combine("$(PluginDir)", "Binaries", "ThirdParty", "StormLib", "Linux", Target.Architecture.LinuxName);
        string RuntimeLibraryName = "libstorm.so";
        string RuntimeLibraryPath = Path.Combine(RuntimeLibraryDir, RuntimeLibraryName);

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        PublicSystemIncludePaths.Add(IncludeDir);
        PrivateDependencyModuleNames.Add("Core");
        PublicAdditionalLibraries.Add(RuntimeLibraryPath);
        PublicDelayLoadDLLs.Add(RuntimeLibraryPath);
        PublicRuntimeLibraryPaths.Add(RuntimeLibraryDir);

        RuntimeDependencies.Add(Path.Combine(RuntimeLibraryDir, RuntimeLibraryName), Path.Combine(SourceLibraryDir, RuntimeLibraryName), StagedFileType.NonUFS);
        RuntimeDependencies.Add(Path.Combine(RuntimeLibraryDir, "libstorm.so.9"), Path.Combine(SourceLibraryDir, "libstorm.so.9"), StagedFileType.NonUFS);
        RuntimeDependencies.Add(Path.Combine(RuntimeLibraryDir, "libstorm.so.9.22.0"), Path.Combine(SourceLibraryDir, "libstorm.so.9.22.0"), StagedFileType.NonUFS);
        RuntimeDependencies.Add(Path.Combine(RuntimeLibraryDir, "libtomcrypt.so.1"), Path.Combine(SourceLibraryDir, "libtomcrypt.so.1"), StagedFileType.NonUFS);
        RuntimeDependencies.Add(Path.Combine(RuntimeLibraryDir, "libtomcrypt.so.1.0.1"), Path.Combine(SourceLibraryDir, "libtomcrypt.so.1.0.1"), StagedFileType.NonUFS);
        RuntimeDependencies.Add(Path.Combine(RuntimeLibraryDir, "libtommath.so.1"), Path.Combine(SourceLibraryDir, "libtommath.so.1"), StagedFileType.NonUFS);
        RuntimeDependencies.Add(Path.Combine(RuntimeLibraryDir, "libtommath.so.1.2.1"), Path.Combine(SourceLibraryDir, "libtommath.so.1.2.1"), StagedFileType.NonUFS);
    }
}
