#include "Formats/UnrealAzerothM2Loader.h"

#include "Archives/UnrealAzerothMpqArchiveCollection.h"
#include "Misc/Paths.h"

namespace
{
constexpr uint32 WoW_M2_Version_WotLK = 264;

template <typename T>
bool ReadStructAt(const TArray<uint8>& Bytes, int64 Offset, T& OutValue)
{
    if (Offset < 0 || Offset + static_cast<int64>(sizeof(T)) > Bytes.Num())
    {
        return false;
    }

    FMemory::Memcpy(&OutValue, Bytes.GetData() + Offset, sizeof(T));
    return true;
}

template <typename T>
bool ReadArrayAt(const TArray<uint8>& Bytes, uint32 Count, uint32 Offset, TArray<T>& OutValues)
{
    if (Count == 0)
    {
        OutValues.Reset();
        return true;
    }

    const int64 ByteCount = static_cast<int64>(Count) * static_cast<int64>(sizeof(T));
    if (Offset + ByteCount > Bytes.Num())
    {
        return false;
    }

    OutValues.SetNumUninitialized(static_cast<int32>(Count));
    FMemory::Memcpy(OutValues.GetData(), Bytes.GetData() + Offset, ByteCount);
    return true;
}

bool ReadUtf8StringAt(const TArray<uint8>& Bytes, uint32 Count, uint32 Offset, FString& OutString)
{
    OutString.Reset();
    if (Count == 0)
    {
        return true;
    }

    if (Offset + Count > Bytes.Num())
    {
        return false;
    }

    TArray<UTF8CHAR> NullTerminatedBuffer;
    NullTerminatedBuffer.SetNumUninitialized(static_cast<int32>(Count) + 1);
    FMemory::Memcpy(NullTerminatedBuffer.GetData(), Bytes.GetData() + Offset, Count);
    NullTerminatedBuffer[Count] = '\0';

    OutString = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(NullTerminatedBuffer.GetData())));
    OutString.TrimStartAndEndInline();
    OutString.ReplaceInline(TEXT("\\"), TEXT("/"));
    return true;
}

FString MakeSkinVirtualPath(const FString& ModelVirtualPath)
{
    const FString BasePath = FPaths::ChangeExtension(ModelVirtualPath, TEXT(""));
    return FString::Printf(TEXT("%s00.skin"), *BasePath);
}

FVector ConvertPosition(const float* Position, float PreviewScale)
{
    return FVector(-Position[1], Position[0], Position[2]) * PreviewScale;
}

FVector ConvertNormal(const float* Normal)
{
    FVector Converted(-Normal[1], Normal[0], Normal[2]);
    Converted.Normalize();
    return Converted;
}

FProcMeshTangent MakeFallbackTangent(const FVector& Normal)
{
    FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normal);
    if (Tangent.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        Tangent = FVector::CrossProduct(FVector::ForwardVector, Normal);
    }

    Tangent.Normalize();
    return FProcMeshTangent(Tangent, false);
}

#pragma pack(push, 1)
struct FUnrealAzerothM2ArrayDef
{
    uint32 Count = 0;
    uint32 Offset = 0;
};

struct FUnrealAzerothM2Box
{
    float Min[3];
    float Max[3];
};

struct FUnrealAzerothM2HeaderWotLK
{
    char Magic[4];
    uint32 Version = 0;
    FUnrealAzerothM2ArrayDef Name;
    uint32 GlobalFlags = 0;
    FUnrealAzerothM2ArrayDef GlobalSequences;
    FUnrealAzerothM2ArrayDef Sequences;
    FUnrealAzerothM2ArrayDef SequenceLookup;
    FUnrealAzerothM2ArrayDef Bones;
    FUnrealAzerothM2ArrayDef KeyBoneLookup;
    FUnrealAzerothM2ArrayDef Vertices;
    uint32 NumSkinProfiles = 0;
    FUnrealAzerothM2ArrayDef Colors;
    FUnrealAzerothM2ArrayDef Textures;
    FUnrealAzerothM2ArrayDef TextureWeights;
    FUnrealAzerothM2ArrayDef TextureTransforms;
    FUnrealAzerothM2ArrayDef ReplaceableTextureLookup;
    FUnrealAzerothM2ArrayDef Materials;
    FUnrealAzerothM2ArrayDef BoneLookupTable;
    FUnrealAzerothM2ArrayDef TextureLookupTable;
    FUnrealAzerothM2ArrayDef TexUnitLookupTable;
    FUnrealAzerothM2ArrayDef TransparencyLookupTable;
    FUnrealAzerothM2ArrayDef TextureTransformsLookupTable;
    FUnrealAzerothM2Box BoundingBox;
    float BoundingSphereRadius = 0.0f;
    FUnrealAzerothM2Box CollisionBox;
    float CollisionSphereRadius = 0.0f;
    FUnrealAzerothM2ArrayDef CollisionTriangles;
    FUnrealAzerothM2ArrayDef CollisionVertices;
    FUnrealAzerothM2ArrayDef CollisionNormals;
    FUnrealAzerothM2ArrayDef Attachments;
    FUnrealAzerothM2ArrayDef AttachmentLookupTable;
    FUnrealAzerothM2ArrayDef Events;
    FUnrealAzerothM2ArrayDef Lights;
    FUnrealAzerothM2ArrayDef Cameras;
    FUnrealAzerothM2ArrayDef CameraLookupTable;
    FUnrealAzerothM2ArrayDef RibbonEmitters;
    FUnrealAzerothM2ArrayDef ParticleEmitters;
    FUnrealAzerothM2ArrayDef TextureCombinerCombos;
};

struct FUnrealAzerothM2Vertex
{
    float Position[3];
    uint8 BoneWeights[4];
    uint8 BoneIndices[4];
    float Normal[3];
    float UV0[2];
    float UV1[2];
};

struct FUnrealAzerothM2TextureDef
{
    uint32 Type = 0;
    uint32 Flags = 0;
    FUnrealAzerothM2ArrayDef FileName;
};

struct FUnrealAzerothM2SkinHeader
{
    char Magic[4];
    FUnrealAzerothM2ArrayDef VertexIndices;
    FUnrealAzerothM2ArrayDef TriangleIndices;
    FUnrealAzerothM2ArrayDef BoneIndices;
    FUnrealAzerothM2ArrayDef Submeshes;
    FUnrealAzerothM2ArrayDef TextureUnits;
    uint32 BoneCountMax = 0;
};
#pragma pack(pop)

static_assert(sizeof(FUnrealAzerothM2HeaderWotLK) == 312, "Unexpected WotLK M2 header size.");
static_assert(sizeof(FUnrealAzerothM2Vertex) == 48, "Unexpected WotLK M2 vertex size.");
static_assert(sizeof(FUnrealAzerothM2TextureDef) == 16, "Unexpected WotLK M2 texture definition size.");
static_assert(sizeof(FUnrealAzerothM2SkinHeader) == 48, "Unexpected WotLK skin header size.");
}

bool FUnrealAzerothM2Loader::LoadPreviewMesh(
    const FString& ClientDataPath,
    const FString& VirtualPath,
    const EUnrealAzerothArchivePreference ArchivePreference,
    const float PreviewScale,
    const bool bGenerateDoubleSidedPreview,
    FUnrealAzerothM2MeshData& OutMeshData,
    FString& OutErrorMessage)
{
    OutMeshData = FUnrealAzerothM2MeshData{};

    FUnrealAzerothMpqFileReadResult ModelReadResult;
    if (!FUnrealAzerothMpqArchiveCollection::Get().ReadFile(ClientDataPath, ArchivePreference, VirtualPath, ModelReadResult))
    {
        OutErrorMessage = MoveTemp(ModelReadResult.ErrorMessage);
        return false;
    }

    FUnrealAzerothM2HeaderWotLK Header;
    if (!ReadStructAt(ModelReadResult.Bytes, 0, Header))
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' is too small to contain a valid M2 header."), *VirtualPath);
        return false;
    }

    if (FMemory::Memcmp(Header.Magic, "MD20", 4) != 0)
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' is not a supported MD20 model."), *VirtualPath);
        return false;
    }

    if (Header.Version != WoW_M2_Version_WotLK)
    {
        OutErrorMessage = FString::Printf(
            TEXT("'%s' uses unsupported M2 version %u. The first loader only supports WotLK version %u."),
            *VirtualPath,
            Header.Version,
            WoW_M2_Version_WotLK);
        return false;
    }

    if (Header.NumSkinProfiles == 0)
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' does not report any external skin profiles."), *VirtualPath);
        return false;
    }

    TArray<FUnrealAzerothM2Vertex> SourceVertices;
    if (!ReadArrayAt(ModelReadResult.Bytes, Header.Vertices.Count, Header.Vertices.Offset, SourceVertices))
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' contains an invalid vertex array."), *VirtualPath);
        return false;
    }

    if (SourceVertices.Num() == 0)
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' does not contain any vertices."), *VirtualPath);
        return false;
    }

    TArray<FUnrealAzerothM2TextureDef> Textures;
    if (!ReadArrayAt(ModelReadResult.Bytes, Header.Textures.Count, Header.Textures.Offset, Textures))
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' contains an invalid texture array."), *VirtualPath);
        return false;
    }

    for (const FUnrealAzerothM2TextureDef& Texture : Textures)
    {
        if (Texture.Type != 0 || Texture.FileName.Count == 0)
        {
            continue;
        }

        FString TexturePath;
        if (ReadUtf8StringAt(ModelReadResult.Bytes, Texture.FileName.Count, Texture.FileName.Offset, TexturePath) && !TexturePath.IsEmpty())
        {
            OutMeshData.ReferencedTexturePaths.AddUnique(TexturePath.ToLower());
        }
    }

    const FString SkinVirtualPath = MakeSkinVirtualPath(VirtualPath);

    FUnrealAzerothMpqFileReadResult SkinReadResult;
    if (!FUnrealAzerothMpqArchiveCollection::Get().ReadFile(ClientDataPath, ArchivePreference, SkinVirtualPath, SkinReadResult))
    {
        OutErrorMessage = FString::Printf(
            TEXT("'%s' loaded successfully, but its primary skin '%s' could not be read. %s"),
            *VirtualPath,
            *SkinVirtualPath,
            *SkinReadResult.ErrorMessage);
        return false;
    }

    FUnrealAzerothM2SkinHeader SkinHeader;
    if (!ReadStructAt(SkinReadResult.Bytes, 0, SkinHeader))
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' is too small to contain a valid skin header."), *SkinVirtualPath);
        return false;
    }

    if (FMemory::Memcmp(SkinHeader.Magic, "SKIN", 4) != 0)
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' is not a supported WotLK skin file."), *SkinVirtualPath);
        return false;
    }

    TArray<uint16> SkinVertexIndices;
    if (!ReadArrayAt(SkinReadResult.Bytes, SkinHeader.VertexIndices.Count, SkinHeader.VertexIndices.Offset, SkinVertexIndices))
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' contains an invalid vertex remap table."), *SkinVirtualPath);
        return false;
    }

    TArray<uint16> SkinTriangleIndices;
    if (!ReadArrayAt(SkinReadResult.Bytes, SkinHeader.TriangleIndices.Count, SkinHeader.TriangleIndices.Offset, SkinTriangleIndices))
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' contains an invalid triangle index table."), *SkinVirtualPath);
        return false;
    }

    if (SkinVertexIndices.Num() == 0 || SkinTriangleIndices.Num() < 3 || (SkinTriangleIndices.Num() % 3) != 0)
    {
        OutErrorMessage = FString::Printf(TEXT("'%s' does not contain a renderable triangle list."), *SkinVirtualPath);
        return false;
    }

    OutMeshData.Vertices.Reserve(SkinVertexIndices.Num());
    OutMeshData.Normals.Reserve(SkinVertexIndices.Num());
    OutMeshData.UV0.Reserve(SkinVertexIndices.Num());
    OutMeshData.VertexColors.Reserve(SkinVertexIndices.Num());
    OutMeshData.Tangents.Reserve(SkinVertexIndices.Num());

    for (const uint16 SourceVertexIndex : SkinVertexIndices)
    {
        if (!SourceVertices.IsValidIndex(SourceVertexIndex))
        {
            OutErrorMessage = FString::Printf(
                TEXT("'%s' references source vertex %u, but the model only has %d vertices."),
                *SkinVirtualPath,
                SourceVertexIndex,
                SourceVertices.Num());
            return false;
        }

        const FUnrealAzerothM2Vertex& SourceVertex = SourceVertices[SourceVertexIndex];
        const FVector Normal = ConvertNormal(SourceVertex.Normal);

        OutMeshData.Vertices.Add(ConvertPosition(SourceVertex.Position, PreviewScale));
        OutMeshData.Normals.Add(Normal);
        OutMeshData.UV0.Add(FVector2D(SourceVertex.UV0[0], SourceVertex.UV0[1]));
        OutMeshData.VertexColors.Add(FLinearColor::White);
        OutMeshData.Tangents.Add(MakeFallbackTangent(Normal));
    }

    OutMeshData.Triangles.Reserve(SkinTriangleIndices.Num() * (bGenerateDoubleSidedPreview ? 2 : 1));

    for (int32 Index = 0; Index < SkinTriangleIndices.Num(); Index += 3)
    {
        const int32 A = SkinTriangleIndices[Index];
        const int32 B = SkinTriangleIndices[Index + 1];
        const int32 C = SkinTriangleIndices[Index + 2];

        if (!OutMeshData.Vertices.IsValidIndex(A) || !OutMeshData.Vertices.IsValidIndex(B) || !OutMeshData.Vertices.IsValidIndex(C))
        {
            OutErrorMessage = FString::Printf(
                TEXT("'%s' references a triangle vertex outside the remapped vertex buffer."),
                *SkinVirtualPath);
            return false;
        }

        OutMeshData.Triangles.Add(A);
        OutMeshData.Triangles.Add(B);
        OutMeshData.Triangles.Add(C);

        if (bGenerateDoubleSidedPreview)
        {
            OutMeshData.Triangles.Add(A);
            OutMeshData.Triangles.Add(C);
            OutMeshData.Triangles.Add(B);
        }
    }

    OutMeshData.ModelArchivePath = ModelReadResult.ArchiveRelativePath;
    OutMeshData.SkinArchivePath = SkinReadResult.ArchiveRelativePath;
    OutMeshData.SourceVertexCount = OutMeshData.Vertices.Num();
    OutMeshData.SourceTriangleCount = SkinTriangleIndices.Num() / 3;
    return true;
}
