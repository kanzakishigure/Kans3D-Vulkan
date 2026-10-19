#include "kspch.h"
#include "MeshData.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Kans
{
    void InsertSkinInfluence(MeshSkinVertex& vertex, uint32_t joint, float weight)
    {
        if (!std::isfinite(weight) || weight <= 0)
            return;
        for (size_t i = 0; i < 4; ++i)
            if (weight > vertex.Weights[i])
            {
                for (size_t k = 3; k > i; --k)
                {
                    vertex.Joints[k]  = vertex.Joints[k - 1];
                    vertex.Weights[k] = vertex.Weights[k - 1];
                }
                vertex.Joints[i]  = joint;
                vertex.Weights[i] = weight;
                break;
            }
    }
    bool NormalizeSkinWeights(MeshSkinVertex& vertex)
    {
        double sum = 0;
        for (float w : vertex.Weights)
            sum += w;
        if (!(sum > 0) || !std::isfinite(sum))
            return false;
        for (float& w : vertex.Weights)
            w = float(w / sum);
        return true;
    }
    void BuildSingleSection(MeshData& mesh)
    {
        mesh.LocalBounds = {};
        for (const auto& vertex : mesh.Vertices)
            mesh.LocalBounds.Expand(glm::vec3(vertex.Position[0], vertex.Position[1], vertex.Position[2]));
        mesh.Sections      = {{0, static_cast<uint32_t>(mesh.Indices.size()), 0, mesh.LocalBounds}};
        mesh.MaterialSlots = {{"", "", mesh.Material, mesh.AuthoredMaterial}};
    }

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
        const auto validBounds = [](const Bounds& bounds) {
            return bounds.IsValid() && std::isfinite(bounds.Min.x) && std::isfinite(bounds.Min.y) &&
                   std::isfinite(bounds.Min.z) && std::isfinite(bounds.Max.x) && std::isfinite(bounds.Max.y) &&
                   std::isfinite(bounds.Max.z);
        };
        if (mesh.VertexFormat == MeshVertexFormat::Skinned)
        {
            if (!mesh.Skeleton.IsValid() || mesh.Skin.size() != vertices || mesh.JointBounds.empty() ||
                mesh.JointBounds.size() > 65536)
                return AssetError {AssetErrorCode::InvalidData, "Invalid skin stream or skeleton"};
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(mesh.SkinBindTransform[c][r]))
                        return AssetError {AssetErrorCode::InvalidData, "Non-finite mesh bind transform"};
            if (std::abs(glm::determinant(mesh.SkinBindTransform)) < 1e-12f)
                return AssetError {AssetErrorCode::InvalidData, "Singular mesh bind transform"};
            for (const auto& box : mesh.JointBounds)
                if (box.IsValid() && !validBounds(box))
                    return AssetError {AssetErrorCode::InvalidData, "Invalid joint bounds"};
            for (const auto& skin : mesh.Skin)
            {
                double sum = 0;
                for (size_t k = 0; k < 4; ++k)
                {
                    if (skin.Joints[k] >= mesh.JointBounds.size() || !std::isfinite(skin.Weights[k]) ||
                        skin.Weights[k] < 0)
                        return AssetError {AssetErrorCode::InvalidData, "Invalid skin influence"};
                    sum += skin.Weights[k];
                }
                if (std::abs(sum - 1) > 1e-5)
                    return AssetError {AssetErrorCode::InvalidData, "Skin weights must sum to one"};
            }
        }
        else if (!mesh.Skin.empty() || mesh.Skeleton.IsValid() || !mesh.JointBounds.empty())
            return AssetError {AssetErrorCode::InvalidData, "Static mesh contains skin data"};
        if ((mesh.VertexFormat != MeshVertexFormat::PositionNormalUVColorTangent &&
             mesh.VertexFormat != MeshVertexFormat::Skinned) ||
            mesh.Sections.empty() || mesh.MaterialSlots.empty() || !validBounds(mesh.LocalBounds) ||
            mesh.Sections.size() > 1000000 || mesh.MaterialSlots.size() > 1000000)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Mesh Product format, sections or bounds"};
        uint64_t size = 56ull + vertices * 64ull + indices * 4ull + mesh.Sections.size() * 36ull;
        for (const auto& slot : mesh.MaterialSlots)
        {
            if (slot.Name.size() > 4096 || slot.ObjectKey.size() > 4096 || slot.Name.find('\0') != std::string::npos ||
                slot.ObjectKey.find('\0') != std::string::npos ||
                slot.DefaultMaterial.GetSourceID().IsValid() != (slot.DefaultMaterial.GetLocalID() != 0) ||
                slot.AuthoredMaterial.GetSourceID().IsValid() != (slot.AuthoredMaterial.GetLocalID() != 0))
                return AssetError {AssetErrorCode::InvalidData, "Invalid Mesh Product material slot"};
            size += 40 + slot.Name.size() + slot.ObjectKey.size();
        }
        if (size > 512ull * 1024 * 1024)
            return AssetError {AssetErrorCode::InvalidData, "Mesh Product exceeds size limit"};
        uint64_t end = 0;
        for (const auto& section : mesh.Sections)
        {
            if (!section.IndexCount || section.IndexCount % 3 || section.FirstIndex != end ||
                uint64_t(section.FirstIndex) + section.IndexCount > indices ||
                section.MaterialSlot >= mesh.MaterialSlots.size() || !validBounds(section.LocalBounds) ||
                glm::any(glm::lessThan(section.LocalBounds.Min, mesh.LocalBounds.Min)) ||
                glm::any(glm::greaterThan(section.LocalBounds.Max, mesh.LocalBounds.Max)))
                return AssetError {AssetErrorCode::InvalidData, "Invalid Mesh Product section range, slot or bounds"};
            end += section.IndexCount;
        }
        if (end != indices)
            return AssetError {AssetErrorCode::InvalidData, "Mesh Product sections do not cover indices"};
        return std::monostate {};
    }
} // namespace Kans
