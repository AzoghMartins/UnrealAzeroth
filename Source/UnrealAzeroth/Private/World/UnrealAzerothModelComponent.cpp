#include "World/UnrealAzerothModelComponent.h"

#include "HAL/FileManager.h"
#include "Settings/UnrealAzerothSettings.h"
#include "UObject/UnrealType.h"
#include "UnrealAzerothLog.h"

UUnrealAzerothModelComponent::UUnrealAzerothModelComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    LastRefreshStatus = TEXT("No Azeroth asset selected.");
}

void UUnrealAzerothModelComponent::RequestAssetRefresh()
{
    UpdateRefreshStatus(true);
}

bool UUnrealAzerothModelComponent::HasConfiguredAsset() const
{
    return AssetReference.IsSet();
}

FString UUnrealAzerothModelComponent::GetLastRefreshStatus() const
{
    return LastRefreshStatus;
}

void UUnrealAzerothModelComponent::OnRegister()
{
    Super::OnRegister();

    if (bAutoRefreshWhenPlaced)
    {
        UpdateRefreshStatus(false);
    }
}

#if WITH_EDITOR
void UUnrealAzerothModelComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    if (bAutoRefreshWhenPlaced)
    {
        UpdateRefreshStatus(false);
    }
}
#endif

void UUnrealAzerothModelComponent::UpdateRefreshStatus(bool bEmitLog)
{
    if (!AssetReference.IsSet())
    {
        LastRefreshStatus = TEXT("No Azeroth asset path is configured on this actor.");
        return;
    }

    const UUnrealAzerothSettings* Settings = GetDefault<UUnrealAzerothSettings>();
    if (Settings == nullptr || Settings->ClientDataDirectory.Path.IsEmpty())
    {
        LastRefreshStatus = TEXT("Configure the WoW 3.3.5a Data directory before loading Azeroth assets.");
    }
    else if (!IFileManager::Get().DirectoryExists(*Settings->ClientDataDirectory.Path))
    {
        LastRefreshStatus = FString::Printf(
            TEXT("Configured client data directory does not exist: %s"),
            *Settings->ClientDataDirectory.Path);
    }
    else
    {
        const UEnum* SourceKindEnum = StaticEnum<EUnrealAzerothAssetSourceKind>();
        const FString SourceKindLabel = SourceKindEnum != nullptr
            ? SourceKindEnum->GetDisplayNameTextByValue(static_cast<int64>(AssetReference.SourceKind)).ToString()
            : TEXT("Unknown");
        LastRefreshStatus = FString::Printf(
            TEXT("Ready for implementation: load %s asset '%s' from the configured client data."),
            *SourceKindLabel,
            *AssetReference.GetNormalizedVirtualPath());
    }

    if (bEmitLog)
    {
        UE_LOG(LogUnrealAzeroth, Display, TEXT("%s"), *LastRefreshStatus);
    }
}
