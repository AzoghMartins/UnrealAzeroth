#pragma once

#include "CoreMinimal.h"

#include "Auth/UnrealAzerothAuthTypes.h"

class UUnrealAzerothSession;

namespace UnrealAzeroth::Auth
{
struct FLoginResult
{
    bool bSucceeded = false;
    int32 ErrorCode = 0;
    FString ErrorMessage;
    FString AccountName;
    FString ServerHost;
    int32 AuthServerPort = 0;
    TArray<uint8> SessionKey;
    TArray<FUnrealAzerothRealmInfo> Realms;
};

struct FCharacterListResult
{
    bool bSucceeded = false;
    int32 ErrorCode = 0;
    FString ErrorMessage;
    TArray<FUnrealAzerothCharacterSummary> Characters;
};

FLoginResult LoginToConfiguredServer(const FString& Username, const FString& Password);
FCharacterListResult FetchCharactersForRealm(const UUnrealAzerothSession* Session, const FUnrealAzerothRealmInfo& Realm);
}
