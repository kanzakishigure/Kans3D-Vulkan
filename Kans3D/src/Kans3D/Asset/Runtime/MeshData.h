#pragma once

#include <array>
#include <variant>
#include <vector>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    struct MeshVertexData
    {
        std::array<float, 3> Position {};
        std::array<float, 3> Normal {};
        std::array<float, 2> UV {};
        std::array<float, 4> Color {1, 1, 1, 1};
        std::array<float, 4> Tangent {1, 0, 0, 1};
    };

    // One geometry Product. Node instances/transforms belong to model/scene data,
    // rather than duplicating the same geometry for every source node instance.
    struct MeshData
    {
        std::vector<MeshVertexData> Vertices;
        std::vector<uint32_t>       Indices;
        // Invalid ID explicitly means no material binding.
        AssetID Material;
    };
    [[nodiscard]] std::variant<std::monostate, AssetError> ValidateMeshData(const MeshData& mesh);

    // CPU runtime mesh. Rendering resources are created by the renderer's asset cache.
    class MeshAsset final : public Asset
    {
    public:
        explicit MeshAsset(MeshData data) : m_Data(std::move(data)) {}
        static AssetType GetStaticType() { return AssetType::StaticMesh; }
        AssetType        GetAssetType() const override { return GetStaticType(); }
        const MeshData&  GetData() const { return m_Data; }

    private:
        MeshData m_Data;
    };
} // namespace Kans
