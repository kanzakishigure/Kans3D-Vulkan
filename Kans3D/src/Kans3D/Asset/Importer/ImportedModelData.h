#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>
#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Asset/Runtime/MeshData.h"

namespace Kans
{
    using ImportedVertex = MeshVertexData;

    struct ImportedMeshData
    {
        std::string                 Name;
        uint32_t                    MaterialIndex = 0;
        std::vector<ImportedVertex> Vertices;
        std::vector<uint32_t>       Indices;
    };

    struct ImportedTextureReference
    {
        uint32_t    Semantic  = 0;
        uint32_t    UVChannel = 0;
        std::string Source;
    };

    struct ImportedMaterialData
    {
        std::string                           Name;
        std::array<float, 4>                  BaseColor {1, 1, 1, 1};
        float                                 Metallic  = 0;
        float                                 Roughness = 1;
        std::vector<ImportedTextureReference> Textures;
    };

    struct ImportedNodeData
    {
        std::string Name;
        int64_t     Parent = -1;
        // Row-major local transform; world transforms are derived by consumers.
        std::array<float, 16> LocalTransform {};
        std::vector<uint32_t> Meshes;
    };

    struct ImportedModelData
    {
        std::vector<ImportedMeshData>     Meshes;
        std::vector<ImportedMaterialData> Materials;
        std::vector<ImportedNodeData>     Nodes;
        // Successfully read files, including .mtl and external glTF buffers.
        std::vector<std::filesystem::path> SourceFiles;
        // Source content that was skipped, such as animations.
        std::vector<std::string> Warnings;
    };

    // Owns all returned data. No Assimp or rendering objects escape this boundary.
    // Animated/skinned and embedded-texture models are explicitly rejected until
    // their owned CPU representations are implemented.
    [[nodiscard]] std::variant<ImportedModelData, AssetError> ParseModelCpu(const std::filesystem::path& path);
    // Closed input IO: undeclared paths are rejected before opening them.
    [[nodiscard]] std::variant<ImportedModelData, AssetError>
    ParseModelCpu(const std::filesystem::path& path, const std::vector<std::filesystem::path>& allowedFiles);
    // Finds the file a model's texture reference points to. Accepts backslashes,
    // absolute paths from other machines and case mismatches by falling back to a
    // file-name search below the model directory.
    [[nodiscard]] std::variant<std::filesystem::path, AssetError>
    ResolveModelTexture(const std::filesystem::path& modelFile, const std::string& reference);
} // namespace Kans
