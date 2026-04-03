#include "Blueprint/AsyncActions/UnrealAzerothLoginAsyncAction.h"

#include "Async/Async.h"
#include "Auth/UnrealAzerothAuthService.h"

UUnrealAzerothLoginAsyncAction* UUnrealAzerothLoginAsyncAction::LoginToConfiguredServer(const FString& Username, const FString& Password)
{
    UUnrealAzerothLoginAsyncAction* Action = NewObject<UUnrealAzerothLoginAsyncAction>();
    Action->RequestedUsername = Username;
    Action->RequestedPassword = Password;
    return Action;
}

void UUnrealAzerothLoginAsyncAction::Activate()
{
    AddToRoot();

    TWeakObjectPtr<UUnrealAzerothLoginAsyncAction> WeakThis(this);
    const FString Username = RequestedUsername;
    const FString Password = RequestedPassword;

    Async(EAsyncExecution::ThreadPool, [WeakThis, Username, Password]()
    {
        UnrealAzeroth::Auth::FLoginResult Result = UnrealAzeroth::Auth::LoginToConfiguredServer(Username, Password);

        AsyncTask(ENamedThreads::GameThread, [WeakThis, Result = MoveTemp(Result)]() mutable
        {
            UUnrealAzerothLoginAsyncAction* Action = WeakThis.Get();
            if (Action == nullptr)
            {
                return;
            }

            if (Result.bSucceeded)
            {
                UUnrealAzerothSession* Session = NewObject<UUnrealAzerothSession>(GetTransientPackage());
                Session->Initialize(
                    Result.AccountName,
                    Result.ServerHost,
                    Result.AuthServerPort,
                    Result.Realms,
                    Result.SessionKey);

                Action->OnSuccess.Broadcast(
                    Session,
                    Session->Realms,
                    FString::Printf(TEXT("Authenticated %s and discovered %d realm(s)."), *Session->AccountName, Session->Realms.Num()));
            }
            else
            {
                Action->OnFailure.Broadcast(Result.ErrorCode, Result.ErrorMessage);
            }

            Action->FinishAction();
        });
    });
}

void UUnrealAzerothLoginAsyncAction::FinishAction()
{
    RequestedUsername.Reset();
    RequestedPassword.Reset();
    SetReadyToDestroy();
    RemoveFromRoot();
}
