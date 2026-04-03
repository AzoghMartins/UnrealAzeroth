#pragma once

#include "CoreMinimal.h"

struct FUnrealAzerothBlpTextureData
{
    int32 Width = 0;
    int32 Height = 0;
    TArray64<uint8> BGRA8Pixels;
    FString TextureVirtualPath;
    FString TextureArchivePath;
};

class FUnrealAzerothBlpLoader
{
public:
    static bool LoadFirstMip(
        const FString& ClientDataPath,
        const FString& VirtualPath,
        FUnrealAzerothBlpTextureData& OutTextureData,
        FString& OutErrorMessage);
};
