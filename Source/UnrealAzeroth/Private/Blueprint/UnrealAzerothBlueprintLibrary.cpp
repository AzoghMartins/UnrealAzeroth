#include "Blueprint/UnrealAzerothBlueprintLibrary.h"

#include "ClientData/UnrealAzerothClientDataScanner.h"

FUnrealAzerothClientDataScanResult UUnrealAzerothBlueprintLibrary::ScanConfiguredClientData()
{
    return FUnrealAzerothClientDataScanner::ScanConfiguredPaths();
}
