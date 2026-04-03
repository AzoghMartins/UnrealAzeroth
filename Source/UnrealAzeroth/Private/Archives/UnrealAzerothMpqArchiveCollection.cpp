#include "Archives/UnrealAzerothMpqArchiveCollection.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "StormLibBridge.h"

namespace
{
FString MakeRelativeToArchiveRoot(const FString& AbsolutePath, const FString& RootPath)
{
    FString RelativePath = AbsolutePath;
    FPaths::MakePathRelativeTo(RelativePath, *RootPath);
    FPaths::NormalizeFilename(RelativePath);
    return RelativePath;
}

int32 ComputePatchSuffixScore(const FString& LowerRelativePath)
{
    const FString CleanName = FPaths::GetBaseFilename(LowerRelativePath);
    TArray<FString> Tokens;
    CleanName.ParseIntoArray(Tokens, TEXT("-"), true);
    if (Tokens.Num() == 0)
    {
        return 0;
    }

    const FString LastToken = Tokens.Last();
    if (LastToken.IsNumeric())
    {
        return FCString::Atoi(*LastToken) * 10;
    }

    if (LastToken.Len() == 1)
    {
        return static_cast<int32>(FChar::ToUpper(LastToken[0]));
    }

    return 0;
}

FString ToBackslashPath(const FString& InPath)
{
    FString Result = InPath;
    Result.ReplaceInline(TEXT("/"), TEXT("\\"));
    return Result;
}
}

FUnrealAzerothMpqArchiveCollection& FUnrealAzerothMpqArchiveCollection::Get()
{
    static FUnrealAzerothMpqArchiveCollection Instance;
    return Instance;
}

FUnrealAzerothMpqArchiveCollection::~FUnrealAzerothMpqArchiveCollection()
{
    Unmount();
}

bool FUnrealAzerothMpqArchiveCollection::ReadFile(const FString& ClientDataPath, const FString& VirtualPath, FUnrealAzerothMpqFileReadResult& OutResult)
{
    OutResult = FUnrealAzerothMpqFileReadResult{};
    OutResult.VirtualPath = NormalizeVirtualPath(VirtualPath);

    FString ErrorMessage;
    if (!EnsureMounted(ClientDataPath, ErrorMessage))
    {
        OutResult.ErrorMessage = MoveTemp(ErrorMessage);
        return false;
    }

    FStormReadFileResult StormReadResult;
    if (!TryReadFileFromMountedArchives(VirtualPath, StormReadResult))
    {
        OutResult.ErrorMessage = FString::Printf(
            TEXT("The file '%s' was not found in the configured MPQ archives."),
            *OutResult.VirtualPath);
        return false;
    }

    OutResult.Bytes.SetNumUninitialized(static_cast<int32>(StormReadResult.Size));
    if (StormReadResult.Size > 0)
    {
        FMemory::Memcpy(OutResult.Bytes.GetData(), StormReadResult.Bytes, StormReadResult.Size);
    }

    OutResult.bSuccess = true;
    OutResult.ArchiveAbsolutePath = UTF8_TO_TCHAR(StormReadResult.ArchivePath);
    OutResult.ArchiveRelativePath = MakeRelativeToArchiveRoot(OutResult.ArchiveAbsolutePath, MountedArchiveRoot);

    StormLibBridge_FreeReadFileResult(&StormReadResult);
    return true;
}

void FUnrealAzerothMpqArchiveCollection::Reset()
{
    Unmount();
}

bool FUnrealAzerothMpqArchiveCollection::EnsureMounted(const FString& ClientDataPath, FString& OutErrorMessage)
{
    const FString ArchiveRoot = ResolveArchiveRoot(ClientDataPath);
    if (ArchiveRoot.IsEmpty())
    {
        OutErrorMessage = TEXT("Client data directory is not configured.");
        return false;
    }

    if (MountedArchiveRoot == ArchiveRoot && MountedArchives.Num() > 0)
    {
        return true;
    }

    Unmount();

    if (!IFileManager::Get().DirectoryExists(*ArchiveRoot))
    {
        OutErrorMessage = FString::Printf(TEXT("Client data directory does not exist: %s"), *ArchiveRoot);
        return false;
    }

    TArray<FString> ArchivePaths;
    AddUniqueArchiveMatches(ArchivePaths, ArchiveRoot, TEXT("*.mpq"));
    AddUniqueArchiveMatches(ArchivePaths, ArchiveRoot, TEXT("*.MPQ"));

    if (ArchivePaths.Num() == 0)
    {
        OutErrorMessage = FString::Printf(TEXT("No MPQ archives were found under: %s"), *ArchiveRoot);
        return false;
    }

    ArchivePaths.Sort([&ArchiveRoot](const FString& Left, const FString& Right)
    {
        const int32 LeftPriority = ComputeArchivePriority(ArchiveRoot, Left);
        const int32 RightPriority = ComputeArchivePriority(ArchiveRoot, Right);
        if (LeftPriority != RightPriority)
        {
            return LeftPriority > RightPriority;
        }

        return Left.ToLower() > Right.ToLower();
    });

    for (const FString& ArchivePath : ArchivePaths)
    {
        FMountedArchive& MountedArchive = MountedArchives.AddDefaulted_GetRef();
        MountedArchive.AbsolutePath = ArchivePath;
        MountedArchive.RelativePath = MakeRelativeToArchiveRoot(ArchivePath, ArchiveRoot);
        MountedArchive.Priority = ComputeArchivePriority(ArchiveRoot, ArchivePath);
    }

    if (MountedArchives.Num() == 0)
    {
        OutErrorMessage = FString::Printf(TEXT("Failed to open any MPQ archives under: %s"), *ArchiveRoot);
        return false;
    }

    TArray<FTCHARToUTF8> Utf8ArchivePaths;
    TArray<const char*> ArchivePathPointers;
    Utf8ArchivePaths.Reserve(MountedArchives.Num());
    ArchivePathPointers.Reserve(MountedArchives.Num());

    for (const FMountedArchive& MountedArchive : MountedArchives)
    {
        FTCHARToUTF8& Converter = Utf8ArchivePaths.Emplace_GetRef(*MountedArchive.AbsolutePath);
        ArchivePathPointers.Add(Converter.Get());
    }

    if (!StormLibBridge_OpenArchiveCollection(ArchivePathPointers.GetData(), ArchivePathPointers.Num(), &Handle))
    {
        MountedArchives.Reset();
        OutErrorMessage = FString::Printf(TEXT("Failed to open any MPQ archives under: %s"), *ArchiveRoot);
        return false;
    }

    MountedArchiveRoot = ArchiveRoot;
    BuildCanonicalVirtualPathIndex();
    return true;
}

bool FUnrealAzerothMpqArchiveCollection::TryReadFileFromMountedArchives(const FString& VirtualPath, FStormReadFileResult& OutStormReadResult) const
{
    TArray<FString> CandidatePaths;

    const FString TrimmedPath = VirtualPath.TrimStartAndEnd();
    const FString NormalizedPath = NormalizeVirtualPath(TrimmedPath);

    if (const FString* CanonicalPath = CanonicalVirtualPathMap.Find(NormalizedPath))
    {
        CandidatePaths.Add(*CanonicalPath);
    }

    if (!TrimmedPath.IsEmpty())
    {
        CandidatePaths.AddUnique(TrimmedPath);
        CandidatePaths.AddUnique(ToBackslashPath(TrimmedPath));
    }

    if (!NormalizedPath.IsEmpty())
    {
        CandidatePaths.AddUnique(NormalizedPath);
        CandidatePaths.AddUnique(ToBackslashPath(NormalizedPath));
    }

    for (const FString& CandidatePath : CandidatePaths)
    {
        FTCHARToUTF8 CandidatePathUtf8(*CandidatePath);
        if (StormLibBridge_ReadFile(Handle, CandidatePathUtf8.Get(), &OutStormReadResult))
        {
            return true;
        }
    }

    return false;
}

void FUnrealAzerothMpqArchiveCollection::Unmount()
{
    if (Handle != nullptr)
    {
        StormLibBridge_CloseArchiveCollection(Handle);
        Handle = nullptr;
    }

    MountedArchives.Reset();
    MountedArchiveRoot.Reset();
    CanonicalVirtualPathMap.Reset();
}

FString FUnrealAzerothMpqArchiveCollection::NormalizeDirectoryPath(const FString& InPath)
{
    FString NormalizedPath = InPath;
    FPaths::NormalizeDirectoryName(NormalizedPath);
    return NormalizedPath;
}

FString FUnrealAzerothMpqArchiveCollection::NormalizeFilePath(const FString& InPath)
{
    FString NormalizedPath = InPath;
    FPaths::NormalizeFilename(NormalizedPath);
    return NormalizedPath;
}

FString FUnrealAzerothMpqArchiveCollection::NormalizeVirtualPath(const FString& InPath)
{
    FString NormalizedPath = InPath;
    NormalizedPath.TrimStartAndEndInline();
    NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
    return NormalizedPath.ToLower();
}

FString FUnrealAzerothMpqArchiveCollection::ResolveArchiveRoot(const FString& ClientDataPath)
{
    FString ArchiveRoot = NormalizeDirectoryPath(ClientDataPath);
    if (CountArchivesAtRoot(ArchiveRoot) > 0)
    {
        return ArchiveRoot;
    }

    const FString NestedDataRoot = NormalizeDirectoryPath(ArchiveRoot / TEXT("Data"));
    if (IFileManager::Get().DirectoryExists(*NestedDataRoot) && CountArchivesAtRoot(NestedDataRoot) > 0)
    {
        return NestedDataRoot;
    }

    return ArchiveRoot;
}

void FUnrealAzerothMpqArchiveCollection::BuildCanonicalVirtualPathIndex()
{
    CanonicalVirtualPathMap.Reset();

    if (Handle == nullptr)
    {
        return;
    }

    FStormReadFileResult StormReadResult;
    if (!StormLibBridge_ReadFile(Handle, "(listfile)", &StormReadResult))
    {
        return;
    }

    const FUTF8ToTCHAR ListfileText(reinterpret_cast<const UTF8CHAR*>(StormReadResult.Bytes), static_cast<int32>(StormReadResult.Size));
    FString ListfileString(ListfileText.Length(), ListfileText.Get());

    TArray<FString> Lines;
    ListfileString.ParseIntoArrayLines(Lines, false);
    for (FString& Line : Lines)
    {
        Line.TrimStartAndEndInline();
        if (Line.IsEmpty())
        {
            continue;
        }

        const FString CanonicalPath = ToBackslashPath(Line);
        CanonicalVirtualPathMap.FindOrAdd(NormalizeVirtualPath(CanonicalPath), CanonicalPath);
    }

    StormLibBridge_FreeReadFileResult(&StormReadResult);
}

void FUnrealAzerothMpqArchiveCollection::AddUniqueArchiveMatches(TArray<FString>& InOutArchivePaths, const FString& RootPath, const TCHAR* Pattern)
{
    TArray<FString> Matches;
    IFileManager::Get().FindFilesRecursive(Matches, *RootPath, Pattern, true, false, false);

    for (const FString& Match : Matches)
    {
        InOutArchivePaths.AddUnique(NormalizeFilePath(Match));
    }
}

int32 FUnrealAzerothMpqArchiveCollection::CountArchivesAtRoot(const FString& RootPath)
{
    TArray<FString> Matches;
    IFileManager::Get().FindFiles(Matches, *(RootPath / TEXT("*.mpq")), true, false);
    IFileManager::Get().FindFiles(Matches, *(RootPath / TEXT("*.MPQ")), true, false);
    return Matches.Num();
}

int32 FUnrealAzerothMpqArchiveCollection::ComputeArchivePriority(const FString& ArchiveRoot, const FString& ArchivePath)
{
    const FString RelativePath = MakeRelativeToArchiveRoot(ArchivePath, ArchiveRoot).ToLower();
    const FString FileName = FPaths::GetCleanFilename(RelativePath);
    const bool bIsLocaleArchive = RelativePath.Contains(TEXT("/"));

    int32 Priority = bIsLocaleArchive ? 1000 : 0;

    if (FileName.StartsWith(TEXT("patch")))
    {
        Priority += 5000;
        Priority += ComputePatchSuffixScore(RelativePath);
    }
    else if (FileName.StartsWith(TEXT("locale-")) || FileName.Contains(TEXT("-locale-")))
    {
        Priority += 3000;
    }
    else if (FileName.StartsWith(TEXT("speech-")) || FileName.Contains(TEXT("-speech-")))
    {
        Priority += 2500;
    }
    else if (FileName.StartsWith(TEXT("base-")) || FileName.StartsWith(TEXT("backup-")))
    {
        Priority += 2000;
    }
    else if (FileName.StartsWith(TEXT("lichking")))
    {
        Priority += 700;
    }
    else if (FileName.StartsWith(TEXT("expansion")))
    {
        Priority += 600;
    }
    else if (FileName.StartsWith(TEXT("common")))
    {
        Priority += 500;
    }

    return Priority;
}
