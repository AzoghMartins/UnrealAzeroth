#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UnrealAzerothSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Unreal Azeroth"))
class UNREALAZEROTH_API UUnrealAzerothSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UUnrealAzerothSettings();

    virtual FName GetCategoryName() const override;

#if WITH_EDITOR
    virtual FText GetSectionText() const override;
#endif

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Paths")
    FDirectoryPath ClientDataDirectory;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Paths")
    FDirectoryPath ServerDataDirectory;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Paths")
    FDirectoryPath AzerothCoreSourceDirectory;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Network")
    FString AuthServerHost;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Network", meta=(ClampMin="1", ClampMax="65535"))
    int32 AuthServerPort = 3724;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Network")
    FString PreferredRealmName;
};
