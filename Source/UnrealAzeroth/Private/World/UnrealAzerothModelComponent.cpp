#include "World/UnrealAzerothModelComponent.h"

#include "Archives/UnrealAzerothMpqArchiveCollection.h"
#include "HAL/FileManager.h"
#include "Engine/Texture2D.h"
#include "Formats/UnrealAzerothBlpLoader.h"
#include "Formats/UnrealAzerothM2Loader.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Settings/UnrealAzerothSettings.h"
#include "UObject/UnrealType.h"
#include "UnrealAzerothLog.h"

namespace
{
bool IsLikelyEnvTexture(const FString& VirtualPath)
{
    const FString LowerPath = VirtualPath.ToLower();
    return LowerPath.Contains(TEXT("envmap")) || LowerPath.Contains(TEXT("reflection"));
}
}

UUnrealAzerothModelComponent::UUnrealAzerothModelComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bUseComplexAsSimpleCollision = false;
    PreviewMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/UnrealAzeroth/Materials/M_AzerothPreview.M_AzerothPreview")));
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
    LastResolvedTextureArchive.Reset();
    LastResolvedTexturePath.Reset();
    LoadedVertexCount = 0;
    LoadedTriangleCount = 0;
    LoadedTextureWidth = 0;
    LoadedTextureHeight = 0;
    ReferencedTextureCount = 0;
    LoadedPreviewTexture = nullptr;
    PreviewMaterialInstance = nullptr;
}

UMaterialInterface* UUnrealAzerothModelComponent::ResolvePreviewMaterial()
{
    return PreviewMaterialAsset.IsNull() ? nullptr : PreviewMaterialAsset.LoadSynchronous();
}

bool UUnrealAzerothModelComponent::TryApplyReferencedTexture(
    const FString& ClientDataPath,
    const TArray<FString>& CandidateTexturePaths,
    FString& OutTextureStatus)
{
    UMaterialInterface* PreviewMaterial = ResolvePreviewMaterial();
    if (PreviewMaterial == nullptr)
    {
        OutTextureStatus = TEXT("preview material asset is missing");
        return false;
    }

    FString LastTextureError = TEXT("no referenced textures were usable");
    for (const FString& CandidateTexturePath : CandidateTexturePaths)
    {
        FUnrealAzerothBlpTextureData TextureData;
        FString TextureErrorMessage;
        if (!FUnrealAzerothBlpLoader::LoadFirstMip(ClientDataPath, CandidateTexturePath, TextureData, TextureErrorMessage))
        {
            LastTextureError = FString::Printf(TEXT("'%s': %s"), *CandidateTexturePath, *TextureErrorMessage);
            continue;
        }

        LoadedPreviewTexture = UTexture2D::CreateTransient(
            TextureData.Width,
            TextureData.Height,
            PF_B8G8R8A8,
            NAME_None,
            TextureData.BGRA8Pixels);

        if (LoadedPreviewTexture == nullptr)
        {
            LastTextureError = FString::Printf(TEXT("'%s': failed to create a transient Unreal texture"), *CandidateTexturePath);
            continue;
        }

        LoadedPreviewTexture->SRGB = true;
        LoadedPreviewTexture->AddressX = TA_Wrap;
        LoadedPreviewTexture->AddressY = TA_Wrap;
        LoadedPreviewTexture->NeverStream = true;
        LoadedPreviewTexture->UpdateResource();

        PreviewMaterialInstance = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
        if (PreviewMaterialInstance == nullptr)
        {
            LoadedPreviewTexture = nullptr;
            LastTextureError = FString::Printf(TEXT("'%s': failed to create a material instance"), *CandidateTexturePath);
            continue;
        }

        PreviewMaterialInstance->SetTextureParameterValue(TEXT("DiffuseTexture"), LoadedPreviewTexture);
        SetMaterial(0, PreviewMaterialInstance);

        LastResolvedTextureArchive = TextureData.TextureArchivePath;
        LastResolvedTexturePath = TextureData.TextureVirtualPath;
        LoadedTextureWidth = TextureData.Width;
        LoadedTextureHeight = TextureData.Height;

        OutTextureStatus = FString::Printf(
            TEXT("loaded preview texture '%s' from '%s' (%dx%d)"),
            *LastResolvedTexturePath,
            *LastResolvedTextureArchive,
            LoadedTextureWidth,
            LoadedTextureHeight);
        return true;
    }

    OutTextureStatus = LastTextureError;
    return false;
}

bool UUnrealAzerothModelComponent::TryApplySiblingTextureFallback(
    const FString& ClientDataPath,
    const FString& ModelVirtualPath,
    FString& OutTextureStatus)
{
    TArray<FString> SiblingTexturePaths;
    FString ArchiveQueryError;
    if (!FUnrealAzerothMpqArchiveCollection::Get().FindFilesInDirectory(
        ClientDataPath,
        FPaths::GetPath(ModelVirtualPath),
        TEXT("blp"),
        SiblingTexturePaths,
        ArchiveQueryError))
    {
        OutTextureStatus = ArchiveQueryError;
        return false;
    }

    TArray<FString> PreferredPaths;
    TArray<FString> FallbackPaths;
    for (const FString& CandidatePath : SiblingTexturePaths)
    {
        if (IsLikelyEnvTexture(CandidatePath))
        {
            FallbackPaths.Add(CandidatePath);
        }
        else
        {
            PreferredPaths.Add(CandidatePath);
        }
    }

    PreferredPaths.Append(FallbackPaths);
    if (PreferredPaths.Num() == 0)
    {
        OutTextureStatus = FString::Printf(
            TEXT("no sibling BLP textures were found beside '%s'"),
            *ModelVirtualPath);
        return false;
    }

    FString TextureStatus;
    if (TryApplyReferencedTexture(ClientDataPath, PreferredPaths, TextureStatus))
    {
        OutTextureStatus = FString::Printf(TEXT("loaded preview texture via sibling fallback: %s"), *TextureStatus);
        return true;
    }

    OutTextureStatus = TextureStatus;
    return false;
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

                    FString TextureStatusSuffix;
                    if (ReferencedTextureCount > 0)
                    {
                        FString TextureStatus;
                        if (TryApplyReferencedTexture(Settings->ClientDataDirectory.Path, MeshData.ReferencedTexturePaths, TextureStatus))
                        {
                            TextureStatusSuffix = FString::Printf(TEXT(" %s."), *TextureStatus);
                        }
                        else if (TryApplySiblingTextureFallback(Settings->ClientDataDirectory.Path, VirtualPath, TextureStatus))
                        {
                            TextureStatusSuffix = FString::Printf(TEXT(" %s."), *TextureStatus);
                        }
                        else
                        {
                            TextureStatusSuffix = FString::Printf(
                                TEXT(" Preview texture fallback is active because %s."),
                                *TextureStatus);
                        }
                    }
                    else
                    {
                        FString TextureStatus;
                        if (TryApplySiblingTextureFallback(Settings->ClientDataDirectory.Path, VirtualPath, TextureStatus))
                        {
                            TextureStatusSuffix = FString::Printf(TEXT(" %s."), *TextureStatus);
                        }
                    }

                    LastRefreshStatus = FString::Printf(
                        TEXT("Loaded %d vertices and %d triangles from '%s' using '%s' and '%s'. %d referenced texture path(s) discovered.%s"),
                        LoadedVertexCount,
                        LoadedTriangleCount,
                        *VirtualPath,
                        *LastResolvedModelArchive,
                        *LastResolvedSkinArchive,
                        ReferencedTextureCount,
                        *TextureStatusSuffix);
                }
            }
        }
    }

    if (bEmitLog)
    {
        UE_LOG(LogUnrealAzeroth, Display, TEXT("%s"), *LastRefreshStatus);
    }
}
