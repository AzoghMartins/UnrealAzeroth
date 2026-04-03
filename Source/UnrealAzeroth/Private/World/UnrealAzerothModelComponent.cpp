#include "World/UnrealAzerothModelComponent.h"

#include "HAL/FileManager.h"
#include "Formats/UnrealAzerothM2Loader.h"
#include "Materials/Material.h"
#include "Settings/UnrealAzerothSettings.h"
#include "UObject/UnrealType.h"
#include "UnrealAzerothLog.h"

UUnrealAzerothModelComponent::UUnrealAzerothModelComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bUseComplexAsSimpleCollision = false;
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

void UUnrealAzerothModelComponent::ResetPreviewMesh()
{
    ClearAllMeshSections();

    LastResolvedModelArchive.Reset();
    LastResolvedSkinArchive.Reset();
    LoadedVertexCount = 0;
    LoadedTriangleCount = 0;
    ReferencedTextureCount = 0;
}

void UUnrealAzerothModelComponent::UpdateRefreshStatus(bool bEmitLog)
{
    ResetPreviewMesh();

    if (!AssetReference.IsSet())
    {
        LastRefreshStatus = TEXT("No Azeroth asset path is configured on this actor.");
    }
    else
    {
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
            const FString VirtualPath = AssetReference.GetNormalizedVirtualPath();
            const EUnrealAzerothAssetSourceKind EffectiveSourceKind = AssetReference.GetEffectiveSourceKind();
            if (EffectiveSourceKind != EUnrealAzerothAssetSourceKind::M2)
            {
                const UEnum* SourceKindEnum = StaticEnum<EUnrealAzerothAssetSourceKind>();
                const FString SourceKindLabel = SourceKindEnum != nullptr
                    ? SourceKindEnum->GetDisplayNameTextByValue(static_cast<int64>(EffectiveSourceKind)).ToString()
                    : TEXT("Unknown");
                LastRefreshStatus = FString::Printf(
                    TEXT("Preview loading is currently implemented only for WotLK M2 assets. '%s' resolved as %s."),
                    *VirtualPath,
                    *SourceKindLabel);
            }
            else
            {
                FUnrealAzerothM2MeshData MeshData;
                FString LoadErrorMessage;
                if (!FUnrealAzerothM2Loader::LoadPreviewMesh(
                    Settings->ClientDataDirectory.Path,
                    VirtualPath,
                    PreviewScale,
                    bGenerateDoubleSidedPreview,
                    MeshData,
                    LoadErrorMessage))
                {
                    LastRefreshStatus = LoadErrorMessage;
                }
                else
                {
                    CreateMeshSection_LinearColor(
                        0,
                        MeshData.Vertices,
                        MeshData.Triangles,
                        MeshData.Normals,
                        MeshData.UV0,
                        MeshData.VertexColors,
                        MeshData.Tangents,
                        false);

                    SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));

                    LastResolvedModelArchive = MeshData.ModelArchivePath;
                    LastResolvedSkinArchive = MeshData.SkinArchivePath;
                    LoadedVertexCount = MeshData.SourceVertexCount;
                    LoadedTriangleCount = MeshData.SourceTriangleCount;
                    ReferencedTextureCount = MeshData.ReferencedTexturePaths.Num();

                    const FString TextureSuffix = ReferencedTextureCount > 0
                        ? FString::Printf(TEXT(" (%d referenced texture path(s) discovered)"), ReferencedTextureCount)
                        : FString();
                    LastRefreshStatus = FString::Printf(
                        TEXT("Loaded %d vertices and %d triangles from '%s' using '%s' and '%s'%s."),
                        LoadedVertexCount,
                        LoadedTriangleCount,
                        *VirtualPath,
                        *LastResolvedModelArchive,
                        *LastResolvedSkinArchive,
                        *TextureSuffix);
                }
            }
        }
    }

    if (bEmitLog)
    {
        UE_LOG(LogUnrealAzeroth, Display, TEXT("%s"), *LastRefreshStatus);
    }
}
