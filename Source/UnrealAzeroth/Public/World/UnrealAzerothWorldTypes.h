#pragma once

#include "CoreMinimal.h"
#include "UnrealAzerothWorldTypes.generated.h"

UENUM(BlueprintType)
enum class EUnrealAzerothAssetSourceKind : uint8
{
    Unknown UMETA(DisplayName="Unknown"),
    M2 UMETA(DisplayName="M2 Model"),
    WMO UMETA(DisplayName="WMO Model"),
    ADT UMETA(DisplayName="ADT Tile"),
    BLP UMETA(DisplayName="BLP Texture")
};

UENUM(BlueprintType)
enum class EUnrealAzerothWorldActorRole : uint8
{
    StaticAsset UMETA(DisplayName="Static Asset"),
    Foliage UMETA(DisplayName="Foliage"),
    GameObject UMETA(DisplayName="GameObject"),
    Unit UMETA(DisplayName="Unit"),
    Player UMETA(DisplayName="Player")
};

UENUM(BlueprintType)
enum class EUnrealAzerothWorldUpdateMode : uint8
{
    EditorPlaced UMETA(DisplayName="Editor Placed"),
    ServerDriven UMETA(DisplayName="Server Driven"),
    Hybrid UMETA(DisplayName="Hybrid")
};

USTRUCT(BlueprintType)
struct UNREALAZEROTH_API FUnrealAzerothAssetReference
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    FString VirtualPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    EUnrealAzerothAssetSourceKind SourceKind = EUnrealAzerothAssetSourceKind::Unknown;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Asset")
    bool bLoadAsSkinnedAsset = false;

    bool IsSet() const
    {
        FString NormalizedPath = VirtualPath;
        NormalizedPath.TrimStartAndEndInline();
        return !NormalizedPath.IsEmpty();
    }

    FString GetNormalizedVirtualPath() const
    {
        FString NormalizedPath = VirtualPath;
        NormalizedPath.TrimStartAndEndInline();
        NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
        return NormalizedPath;
    }

    EUnrealAzerothAssetSourceKind GetEffectiveSourceKind() const
    {
        if (SourceKind != EUnrealAzerothAssetSourceKind::Unknown)
        {
            return SourceKind;
        }

        const FString NormalizedPath = GetNormalizedVirtualPath().ToLower();
        if (NormalizedPath.EndsWith(TEXT(".m2")))
        {
            return EUnrealAzerothAssetSourceKind::M2;
        }

        if (NormalizedPath.EndsWith(TEXT(".wmo")))
        {
            return EUnrealAzerothAssetSourceKind::WMO;
        }

        if (NormalizedPath.EndsWith(TEXT(".adt")))
        {
            return EUnrealAzerothAssetSourceKind::ADT;
        }

        if (NormalizedPath.EndsWith(TEXT(".blp")))
        {
            return EUnrealAzerothAssetSourceKind::BLP;
        }

        return EUnrealAzerothAssetSourceKind::Unknown;
    }
};

USTRUCT(BlueprintType)
struct UNREALAZEROTH_API FUnrealAzerothServerObjectReference
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Server")
    FString GuidHex;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Server")
    int32 EntryId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Server")
    int32 MapId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unreal Azeroth|Server")
    EUnrealAzerothWorldUpdateMode UpdateMode = EUnrealAzerothWorldUpdateMode::EditorPlaced;

    bool HasServerIdentity() const
    {
        return !GuidHex.IsEmpty() || EntryId > 0;
    }
};
