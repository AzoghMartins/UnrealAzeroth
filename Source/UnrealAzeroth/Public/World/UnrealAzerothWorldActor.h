#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "World/UnrealAzerothWorldTypes.h"
#include "UnrealAzerothWorldActor.generated.h"

class UArrowComponent;
class USceneComponent;
class UUnrealAzerothModelComponent;

UCLASS(BlueprintType, Blueprintable)
class UNREALAZEROTH_API AUnrealAzerothWorldActor : public AActor
{
    GENERATED_BODY()

public:
    AUnrealAzerothWorldActor();

    virtual void OnConstruction(const FTransform& Transform) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Components")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Components")
    TObjectPtr<UArrowComponent> EditorArrowComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|Components")
    TObjectPtr<UUnrealAzerothModelComponent> ModelComponent;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unreal Azeroth|World")
    EUnrealAzerothWorldActorRole ActorRole = EUnrealAzerothWorldActorRole::StaticAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|World")
    FUnrealAzerothServerObjectReference ServerReference;

    UFUNCTION(BlueprintCallable, Category="Unreal Azeroth|World")
    void RefreshAzerothAsset();

    UFUNCTION(BlueprintPure, Category="Unreal Azeroth|World")
    UUnrealAzerothModelComponent* GetAzerothModelComponent() const
    {
        return ModelComponent;
    }

protected:
    void SetActorRoleDefaults(EUnrealAzerothWorldActorRole InActorRole, EUnrealAzerothWorldUpdateMode InUpdateMode);
};

UCLASS(BlueprintType, Blueprintable)
class UNREALAZEROTH_API AUnrealAzerothStaticAssetActor : public AUnrealAzerothWorldActor
{
    GENERATED_BODY()

public:
    AUnrealAzerothStaticAssetActor();
};

UCLASS(BlueprintType, Blueprintable)
class UNREALAZEROTH_API AUnrealAzerothFoliageActor : public AUnrealAzerothWorldActor
{
    GENERATED_BODY()

public:
    AUnrealAzerothFoliageActor();
};

UCLASS(BlueprintType, Blueprintable)
class UNREALAZEROTH_API AUnrealAzerothGameObjectActor : public AUnrealAzerothWorldActor
{
    GENERATED_BODY()

public:
    AUnrealAzerothGameObjectActor();
};

UCLASS(BlueprintType, Blueprintable)
class UNREALAZEROTH_API AUnrealAzerothUnitActor : public AUnrealAzerothWorldActor
{
    GENERATED_BODY()

public:
    AUnrealAzerothUnitActor();
};

UCLASS(BlueprintType, Blueprintable)
class UNREALAZEROTH_API AUnrealAzerothPlayerActor : public AUnrealAzerothUnitActor
{
    GENERATED_BODY()

public:
    AUnrealAzerothPlayerActor();
};
