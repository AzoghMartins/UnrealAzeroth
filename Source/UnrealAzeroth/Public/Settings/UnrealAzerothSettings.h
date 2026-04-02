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

    UPROPERTY(
        Config,
        EditAnywhere,
        BlueprintReadOnly,
        Category="Paths",
        meta=(DisplayName="WoW 3.3.5a Data Directory", ToolTip="Path to the original World of Warcraft 3.3.5a client Data directory."))
    FDirectoryPath ClientDataDirectory;

    UPROPERTY(
        Config,
        EditAnywhere,
        BlueprintReadOnly,
        Category="Connection",
        meta=(DisplayName="Server IP Address", ToolTip="IP address or hostname used for the authserver. Realm/world server ports are discovered from the authserver."))
    FString ServerHost;

    UPROPERTY(
        Config,
        EditAnywhere,
        BlueprintReadOnly,
        Category="Connection",
        meta=(DisplayName="Auth Server Port", ClampMin="1", ClampMax="65535", ToolTip="Port for the AzerothCore authserver. Realm/world server ports are fetched from the authserver."))
    int32 AuthServerPort = 3724;
};
