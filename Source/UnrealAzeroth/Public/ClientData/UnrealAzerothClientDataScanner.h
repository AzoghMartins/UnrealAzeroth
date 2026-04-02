#pragma once

#include "ClientData/UnrealAzerothClientDataTypes.h"

class UUnrealAzerothSettings;

class UNREALAZEROTH_API FUnrealAzerothClientDataScanner
{
public:
    static FUnrealAzerothClientDataScanResult ScanConfiguredPaths();
    static FUnrealAzerothClientDataScanResult Scan(const UUnrealAzerothSettings& Settings);
};
