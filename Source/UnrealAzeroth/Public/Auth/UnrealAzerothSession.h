#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Auth/UnrealAzerothAuthTypes.h"
#include "UnrealAzerothSession.generated.h"

UCLASS(BlueprintType)
class UNREALAZEROTH_API UUnrealAzerothSession : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    FString AccountName;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    FString ServerHost;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    int32 AuthServerPort = 0;

    UPROPERTY(BlueprintReadOnly, Category="Unreal Azeroth|Auth")
    TArray<FUnrealAzerothRealmInfo> Realms;

    void Initialize(
        FString InAccountName,
        FString InServerHost,
        int32 InAuthServerPort,
        TArray<FUnrealAzerothRealmInfo> InRealms,
        TArray<uint8> InSessionKey);

    const TArray<uint8>& GetSessionKey() const;
    bool HasSessionKey() const;

private:
    TArray<uint8> SessionKey;
};
