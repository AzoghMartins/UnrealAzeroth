#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

#include "World/UnrealAzerothWorldTypes.h"
#include "UnrealAzerothModelComponent.generated.h"

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

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastRefreshStatus;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastResolvedModelArchive;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastResolvedSkinArchive;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    int32 LoadedVertexCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    int32 LoadedTriangleCount = 0;

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
};
