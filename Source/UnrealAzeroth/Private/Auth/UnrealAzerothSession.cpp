#include "Auth/UnrealAzerothSession.h"

void UUnrealAzerothSession::Initialize(
    FString InAccountName,
    FString InServerHost,
    int32 InAuthServerPort,
    TArray<FUnrealAzerothRealmInfo> InRealms,
    TArray<uint8> InSessionKey)
{
    AccountName = MoveTemp(InAccountName);
    ServerHost = MoveTemp(InServerHost);
    AuthServerPort = InAuthServerPort;
    Realms = MoveTemp(InRealms);
    SessionKey = MoveTemp(InSessionKey);
}

const TArray<uint8>& UUnrealAzerothSession::GetSessionKey() const
{
    return SessionKey;
}

bool UUnrealAzerothSession::HasSessionKey() const
{
    return SessionKey.Num() > 0;
}
