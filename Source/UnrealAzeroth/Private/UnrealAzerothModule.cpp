#include "UnrealAzerothModule.h"

#include "CoreMinimal.h"
#include "ClientData/UnrealAzerothClientDataScanner.h"
#include "ClientData/UnrealAzerothClientDataTypes.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "UnrealAzerothLog.h"

DEFINE_LOG_CATEGORY(LogUnrealAzeroth);

namespace
{
void LogClientDataScanResult()
{
    const FUnrealAzerothClientDataScanResult Result = FUnrealAzerothClientDataScanner::ScanConfiguredPaths();

    UE_LOG(LogUnrealAzeroth, Display, TEXT("UnrealAzeroth client data scan started."));
    UE_LOG(LogUnrealAzeroth, Display, TEXT("Plugin base dir: %s"), *Result.PluginBaseDir);
    UE_LOG(LogUnrealAzeroth, Display, TEXT("Client data dir: %s"), *Result.ResolvedClientDataPath);
    UE_LOG(LogUnrealAzeroth, Display, TEXT("Server host: %s"), *Result.ResolvedServerHost);
    UE_LOG(LogUnrealAzeroth, Display, TEXT("Auth server port: %d"), Result.ResolvedAuthServerPort);

    for (const FString& Message : Result.Messages)
    {
        UE_LOG(LogUnrealAzeroth, Display, TEXT("%s"), *Message);
    }

    for (const FString& Error : Result.Errors)
    {
        UE_LOG(LogUnrealAzeroth, Error, TEXT("%s"), *Error);
    }

    const int32 ArchivePreviewCount = FMath::Min(Result.ArchiveFiles.Num(), 10);
    for (int32 ArchiveIndex = 0; ArchiveIndex < ArchivePreviewCount; ++ArchiveIndex)
    {
        const FUnrealAzerothArchiveFile& Archive = Result.ArchiveFiles[ArchiveIndex];
        UE_LOG(
            LogUnrealAzeroth,
            Display,
            TEXT("Archive %d: %s (%lld bytes)"),
            ArchiveIndex + 1,
            *Archive.RelativePath,
            Archive.FileSizeBytes);
    }

    if (Result.ArchiveFiles.Num() > ArchivePreviewCount)
    {
        UE_LOG(LogUnrealAzeroth, Display, TEXT("...and %d more archive(s)."), Result.ArchiveFiles.Num() - ArchivePreviewCount);
    }
}
}

void FUnrealAzerothModule::StartupModule()
{
    ScanClientDataCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("UnrealAzeroth.ScanClientData"),
        TEXT("Scans the configured WoW client Data directory and logs discovered MPQ archives."),
        FConsoleCommandDelegate::CreateStatic(&LogClientDataScanResult),
        ECVF_Default);
}

void FUnrealAzerothModule::ShutdownModule()
{
    if (ScanClientDataCommand != nullptr)
    {
        IConsoleManager::Get().UnregisterConsoleObject(ScanClientDataCommand);
        ScanClientDataCommand = nullptr;
    }
}

IMPLEMENT_MODULE(FUnrealAzerothModule, UnrealAzeroth)
