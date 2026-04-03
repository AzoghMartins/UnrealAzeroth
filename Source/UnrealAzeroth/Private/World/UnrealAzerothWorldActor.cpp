#include "World/UnrealAzerothWorldActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "World/UnrealAzerothModelComponent.h"

AUnrealAzerothWorldActor::AUnrealAzerothWorldActor()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    EditorArrowComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("EditorArrow"));
    EditorArrowComponent->SetupAttachment(SceneRoot);
    EditorArrowComponent->SetArrowColor(FColor(196, 128, 64));
    EditorArrowComponent->bHiddenInGame = true;

    ModelComponent = CreateDefaultSubobject<UUnrealAzerothModelComponent>(TEXT("AzerothModel"));
    ModelComponent->SetupAttachment(SceneRoot);

    SetActorRoleDefaults(EUnrealAzerothWorldActorRole::StaticAsset, EUnrealAzerothWorldUpdateMode::EditorPlaced);
}

void AUnrealAzerothWorldActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshAzerothAsset();
}

void AUnrealAzerothWorldActor::RefreshAzerothAsset()
{
    if (ModelComponent != nullptr)
    {
        ModelComponent->RequestAssetRefresh();
    }
}

void AUnrealAzerothWorldActor::SetActorRoleDefaults(EUnrealAzerothWorldActorRole InActorRole, EUnrealAzerothWorldUpdateMode InUpdateMode)
{
    ActorRole = InActorRole;
    ServerReference.UpdateMode = InUpdateMode;
}

AUnrealAzerothStaticAssetActor::AUnrealAzerothStaticAssetActor()
{
    SetActorRoleDefaults(EUnrealAzerothWorldActorRole::StaticAsset, EUnrealAzerothWorldUpdateMode::EditorPlaced);
}

AUnrealAzerothFoliageActor::AUnrealAzerothFoliageActor()
{
    SetActorRoleDefaults(EUnrealAzerothWorldActorRole::Foliage, EUnrealAzerothWorldUpdateMode::EditorPlaced);
}

AUnrealAzerothGameObjectActor::AUnrealAzerothGameObjectActor()
{
    SetActorRoleDefaults(EUnrealAzerothWorldActorRole::GameObject, EUnrealAzerothWorldUpdateMode::Hybrid);
}

AUnrealAzerothUnitActor::AUnrealAzerothUnitActor()
{
    SetActorRoleDefaults(EUnrealAzerothWorldActorRole::Unit, EUnrealAzerothWorldUpdateMode::ServerDriven);
}

AUnrealAzerothPlayerActor::AUnrealAzerothPlayerActor()
{
    SetActorRoleDefaults(EUnrealAzerothWorldActorRole::Player, EUnrealAzerothWorldUpdateMode::ServerDriven);
}
