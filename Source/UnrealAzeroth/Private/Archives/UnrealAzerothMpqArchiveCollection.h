#pragma once

#include "CoreMinimal.h"
#include "World/UnrealAzerothWorldTypes.h"

struct FStormArchiveCollectionHandle;
struct FStormReadFileResult;

struct FUnrealAzerothMpqFileReadResult
{
    bool bSuccess = false;
    FString VirtualPath;
    FString ArchiveAbsolutePath;
    FString ArchiveRelativePath;
    TArray<uint8> Bytes;
    FString ErrorMessage;
};

class FUnrealAzerothMpqArchiveCollection
{
public:
    static FUnrealAzerothMpqArchiveCollection& Get();

    ~FUnrealAzerothMpqArchiveCollection();

    bool ReadFile(
        const FString& ClientDataPath,
        EUnrealAzerothArchivePreference ArchivePreference,
        const FString& VirtualPath,
        FUnrealAzerothMpqFileReadResult& OutResult);
    bool FindFilesInDirectory(
        const FString& ClientDataPath,
        EUnrealAzerothArchivePreference ArchivePreference,
        const FString& DirectoryVirtualPath,
        const FString& RequiredExtension,
        TArray<FString>& OutVirtualPaths,
        FString& OutErrorMessage);
    void Reset();

private:
    struct FMountedArchive
    {
        FString AbsolutePath;
        FString RelativePath;
        int32 Priority = 0;
    };

    bool EnsureMounted(
        const FString& ClientDataPath,
        EUnrealAzerothArchivePreference ArchivePreference,
        FString& OutErrorMessage);
    bool TryReadFileFromMountedArchives(const FString& VirtualPath, FStormReadFileResult& OutStormReadResult) const;
    void BuildCanonicalVirtualPathIndex();
    void Unmount();

    static FString NormalizeDirectoryPath(const FString& InPath);
    static FString NormalizeFilePath(const FString& InPath);
    static FString NormalizeVirtualPath(const FString& InPath);
    static FString ResolveArchiveRoot(const FString& ClientDataPath);
    static void AddUniqueArchiveMatches(TArray<FString>& InOutArchivePaths, const FString& RootPath, const TCHAR* Pattern);
    static int32 CountArchivesAtRoot(const FString& RootPath);
    static int32 ComputeArchivePriority(const FString& ArchiveRoot, const FString& ArchivePath);
    static bool ShouldIncludeArchive(const FString& ArchivePath, EUnrealAzerothArchivePreference ArchivePreference);

    FString MountedArchiveRoot;
    EUnrealAzerothArchivePreference MountedArchivePreference = EUnrealAzerothArchivePreference::PatchedPreferred;
    TArray<FMountedArchive> MountedArchives;
    TMap<FString, FString> CanonicalVirtualPathMap;
    FStormArchiveCollectionHandle* Handle = nullptr;
};
