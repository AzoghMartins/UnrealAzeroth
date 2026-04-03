#include "Formats/UnrealAzerothBlpLoader.h"

#include "Archives/UnrealAzerothMpqArchiveCollection.h"

namespace
{
constexpr uint8 BLP_ENCODING_PALETTE = 1;
constexpr uint8 BLP_ENCODING_DXT = 2;
constexpr uint8 BLP_ENCODING_ARGB8888 = 3;

constexpr uint8 BLP_ALPHA_ENCODING_DXT1 = 0;
constexpr uint8 BLP_ALPHA_ENCODING_DXT3 = 1;
constexpr uint8 BLP_ALPHA_ENCODING_DXT5 = 7;

template <typename T>
bool ReadBlpStructAt(const TArray<uint8>& Bytes, int64 Offset, T& OutValue)
{
    if (Offset < 0 || Offset + static_cast<int64>(sizeof(T)) > Bytes.Num())
    {
        return false;
    }

    FMemory::Memcpy(&OutValue, Bytes.GetData() + Offset, sizeof(T));
    return true;
}

int64 ComputePaletteAlphaByteCount(const int64 PixelCount, const uint8 AlphaDepth)
{
    switch (AlphaDepth)
    {
    case 0:
        return 0;

    case 1:
        return (PixelCount + 7) / 8;

    case 4:
        return (PixelCount + 1) / 2;

    case 8:
        return PixelCount;

    default:
        return -1;
    }
}

uint8 DecodePaletteAlpha(const uint8* AlphaBytes, const int64 PixelIndex, const uint8 AlphaDepth)
{
    switch (AlphaDepth)
    {
    case 0:
        return 255;

    case 1:
        return ((AlphaBytes[PixelIndex / 8] >> (PixelIndex % 8)) & 0x1) != 0 ? 255 : 0;

    case 4:
    {
        const uint8 PackedValue = AlphaBytes[PixelIndex / 2];
        const uint8 Nibble = ((PixelIndex & 0x1) == 0) ? (PackedValue & 0x0F) : (PackedValue >> 4);
        return static_cast<uint8>((Nibble << 4) | Nibble);
    }

    case 8:
        return AlphaBytes[PixelIndex];

    default:
        return 255;
    }
}

#pragma pack(push, 1)
struct FUnrealAzerothBlp2Header
{
    char Magic[4];
    uint32 Type = 0;
    uint8 Encoding = 0;
    uint8 AlphaDepth = 0;
    uint8 AlphaEncoding = 0;
    uint8 HasMips = 0;
    uint32 Width = 0;
    uint32 Height = 0;
    uint32 MipOffsets[16] = {};
    uint32 MipSizes[16] = {};
    uint8 Palette[256 * 4] = {};
};
#pragma pack(pop)

static_assert(sizeof(FUnrealAzerothBlp2Header) == 1172, "Unexpected BLP2 header size.");

bool ResolveFirstMipView(
    const FUnrealAzerothBlp2Header& Header,
    const TArray<uint8>& SourceBytes,
    const uint8*& OutMipData,
    int64& OutMipSize,
    int32& OutMipIndex,
    FString& OutErrorMessage)
{
    for (int32 MipIndex = 0; MipIndex < UE_ARRAY_COUNT(Header.MipOffsets); ++MipIndex)
    {
        const uint32 Offset = Header.MipOffsets[MipIndex];
        const uint32 Size = Header.MipSizes[MipIndex];
        if (Offset == 0 || Size == 0)
        {
            continue;
        }

        if (static_cast<int64>(Offset) + static_cast<int64>(Size) > SourceBytes.Num())
        {
            OutErrorMessage = FString::Printf(
                TEXT("BLP mip %d points outside the source file (%u bytes at offset %u)."),
                MipIndex,
                Size,
                Offset);
            return false;
        }

        OutMipData = SourceBytes.GetData() + Offset;
        OutMipSize = Size;
        OutMipIndex = MipIndex;
        return true;
    }

    OutErrorMessage = TEXT("BLP does not contain any readable mip payloads.");
    return false;
}

bool DecodePaletteTexture(
    const FUnrealAzerothBlp2Header& Header,
    const uint8* MipData,
    const int64 MipSize,
    FUnrealAzerothBlpTextureData& OutTextureData,
    FString& OutErrorMessage)
{
    const int64 PixelCount = static_cast<int64>(OutTextureData.Width) * static_cast<int64>(OutTextureData.Height);
    if (PixelCount <= 0)
    {
        OutErrorMessage = TEXT("BLP palette payload reported an invalid image size.");
        return false;
    }

    if (MipSize < PixelCount)
    {
        OutErrorMessage = TEXT("BLP palette payload is too small to contain its palette indices.");
        return false;
    }

    const int64 AlphaByteCount = ComputePaletteAlphaByteCount(PixelCount, Header.AlphaDepth);
    if (AlphaByteCount < 0)
    {
        OutErrorMessage = FString::Printf(TEXT("BLP palette alpha depth %u is not supported yet."), Header.AlphaDepth);
        return false;
    }

    if (MipSize < PixelCount + AlphaByteCount)
    {
        OutErrorMessage = TEXT("BLP palette payload is too small to contain its alpha data.");
        return false;
    }

    const uint8* PaletteIndexBytes = MipData;
    const uint8* AlphaBytes = MipData + PixelCount;

    OutTextureData.BGRA8Pixels.SetNumUninitialized(PixelCount * 4);
    uint8* DestinationBytes = OutTextureData.BGRA8Pixels.GetData();

    for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const int32 PaletteIndex = static_cast<int32>(PaletteIndexBytes[PixelIndex]) * 4;
        DestinationBytes[PixelIndex * 4 + 0] = Header.Palette[PaletteIndex + 0];
        DestinationBytes[PixelIndex * 4 + 1] = Header.Palette[PaletteIndex + 1];
        DestinationBytes[PixelIndex * 4 + 2] = Header.Palette[PaletteIndex + 2];
        DestinationBytes[PixelIndex * 4 + 3] = DecodePaletteAlpha(AlphaBytes, PixelIndex, Header.AlphaDepth);
    }

    return true;
}

uint16 ReadUInt16LittleEndian(const uint8* Bytes)
{
    return static_cast<uint16>(Bytes[0]) | (static_cast<uint16>(Bytes[1]) << 8);
}

uint32 ReadUInt32LittleEndian(const uint8* Bytes)
{
    return static_cast<uint32>(Bytes[0])
        | (static_cast<uint32>(Bytes[1]) << 8)
        | (static_cast<uint32>(Bytes[2]) << 16)
        | (static_cast<uint32>(Bytes[3]) << 24);
}

void ExpandRgb565ToBgra(const uint16 PackedColor, uint8* OutBgra)
{
    const uint8 Red5 = static_cast<uint8>((PackedColor >> 11) & 0x1F);
    const uint8 Green6 = static_cast<uint8>((PackedColor >> 5) & 0x3F);
    const uint8 Blue5 = static_cast<uint8>(PackedColor & 0x1F);

    OutBgra[0] = static_cast<uint8>((Blue5 << 3) | (Blue5 >> 2));
    OutBgra[1] = static_cast<uint8>((Green6 << 2) | (Green6 >> 4));
    OutBgra[2] = static_cast<uint8>((Red5 << 3) | (Red5 >> 2));
    OutBgra[3] = 255;
}

void InterpolateColor(const uint8* ColorA, const uint8* ColorB, const uint32 WeightA, const uint32 WeightB, const uint32 Divisor, uint8* OutBgra)
{
    OutBgra[0] = static_cast<uint8>((WeightA * ColorA[0] + WeightB * ColorB[0]) / Divisor);
    OutBgra[1] = static_cast<uint8>((WeightA * ColorA[1] + WeightB * ColorB[1]) / Divisor);
    OutBgra[2] = static_cast<uint8>((WeightA * ColorA[2] + WeightB * ColorB[2]) / Divisor);
    OutBgra[3] = 255;
}

void DecodeBc1ColorBlock(const uint8* BlockBytes, const bool bForceFourColorMode, uint8* OutBlockBgra)
{
    uint8 Palette[4][4] = {};
    const uint16 Color0 = ReadUInt16LittleEndian(BlockBytes + 0);
    const uint16 Color1 = ReadUInt16LittleEndian(BlockBytes + 2);
    ExpandRgb565ToBgra(Color0, Palette[0]);
    ExpandRgb565ToBgra(Color1, Palette[1]);

    if (Color0 > Color1 || bForceFourColorMode)
    {
        InterpolateColor(Palette[0], Palette[1], 2, 1, 3, Palette[2]);
        InterpolateColor(Palette[0], Palette[1], 1, 2, 3, Palette[3]);
    }
    else
    {
        InterpolateColor(Palette[0], Palette[1], 1, 1, 2, Palette[2]);
        FMemory::Memzero(Palette[3], sizeof(Palette[3]));
    }

    const uint32 Indices = ReadUInt32LittleEndian(BlockBytes + 4);
    for (int32 PixelIndex = 0; PixelIndex < 16; ++PixelIndex)
    {
        const uint32 PaletteIndex = (Indices >> (PixelIndex * 2)) & 0x3;
        FMemory::Memcpy(OutBlockBgra + PixelIndex * 4, Palette[PaletteIndex], 4);
    }
}

void DecodeBc2AlphaBlock(const uint8* BlockBytes, uint8* InOutBlockBgra)
{
    for (int32 PixelIndex = 0; PixelIndex < 16; ++PixelIndex)
    {
        const uint8 PackedAlpha = BlockBytes[PixelIndex / 2];
        const uint8 Alpha4 = (PixelIndex & 0x1) == 0 ? (PackedAlpha & 0x0F) : (PackedAlpha >> 4);
        InOutBlockBgra[PixelIndex * 4 + 3] = static_cast<uint8>((Alpha4 << 4) | Alpha4);
    }
}

void BuildBc3AlphaPalette(const uint8 Alpha0, const uint8 Alpha1, uint8* OutPalette)
{
    OutPalette[0] = Alpha0;
    OutPalette[1] = Alpha1;

    if (Alpha0 > Alpha1)
    {
        OutPalette[2] = static_cast<uint8>((6 * Alpha0 + 1 * Alpha1) / 7);
        OutPalette[3] = static_cast<uint8>((5 * Alpha0 + 2 * Alpha1) / 7);
        OutPalette[4] = static_cast<uint8>((4 * Alpha0 + 3 * Alpha1) / 7);
        OutPalette[5] = static_cast<uint8>((3 * Alpha0 + 4 * Alpha1) / 7);
        OutPalette[6] = static_cast<uint8>((2 * Alpha0 + 5 * Alpha1) / 7);
        OutPalette[7] = static_cast<uint8>((1 * Alpha0 + 6 * Alpha1) / 7);
    }
    else
    {
        OutPalette[2] = static_cast<uint8>((4 * Alpha0 + 1 * Alpha1) / 5);
        OutPalette[3] = static_cast<uint8>((3 * Alpha0 + 2 * Alpha1) / 5);
        OutPalette[4] = static_cast<uint8>((2 * Alpha0 + 3 * Alpha1) / 5);
        OutPalette[5] = static_cast<uint8>((1 * Alpha0 + 4 * Alpha1) / 5);
        OutPalette[6] = 0;
        OutPalette[7] = 255;
    }
}

void DecodeBc3AlphaBlock(const uint8* BlockBytes, uint8* InOutBlockBgra)
{
    uint8 AlphaPalette[8] = {};
    BuildBc3AlphaPalette(BlockBytes[0], BlockBytes[1], AlphaPalette);

    uint64 PackedIndices = 0;
    for (int32 ByteIndex = 0; ByteIndex < 6; ++ByteIndex)
    {
        PackedIndices |= static_cast<uint64>(BlockBytes[2 + ByteIndex]) << (ByteIndex * 8);
    }

    for (int32 PixelIndex = 0; PixelIndex < 16; ++PixelIndex)
    {
        const uint32 AlphaIndex = static_cast<uint32>((PackedIndices >> (PixelIndex * 3)) & 0x7);
        InOutBlockBgra[PixelIndex * 4 + 3] = AlphaPalette[AlphaIndex];
    }
}

bool DecodeBcCompressedTexture(
    const FUnrealAzerothBlp2Header& Header,
    const uint8* MipData,
    const int64 MipSize,
    FUnrealAzerothBlpTextureData& OutTextureData,
    FString& OutErrorMessage)
{
    enum class EBcVariant : uint8
    {
        BC1,
        BC2,
        BC3,
    };

    EBcVariant Variant;
    int32 BytesPerBlock = 0;
    if (Header.AlphaDepth == 0 || Header.AlphaEncoding == BLP_ALPHA_ENCODING_DXT1)
    {
        Variant = EBcVariant::BC1;
        BytesPerBlock = 8;
    }
    else if (Header.AlphaEncoding == BLP_ALPHA_ENCODING_DXT3)
    {
        Variant = EBcVariant::BC2;
        BytesPerBlock = 16;
    }
    else if (Header.AlphaEncoding == BLP_ALPHA_ENCODING_DXT5)
    {
        Variant = EBcVariant::BC3;
        BytesPerBlock = 16;
    }
    else
    {
        OutErrorMessage = FString::Printf(
            TEXT("BLP DXT encoding uses unsupported alpha settings (depth=%u encoding=%u)."),
            Header.AlphaDepth,
            Header.AlphaEncoding);
        return false;
    }

    const int32 BlockCountX = (OutTextureData.Width + 3) / 4;
    const int32 BlockCountY = (OutTextureData.Height + 3) / 4;
    const int64 ExpectedMipSize = static_cast<int64>(BlockCountX) * static_cast<int64>(BlockCountY) * BytesPerBlock;
    if (MipSize < ExpectedMipSize)
    {
        OutErrorMessage = FString::Printf(
            TEXT("BLP DXT payload is shorter than expected (%lld bytes, needed %lld bytes)."),
            MipSize,
            ExpectedMipSize);
        return false;
    }

    OutTextureData.BGRA8Pixels.SetNumZeroed(static_cast<int64>(OutTextureData.Width) * static_cast<int64>(OutTextureData.Height) * 4);
    const uint8* BlockCursor = MipData;
    uint8 DecodedBlock[16 * 4] = {};

    for (int32 BlockY = 0; BlockY < BlockCountY; ++BlockY)
    {
        for (int32 BlockX = 0; BlockX < BlockCountX; ++BlockX)
        {
            switch (Variant)
            {
            case EBcVariant::BC1:
                DecodeBc1ColorBlock(BlockCursor, false, DecodedBlock);
                break;

            case EBcVariant::BC2:
                DecodeBc1ColorBlock(BlockCursor + 8, true, DecodedBlock);
                DecodeBc2AlphaBlock(BlockCursor, DecodedBlock);
                break;

            case EBcVariant::BC3:
                DecodeBc1ColorBlock(BlockCursor + 8, true, DecodedBlock);
                DecodeBc3AlphaBlock(BlockCursor, DecodedBlock);
                break;
            }

            for (int32 LocalY = 0; LocalY < 4; ++LocalY)
            {
                const int32 DestY = BlockY * 4 + LocalY;
                if (DestY >= OutTextureData.Height)
                {
                    break;
                }

                for (int32 LocalX = 0; LocalX < 4; ++LocalX)
                {
                    const int32 DestX = BlockX * 4 + LocalX;
                    if (DestX >= OutTextureData.Width)
                    {
                        break;
                    }

                    const int32 DestPixelIndex = DestY * OutTextureData.Width + DestX;
                    const int32 SourcePixelIndex = LocalY * 4 + LocalX;
                    FMemory::Memcpy(
                        OutTextureData.BGRA8Pixels.GetData() + DestPixelIndex * 4,
                        DecodedBlock + SourcePixelIndex * 4,
                        4);
                }
            }

            BlockCursor += BytesPerBlock;
        }
    }

    return true;
}

bool DecodeArgb8888Texture(
    const FUnrealAzerothBlp2Header& Header,
    const uint8* MipData,
    const int64 MipSize,
    FUnrealAzerothBlpTextureData& OutTextureData,
    FString& OutErrorMessage)
{
    const int64 PixelByteCount = static_cast<int64>(OutTextureData.Width) * static_cast<int64>(OutTextureData.Height) * 4;
    if (PixelByteCount <= 0)
    {
        OutErrorMessage = TEXT("BLP ARGB payload reported an invalid image size.");
        return false;
    }

    if (MipSize < PixelByteCount)
    {
        OutErrorMessage = TEXT("BLP ARGB payload is too small to contain its first mip.");
        return false;
    }

    OutTextureData.BGRA8Pixels.SetNumUninitialized(PixelByteCount);
    FMemory::Memcpy(OutTextureData.BGRA8Pixels.GetData(), MipData, PixelByteCount);
    return true;
}
}

bool FUnrealAzerothBlpLoader::LoadFirstMip(
    const FString& ClientDataPath,
    const FString& VirtualPath,
    const EUnrealAzerothArchivePreference ArchivePreference,
    FUnrealAzerothBlpTextureData& OutTextureData,
    FString& OutErrorMessage)
{
    OutTextureData = FUnrealAzerothBlpTextureData{};

    FUnrealAzerothMpqFileReadResult ReadResult;
    if (!FUnrealAzerothMpqArchiveCollection::Get().ReadFile(ClientDataPath, ArchivePreference, VirtualPath, ReadResult))
    {
        OutErrorMessage = MoveTemp(ReadResult.ErrorMessage);
        return false;
    }

    FUnrealAzerothBlp2Header Header;
    if (!ReadBlpStructAt(ReadResult.Bytes, 0, Header))
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' is too small to contain a valid BLP header."), *VirtualPath);
        return false;
    }

    if (FMemory::Memcmp(Header.Magic, "BLP2", 4) != 0)
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' is not a supported BLP2 texture."), *VirtualPath);
        return false;
    }

    if (Header.Width == 0 || Header.Height == 0)
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' reports an invalid image size of %ux%u."), *VirtualPath, Header.Width, Header.Height);
        return false;
    }

    const uint8* MipData = nullptr;
    int64 MipSize = 0;
    int32 MipIndex = INDEX_NONE;
    if (!ResolveFirstMipView(Header, ReadResult.Bytes, MipData, MipSize, MipIndex, OutErrorMessage))
    {
        return false;
    }

    OutTextureData.Width = static_cast<int32>(Header.Width >> MipIndex);
    OutTextureData.Height = static_cast<int32>(Header.Height >> MipIndex);
    OutTextureData.Width = FMath::Max(1, OutTextureData.Width);
    OutTextureData.Height = FMath::Max(1, OutTextureData.Height);
    OutTextureData.TextureVirtualPath = VirtualPath;
    OutTextureData.TextureArchivePath = ReadResult.ArchiveRelativePath;

    bool bDecoded = false;
    switch (Header.Encoding)
    {
    case BLP_ENCODING_PALETTE:
        bDecoded = DecodePaletteTexture(Header, MipData, MipSize, OutTextureData, OutErrorMessage);
        break;

    case BLP_ENCODING_DXT:
        bDecoded = DecodeBcCompressedTexture(Header, MipData, MipSize, OutTextureData, OutErrorMessage);
        break;

    case BLP_ENCODING_ARGB8888:
        bDecoded = DecodeArgb8888Texture(Header, MipData, MipSize, OutTextureData, OutErrorMessage);
        break;

    default:
        OutErrorMessage = FString::Printf(TEXT("'%s' uses unsupported BLP encoding %u."), *VirtualPath, Header.Encoding);
        return false;
    }

    if (!bDecoded)
    {
        return false;
    }

    const int64 ExpectedPixelBytes = static_cast<int64>(OutTextureData.Width) * static_cast<int64>(OutTextureData.Height) * 4;
    if (OutTextureData.BGRA8Pixels.Num() != ExpectedPixelBytes)
    {
        OutErrorMessage = FString::Printf(
            TEXT("'%s' decoded to an unexpected pixel buffer size (%lld bytes, wanted %lld bytes)."),
            *VirtualPath,
            OutTextureData.BGRA8Pixels.Num(),
            ExpectedPixelBytes);
        return false;
    }

    return true;
}
