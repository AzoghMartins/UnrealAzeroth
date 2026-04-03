#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "Auth/UnrealAzerothAuthTypes.h"
#include "Auth/UnrealAzerothSession.h"
#include "UnrealAzerothLoginAsyncAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FUnrealAzerothLoginSucceeded,
    UUnrealAzerothSession*,
    Session,
    const TArray<FUnrealAzerothRealmInfo>&,
    Realms,
    const FString&,
    Message);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FUnrealAzerothLoginFailed,
    int32,
    ErrorCode,
    const FString&,
    ErrorMessage);

UCLASS()
class UNREALAZEROTH_API UUnrealAzerothLoginAsyncAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FUnrealAzerothLoginSucceeded OnSuccess;

    UPROPERTY(BlueprintAssignable)
    FUnrealAzerothLoginFailed OnFailure;

    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true"), Category="Unreal Azeroth|Auth")
    static UUnrealAzerothLoginAsyncAction* LoginToConfiguredServer(const FString& Username, const FString& Password);

    virtual void Activate() override;

private:
    FString RequestedUsername;
    FString RequestedPassword;

    void FinishAction();
};
