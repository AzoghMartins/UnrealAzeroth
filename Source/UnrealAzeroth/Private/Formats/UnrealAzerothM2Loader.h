#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "World/UnrealAzerothWorldTypes.h"

struct FUnrealAzerothM2MeshData
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;
    TArray<FString> ReferencedTexturePaths;
    FString ModelArchivePath;
    FString SkinArchivePath;
    int32 SourceVertexCount = 0;
    int32 SourceTriangleCount = 0;
};

class FUnrealAzerothM2Loader
{
public:
    static bool LoadPreviewMesh(
        const FString& ClientDataPath,
        const FString& VirtualPath,
        EUnrealAzerothArchivePreference ArchivePreference,
        float PreviewScale,
        bool bGenerateDoubleSidedPreview,
        FUnrealAzerothM2MeshData& OutMeshData,
        FString& OutErrorMessage);
};
