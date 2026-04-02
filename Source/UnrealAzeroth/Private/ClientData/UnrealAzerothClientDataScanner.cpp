#include "ClientData/UnrealAzerothClientDataScanner.h"

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Settings/UnrealAzerothSettings.h"

namespace
{
struct FUnrealAzerothResolvedPaths
{
    FString PluginBaseDir;
    FString ClientDataPath;
    FString ServerHost;
    int32 AuthServerPort = 3724;
    bool bUsedLocalBootstrapFile = false;
    FString LocalBootstrapFilePath;
};

FString NormalizeDirectoryPath(const FString& InPath)
{
    FString NormalizedPath = InPath;
    FPaths::NormalizeDirectoryName(NormalizedPath);
    return NormalizedPath;
}

FString NormalizeFilePath(const FString& InPath)
{
    FString NormalizedPath = InPath;
    FPaths::NormalizeFilename(NormalizedPath);
    return NormalizedPath;
}

FString MakeRelativeToRoot(const FString& AbsolutePath, const FString& RootPath)
{
    const FString NormalizedAbsolutePath = NormalizeFilePath(AbsolutePath);
    FString NormalizedRootPath = NormalizeDirectoryPath(RootPath);

    if (!NormalizedRootPath.IsEmpty())
    {
        const FString RootWithSlash = NormalizedRootPath / TEXT("");
        if (NormalizedAbsolutePath.StartsWith(RootWithSlash))
        {
            return NormalizedAbsolutePath.RightChop(RootWithSlash.Len());
        }

        if (NormalizedAbsolutePath == NormalizedRootPath)
        {
            return FPaths::GetCleanFilename(NormalizedAbsolutePath);
        }
    }

    FString RelativePath = NormalizedAbsolutePath;
    FPaths::MakePathRelativeTo(RelativePath, *NormalizedRootPath);
    FPaths::NormalizeFilename(RelativePath);
    return RelativePath;
}

void AddUniqueArchiveMatches(TArray<FString>& InOutFiles, const FString& RootPath, const TCHAR* Pattern)
{
    TArray<FString> Matches;
    IFileManager::Get().FindFilesRecursive(Matches, *RootPath, Pattern, true, false, false);

    for (const FString& Match : Matches)
    {
        InOutFiles.AddUnique(NormalizeFilePath(Match));
    }
}

int32 CountArchivesAtRoot(const FString& RootPath)
{
    TArray<FString> Matches;
    IFileManager::Get().FindFiles(Matches, *(RootPath / TEXT("*.mpq")), true, false);
    IFileManager::Get().FindFiles(Matches, *(RootPath / TEXT("*.MPQ")), true, false);
    return Matches.Num();
}

FString ResolveArchiveRoot(const FString& CandidateRoot, TArray<FString>& InOutMessages)
{
    FString ArchiveRoot = CandidateRoot;
    if (CountArchivesAtRoot(ArchiveRoot) > 0)
    {
        return ArchiveRoot;
    }

    const FString NestedDataRoot = NormalizeDirectoryPath(ArchiveRoot / TEXT("Data"));
    if (IFileManager::Get().DirectoryExists(*NestedDataRoot) && CountArchivesAtRoot(NestedDataRoot) > 0)
    {
        InOutMessages.Add(FString::Printf(TEXT("Using nested Data directory as archive root: %s"), *NestedDataRoot));
        return NestedDataRoot;
    }

    return ArchiveRoot;
}

void ApplyBootstrapValue(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FString& InOutValue)
{
    if (InOutValue.IsEmpty())
    {
        JsonObject->TryGetStringField(FieldName, InOutValue);
    }
}

FUnrealAzerothResolvedPaths ResolvePaths(const UUnrealAzerothSettings& Settings)
{
    FUnrealAzerothResolvedPaths ResolvedPaths;

    ResolvedPaths.ClientDataPath = NormalizeDirectoryPath(Settings.ClientDataDirectory.Path);
    ResolvedPaths.ServerHost = Settings.ServerHost;
    ResolvedPaths.AuthServerPort = Settings.AuthServerPort;

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAzeroth"));
    if (!Plugin.IsValid())
    {
        return ResolvedPaths;
    }

    ResolvedPaths.PluginBaseDir = NormalizeDirectoryPath(Plugin->GetBaseDir());
    ResolvedPaths.LocalBootstrapFilePath = ResolvedPaths.PluginBaseDir / TEXT(".unrealazeroth.local.json");

    FString BootstrapJson;
    if (!FFileHelper::LoadFileToString(BootstrapJson, *ResolvedPaths.LocalBootstrapFilePath))
    {
        return ResolvedPaths;
    }

    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BootstrapJson);
    TSharedPtr<FJsonObject> JsonObject;
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return ResolvedPaths;
    }

    ApplyBootstrapValue(JsonObject, TEXT("clientData"), ResolvedPaths.ClientDataPath);

    ResolvedPaths.ClientDataPath = NormalizeDirectoryPath(ResolvedPaths.ClientDataPath);
    ResolvedPaths.bUsedLocalBootstrapFile = true;

    return ResolvedPaths;
}
}

FUnrealAzerothClientDataScanResult FUnrealAzerothClientDataScanner::ScanConfiguredPaths()
{
    return Scan(*GetDefault<UUnrealAzerothSettings>());
}

FUnrealAzerothClientDataScanResult FUnrealAzerothClientDataScanner::Scan(const UUnrealAzerothSettings& Settings)
{
    const FUnrealAzerothResolvedPaths ResolvedPaths = ResolvePaths(Settings);

    FUnrealAzerothClientDataScanResult Result;
    Result.PluginBaseDir = ResolvedPaths.PluginBaseDir;
    Result.ResolvedClientDataPath = ResolvedPaths.ClientDataPath;
    Result.ResolvedServerHost = ResolvedPaths.ServerHost;
    Result.ResolvedAuthServerPort = ResolvedPaths.AuthServerPort;
    Result.bUsedLocalBootstrapFile = ResolvedPaths.bUsedLocalBootstrapFile;
    Result.LocalBootstrapFilePath = ResolvedPaths.LocalBootstrapFilePath;

    if (Result.bUsedLocalBootstrapFile)
    {
        Result.Messages.Add(FString::Printf(TEXT("Using local bootstrap file: %s"), *Result.LocalBootstrapFilePath));
    }

    Result.Messages.Add(TEXT("Realm/world server ports are discovered from the authserver and are not configured locally."));

    if (Result.PluginBaseDir.IsEmpty())
    {
        Result.Errors.Add(TEXT("Could not resolve the UnrealAzeroth plugin base directory."));
    }

    if (Result.ResolvedClientDataPath.IsEmpty())
    {
        Result.Errors.Add(TEXT("Client data directory is not configured."));
    }
    else
    {
        Result.bClientDataPathExists = IFileManager::Get().DirectoryExists(*Result.ResolvedClientDataPath);
        if (!Result.bClientDataPathExists)
        {
            Result.Errors.Add(FString::Printf(TEXT("Client data directory does not exist: %s"), *Result.ResolvedClientDataPath));
        }
    }

    Result.bHasServerHost = !Result.ResolvedServerHost.IsEmpty();
    if (!Result.bHasServerHost)
    {
        Result.Errors.Add(TEXT("Server IP address is not configured."));
    }

    if (Result.ResolvedAuthServerPort < 1 || Result.ResolvedAuthServerPort > 65535)
    {
        Result.Errors.Add(FString::Printf(TEXT("Auth server port is invalid: %d"), Result.ResolvedAuthServerPort));
    }

    if (!Result.bClientDataPathExists)
    {
        return Result;
    }

    Result.ResolvedClientDataPath = ResolveArchiveRoot(Result.ResolvedClientDataPath, Result.Messages);

    TArray<FString> ArchivePaths;
    AddUniqueArchiveMatches(ArchivePaths, Result.ResolvedClientDataPath, TEXT("*.mpq"));
    AddUniqueArchiveMatches(ArchivePaths, Result.ResolvedClientDataPath, TEXT("*.MPQ"));
    ArchivePaths.Sort();

    TSet<FString> UniqueLocaleDirectories;

    for (const FString& ArchivePath : ArchivePaths)
    {
        FUnrealAzerothArchiveFile ArchiveFile;
        ArchiveFile.AbsolutePath = ArchivePath;
        ArchiveFile.FileName = FPaths::GetCleanFilename(ArchivePath);
        ArchiveFile.FileSizeBytes = IFileManager::Get().FileSize(*ArchivePath);

        ArchiveFile.RelativePath = MakeRelativeToRoot(ArchivePath, Result.ResolvedClientDataPath);

        TArray<FString> PathSegments;
        ArchiveFile.RelativePath.ParseIntoArray(PathSegments, TEXT("/"), true);
        if (PathSegments.Num() > 1)
        {
            ArchiveFile.TopLevelDirectory = PathSegments[0];
            ArchiveFile.bIsInSubdirectory = true;
            UniqueLocaleDirectories.Add(ArchiveFile.TopLevelDirectory);
        }

        Result.ArchiveFiles.Add(MoveTemp(ArchiveFile));
    }

    Result.LocaleDirectories.Reserve(UniqueLocaleDirectories.Num());
    for (const FString& LocaleDirectory : UniqueLocaleDirectories)
    {
        Result.LocaleDirectories.Add(LocaleDirectory);
    }
    Result.LocaleDirectories.Sort();

    if (Result.ArchiveFiles.Num() == 0)
    {
        Result.Errors.Add(FString::Printf(TEXT("No MPQ archives were found under: %s"), *Result.ResolvedClientDataPath));
    }
    else
    {
        Result.Messages.Add(FString::Printf(TEXT("Discovered %d MPQ archive(s)."), Result.ArchiveFiles.Num()));
    }

    if (Result.LocaleDirectories.Num() > 0)
    {
        Result.Messages.Add(FString::Printf(TEXT("Detected locale/archive directories: %s"), *FString::Join(Result.LocaleDirectories, TEXT(", "))));
    }

    return Result;
}
