#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ClientData/UnrealAzerothClientDataTypes.h"
#include "UnrealAzerothBlueprintLibrary.generated.h"

UCLASS()
class UNREALAZEROTH_API UUnrealAzerothBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Unreal Azeroth|Configuration")
    static FUnrealAzerothClientDataScanResult ScanConfiguredClientData();
};
