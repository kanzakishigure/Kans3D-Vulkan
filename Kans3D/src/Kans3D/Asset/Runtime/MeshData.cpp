#include "kspch.h"
#include "MeshData.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Kans
{
    std::variant<std::monostate, AssetError> ValidateMeshData(const MeshData& mesh)
    {
        const auto vertices = mesh.Vertices.size(), indices = mesh.Indices.size();
        if (!vertices || !indices || indices % 3 || vertices > std::numeric_limits<uint32_t>::max() ||
            indices > std::numeric_limits<uint32_t>::max())
            return AssetError {AssetErrorCode::InvalidData, "Invalid Mesh Product geometry counts"};
        if (56ull + uint64_t(vertices) * 64 + uint64_t(indices) * 4 > 512ull * 1024 * 1024)
            return AssetError {AssetErrorCode::InvalidData, "Mesh Product exceeds size limit"};
        if (mesh.Material.GetSourceID().IsValid() != (mesh.Material.GetLocalID() != 0))
            return AssetError {AssetErrorCode::InvalidData, "Partially valid mesh material identity"};
        if (mesh.AuthoredMaterial.GetSourceID().IsValid() != (mesh.AuthoredMaterial.GetLocalID() != 0))
            return AssetError {AssetErrorCode::InvalidData, "Partially valid authored mesh material identity"};
        const auto finite = [](const auto& values) {
            return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
        };
        for (const auto& vertex : mesh.Vertices)
            if (!finite(vertex.Position) || !finite(vertex.Normal) || !finite(vertex.UV) || !finite(vertex.Color) ||
                !finite(vertex.Tangent))
                return AssetError {AssetErrorCode::InvalidData, "Non-finite Mesh Product vertex"};
        for (const auto index : mesh.Indices)
            if (index >= vertices)
                return AssetError {AssetErrorCode::InvalidData, "Mesh Product index out of range"};
        return std::monostate {};
    }
} // namespace Kans
