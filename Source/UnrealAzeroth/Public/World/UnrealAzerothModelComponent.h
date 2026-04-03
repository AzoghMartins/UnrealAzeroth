#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "UObject/SoftObjectPtr.h"

#include "World/UnrealAzerothWorldTypes.h"
#include "UnrealAzerothModelComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;

UCLASS(ClassGroup=("Unreal Azeroth"), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class UNREALAZEROTH_API UUnrealAzerothModelComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()

public:
    UUnrealAzerothModelComponent(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    FUnrealAzerothAssetReference AssetReference;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    bool bAutoRefreshWhenPlaced = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset", meta=(ClampMin="0.01", UIMin="0.01"))
    float PreviewScale = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    bool bGenerateDoubleSidedPreview = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    EUnrealAzerothArchivePreference PreviewArchivePreference = EUnrealAzerothArchivePreference::OriginalOnly;

    UPROPERTY(EditDefaultsOnly, Category="Unreal Azeroth|Rendering", AdvancedDisplay)
    TSoftObjectPtr<UMaterialInterface> PreviewMaterialAsset;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastRefreshStatus;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastResolvedModelArchive;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastResolvedSkinArchive;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastResolvedTextureArchive;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastResolvedTexturePath;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    int32 LoadedVertexCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    int32 LoadedTriangleCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    int32 LoadedTextureWidth = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    int32 LoadedTextureHeight = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    int32 ReferencedTextureCount = 0;

    UFUNCTION(BlueprintCallable, Category="Unreal Azeroth|Asset")
    void RequestAssetRefresh();

    UFUNCTION(BlueprintPure, Category="Unreal Azeroth|Asset")
    bool HasConfiguredAsset() const;

    UFUNCTION(BlueprintPure, Category="Unreal Azeroth|Asset")
    FString GetLastRefreshStatus() const;

protected:
    virtual void OnRegister() override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    void ResetPreviewMesh();
    void UpdateRefreshStatus(bool bEmitLog);
    bool TryApplyReferencedTexture(
        const FString& ClientDataPath,
        EUnrealAzerothArchivePreference ArchivePreference,
        const TArray<FString>& CandidateTexturePaths,
        FString& OutTextureStatus);
    bool TryApplySiblingTextureFallback(
        const FString& ClientDataPath,
        EUnrealAzerothArchivePreference ArchivePreference,
        const FString& ModelVirtualPath,
        FString& OutTextureStatus);
    UMaterialInterface* ResolvePreviewMaterial();

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> LoadedPreviewTexture;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance;
};
