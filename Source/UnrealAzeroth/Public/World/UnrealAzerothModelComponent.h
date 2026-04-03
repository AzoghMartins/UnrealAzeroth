#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "World/UnrealAzerothWorldTypes.h"
#include "UnrealAzerothModelComponent.generated.h"

UCLASS(ClassGroup=("Unreal Azeroth"), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class UNREALAZEROTH_API UUnrealAzerothModelComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UUnrealAzerothModelComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    FUnrealAzerothAssetReference AssetReference;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    bool bAutoRefreshWhenPlaced = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Asset")
    FString LastRefreshStatus;

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
    void UpdateRefreshStatus(bool bEmitLog);
};
