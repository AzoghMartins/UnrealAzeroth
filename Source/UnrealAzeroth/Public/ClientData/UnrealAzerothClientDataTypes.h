#pragma once

#include "CoreMinimal.h"
#include "UnrealAzerothClientDataTypes.generated.h"

USTRUCT(BlueprintType)
struct UNREALAZEROTH_API FUnrealAzerothArchiveFile
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    FString FileName;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    FString AbsolutePath;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    FString RelativePath;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    FString TopLevelDirectory;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    bool bIsInSubdirectory = false;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    int64 FileSizeBytes = 0;
};

USTRUCT(BlueprintType)
struct UNREALAZEROTH_API FUnrealAzerothClientDataScanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    FString PluginBaseDir;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    FString ResolvedClientDataPath;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    FString ResolvedServerHost;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    int32 ResolvedAuthServerPort = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    bool bClientDataPathExists = false;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    bool bHasServerHost = false;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    bool bUsedLocalBootstrapFile = false;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    FString LocalBootstrapFilePath;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    TArray<FString> LocaleDirectories;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    TArray<FUnrealAzerothArchiveFile> ArchiveFiles;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    TArray<FString> Messages;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth")
    TArray<FString> Errors;
};
