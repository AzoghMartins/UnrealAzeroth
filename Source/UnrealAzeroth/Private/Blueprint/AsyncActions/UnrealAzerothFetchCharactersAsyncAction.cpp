#include "Blueprint/AsyncActions/UnrealAzerothFetchCharactersAsyncAction.h"

#include "Async/Async.h"
#include "Auth/UnrealAzerothAuthService.h"

UUnrealAzerothFetchCharactersAsyncAction* UUnrealAzerothFetchCharactersAsyncAction::FetchCharactersForRealm(
    UUnrealAzerothSession* Session,
    FUnrealAzerothRealmInfo Realm)
{
    UUnrealAzerothFetchCharactersAsyncAction* Action = NewObject<UUnrealAzerothFetchCharactersAsyncAction>();
    Action->RequestedSession = Session;
    Action->RequestedRealm = MoveTemp(Realm);
    return Action;
}

void UUnrealAzerothFetchCharactersAsyncAction::Activate()
{
    AddToRoot();

    TWeakObjectPtr<UUnrealAzerothFetchCharactersAsyncAction> WeakThis(this);
    UUnrealAzerothSession* Session = RequestedSession.Get();
    const FUnrealAzerothRealmInfo Realm = RequestedRealm;

    Async(EAsyncExecution::ThreadPool, [WeakThis, Session, Realm]()
    {
        UnrealAzeroth::Auth::FCharacterListResult Result = UnrealAzeroth::Auth::FetchCharactersForRealm(Session, Realm);

        AsyncTask(ENamedThreads::GameThread, [WeakThis, Result = MoveTemp(Result)]() mutable
        {
            UUnrealAzerothFetchCharactersAsyncAction* Action = WeakThis.Get();
            if (Action == nullptr)
            {
                return;
            }

            if (Result.bSucceeded)
            {
                Action->OnSuccess.Broadcast(
                    Result.Characters,
                    FString::Printf(TEXT("Fetched %d character(s)."), Result.Characters.Num()));
            }
            else
            {
                Action->OnFailure.Broadcast(Result.ErrorCode, Result.ErrorMessage);
            }

            Action->FinishAction();
        });
    });
}

void UUnrealAzerothFetchCharactersAsyncAction::FinishAction()
{
    RequestedSession = nullptr;
    SetReadyToDestroy();
    RemoveFromRoot();
}
