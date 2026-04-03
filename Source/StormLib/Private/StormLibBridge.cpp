#include "StormLibBridge.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#ifdef PLATFORM_LINUX
#undef PLATFORM_LINUX
#endif

#include "StormLib.h"

struct FStormArchiveCollectionHandle
{
    std::vector<HANDLE> Archives;
    std::vector<std::string> ArchivePaths;
};

namespace
{
const char* DuplicateCString(const char* Input)
{
    if (Input == nullptr)
    {
        return nullptr;
    }

    const std::size_t Length = std::strlen(Input) + 1;
    char* Copy = static_cast<char*>(std::malloc(Length));
    if (Copy == nullptr)
    {
        return nullptr;
    }

    std::memcpy(Copy, Input, Length);
    return Copy;
}

bool ReadFileFromArchiveHandle(HANDLE ArchiveHandle, const char* ArchivePath, const char* VirtualPath, FStormReadFileResult* OutResult)
{
    if (ArchiveHandle == nullptr || ArchivePath == nullptr || VirtualPath == nullptr || OutResult == nullptr)
    {
        return false;
    }

    HANDLE FileHandle = nullptr;
    if (!SFileOpenFileEx(ArchiveHandle, VirtualPath, SFILE_OPEN_FROM_MPQ, &FileHandle))
    {
        return false;
    }

    DWORD FileSizeHigh = 0;
    const DWORD FileSizeLow = SFileGetFileSize(FileHandle, &FileSizeHigh);
    if (FileSizeLow == SFILE_INVALID_SIZE || FileSizeHigh != 0)
    {
        SFileCloseFile(FileHandle);
        return false;
    }

    std::uint8_t* Buffer = nullptr;
    if (FileSizeLow > 0)
    {
        Buffer = static_cast<std::uint8_t*>(std::malloc(FileSizeLow));
        if (Buffer == nullptr)
        {
            SFileCloseFile(FileHandle);
            return false;
        }
    }

    DWORD BytesRead = 0;
    const bool bReadSucceeded = FileSizeLow == 0 || SFileReadFile(FileHandle, Buffer, FileSizeLow, &BytesRead, nullptr);
    SFileCloseFile(FileHandle);

    if (!bReadSucceeded || BytesRead != FileSizeLow)
    {
        std::free(Buffer);
        return false;
    }

    OutResult->ArchivePath = DuplicateCString(ArchivePath);
    OutResult->Bytes = Buffer;
    OutResult->Size = FileSizeLow;
    return true;
}
}

bool StormLibBridge_OpenArchiveCollection(
    const char* const* ArchivePaths,
    const std::size_t ArchiveCount,
    FStormArchiveCollectionHandle** OutHandle)
{
    if (OutHandle == nullptr)
    {
        return false;
    }

    *OutHandle = nullptr;

    FStormArchiveCollectionHandle* Handle = new (std::nothrow) FStormArchiveCollectionHandle();
    if (Handle == nullptr)
    {
        return false;
    }

    for (std::size_t Index = 0; Index < ArchiveCount; ++Index)
    {
        const char* ArchivePath = ArchivePaths[Index];
        if (ArchivePath == nullptr || ArchivePath[0] == '\0')
        {
            continue;
        }

        HANDLE ArchiveHandle = nullptr;
        if (!SFileOpenArchive(ArchivePath, 0, 0, &ArchiveHandle))
        {
            continue;
        }

        Handle->Archives.push_back(ArchiveHandle);
        Handle->ArchivePaths.emplace_back(ArchivePath);
    }

    if (Handle->Archives.empty())
    {
        delete Handle;
        return false;
    }

    *OutHandle = Handle;
    return true;
}

void StormLibBridge_CloseArchiveCollection(FStormArchiveCollectionHandle* Handle)
{
    if (Handle == nullptr)
    {
        return;
    }

    for (HANDLE Archive : Handle->Archives)
    {
        if (Archive != nullptr)
        {
            SFileCloseArchive(Archive);
        }
    }

    delete Handle;
}

bool StormLibBridge_ReadFile(
    FStormArchiveCollectionHandle* Handle,
    const char* VirtualPath,
    FStormReadFileResult* OutResult)
{
    if (Handle == nullptr || VirtualPath == nullptr || OutResult == nullptr)
    {
        return false;
    }

    *OutResult = FStormReadFileResult{};

    for (std::size_t Index = 0; Index < Handle->Archives.size(); ++Index)
    {
        if (ReadFileFromArchiveHandle(Handle->Archives[Index], Handle->ArchivePaths[Index].c_str(), VirtualPath, OutResult))
        {
            return true;
        }
    }

    return false;
}

bool StormLibBridge_ReadFileFromArchive(
    const char* ArchivePath,
    const char* VirtualPath,
    FStormReadFileResult* OutResult)
{
    if (ArchivePath == nullptr || ArchivePath[0] == '\0' || VirtualPath == nullptr || OutResult == nullptr)
    {
        return false;
    }

    *OutResult = FStormReadFileResult{};

    HANDLE ArchiveHandle = nullptr;
    if (!SFileOpenArchive(ArchivePath, 0, 0, &ArchiveHandle))
    {
        return false;
    }

    const bool bReadSucceeded = ReadFileFromArchiveHandle(ArchiveHandle, ArchivePath, VirtualPath, OutResult);
    SFileCloseArchive(ArchiveHandle);
    return bReadSucceeded;
}

void StormLibBridge_FreeReadFileResult(FStormReadFileResult* Result)
{
    if (Result == nullptr)
    {
        return;
    }

    std::free(const_cast<char*>(Result->ArchivePath));
    std::free(Result->Bytes);
    *Result = FStormReadFileResult{};
}
