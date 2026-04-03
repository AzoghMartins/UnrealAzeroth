#pragma once

#include "CoreMinimal.h"
#include "UnrealAzerothAuthTypes.generated.h"

USTRUCT(BlueprintType)
struct UNREALAZEROTH_API FUnrealAzerothRealmInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    FString Address;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    FString Host;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Port = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 RealmId = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 RealmType = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Flags = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Timezone = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 CharacterCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    float Population = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    bool bLocked = false;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    bool bOffline = false;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    bool bSpecifiesBuild = false;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 BuildMajor = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 BuildMinor = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 BuildPatch = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 BuildNumber = 0;
};

USTRUCT(BlueprintType)
struct UNREALAZEROTH_API FUnrealAzerothCharacterSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    FString GuidHex;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Race = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Class = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Gender = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Level = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Zone = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 Map = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    FVector Position = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    bool bHasPet = false;
};
