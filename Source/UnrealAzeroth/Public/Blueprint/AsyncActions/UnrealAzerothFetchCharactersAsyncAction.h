#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "Auth/UnrealAzerothAuthTypes.h"
#include "Auth/UnrealAzerothSession.h"
#include "UnrealAzerothFetchCharactersAsyncAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FUnrealAzerothFetchCharactersSucceeded,
    const TArray<FUnrealAzerothCharacterSummary>&,
    Characters,
    const FString&,
    Message);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FUnrealAzerothFetchCharactersFailed,
    int32,
    ErrorCode,
    const FString&,
    ErrorMessage);

UCLASS()
class UNREALAZEROTH_API UUnrealAzerothFetchCharactersAsyncAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FUnrealAzerothFetchCharactersSucceeded OnSuccess;

    UPROPERTY(BlueprintAssignable)
    FUnrealAzerothFetchCharactersFailed OnFailure;

    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true"), Category="Unreal Azeroth|Auth")
    static UUnrealAzerothFetchCharactersAsyncAction* FetchCharactersForRealm(UUnrealAzerothSession* Session, FUnrealAzerothRealmInfo Realm);

    virtual void Activate() override;

private:
    UPROPERTY()
    TObjectPtr<UUnrealAzerothSession> RequestedSession;

    FUnrealAzerothRealmInfo RequestedRealm;

    void FinishAction();
};
