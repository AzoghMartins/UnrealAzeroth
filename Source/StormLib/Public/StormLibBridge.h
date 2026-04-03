#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
    #if defined(STORMLIBBRIDGE_EXPORTS)
        #define STORMLIBBRIDGE_API __declspec(dllexport)
    #else
        #define STORMLIBBRIDGE_API __declspec(dllimport)
    #endif
#else
    #define STORMLIBBRIDGE_API __attribute__((visibility("default")))
#endif

struct FStormArchiveCollectionHandle;

struct FStormReadFileResult
{
    const char* ArchivePath = nullptr;
    std::uint8_t* Bytes = nullptr;
    std::size_t Size = 0;
};

STORMLIBBRIDGE_API bool StormLibBridge_OpenArchiveCollection(
    const char* const* ArchivePaths,
    std::size_t ArchiveCount,
    FStormArchiveCollectionHandle** OutHandle);

STORMLIBBRIDGE_API void StormLibBridge_CloseArchiveCollection(FStormArchiveCollectionHandle* Handle);

STORMLIBBRIDGE_API bool StormLibBridge_ReadFile(
    FStormArchiveCollectionHandle* Handle,
    const char* VirtualPath,
    FStormReadFileResult* OutResult);

STORMLIBBRIDGE_API bool StormLibBridge_ReadFileFromArchive(
    const char* ArchivePath,
    const char* VirtualPath,
    FStormReadFileResult* OutResult);

STORMLIBBRIDGE_API void StormLibBridge_FreeReadFileResult(FStormReadFileResult* Result);
