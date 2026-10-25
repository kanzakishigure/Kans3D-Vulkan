#include "kspch.h"
#include "MeshProduct.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Kans
{
    namespace
    {
        constexpr uint32_t kMagic        = 0x48534d4b; // KMSH, little endian
        constexpr uint32_t kVersion      = 2;
        constexpr size_t   kHeaderBytes  = 56;
        constexpr size_t   kVertexBytes  = 64;
        constexpr size_t   kMaximumBytes = 512 * 1024 * 1024;
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "Mesh Products require IEEE 754 binary32");

        template<typename T>
        void Write(std::vector<std::byte>& out, T value)
        {
            for (size_t i = 0; i < sizeof(T); ++i)
                out.push_back(static_cast<std::byte>((value >> (i * 8)) & 255));
        }

        template<typename T>
        T Read(const std::byte*& bytes)
        {
            T value = 0;
            for (size_t i = 0; i < sizeof(T); ++i)
                value |= static_cast<T>(std::to_integer<uint8_t>(*bytes++)) << (i * 8);
            return value;
        }

        template<size_t N>
        void WriteFloats(std::vector<std::byte>& out, const std::array<float, N>& values)
        {
            for (float value : values)
            {
                uint32_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                Write(out, bits);
            }
        }

        template<size_t N>
        bool ReadFloats(const std::byte*& bytes, std::array<float, N>& values)
        {
            for (float& value : values)
            {
                const uint32_t bits = Read<uint32_t>(bytes);
                std::memcpy(&value, &bits, sizeof(value));
                if (!std::isfinite(value))
                    return false;
            }
            return true;
        }

    } // namespace

    std::variant<std::vector<std::byte>, AssetError> EncodeMeshProduct(const MeshData& mesh)
    {
        const auto vertexCount = mesh.Vertices.size();
        const auto indexCount  = mesh.Indices.size();
        auto       valid       = ValidateMeshData(mesh);
        if (auto* error = std::get_if<AssetError>(&valid))
            return *error;
        const uint64_t         size = kHeaderBytes + uint64_t(vertexCount) * kVertexBytes + uint64_t(indexCount) * 4;
        std::vector<std::byte> bytes;
        bytes.reserve(static_cast<size_t>(size));
        Write(bytes, kMagic);
        Write(bytes, kVersion);
        Write(bytes, static_cast<uint32_t>(AssetType::StaticMesh));
        Write(bytes, uint32_t(0));
        Write(bytes, static_cast<uint32_t>(vertexCount));
        Write(bytes, static_cast<uint32_t>(indexCount));
        Write(bytes, mesh.Material.GetSourceID().GetUUID());
        Write(bytes, mesh.Material.GetLocalID());
        Write(bytes, mesh.AuthoredMaterial.GetSourceID().GetUUID());
        Write(bytes, mesh.AuthoredMaterial.GetLocalID());
        for (const auto& vertex : mesh.Vertices)
        {
            WriteFloats(bytes, vertex.Position);
            WriteFloats(bytes, vertex.Normal);
            WriteFloats(bytes, vertex.UV);
            WriteFloats(bytes, vertex.Color);
            WriteFloats(bytes, vertex.Tangent);
        }
        for (uint32_t index : mesh.Indices)
            Write(bytes, index);
        return bytes;
    }

    std::variant<MeshData, AssetError> DecodeMeshProduct(const std::byte* bytes, size_t size)
    {
        if (!bytes || size < 40 || size > kMaximumBytes)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Mesh Product byte length"};
        const auto* cursor = bytes;
        if (Read<uint32_t>(cursor) != kMagic)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Mesh Product magic"};
        const auto version = Read<uint32_t>(cursor);
        if (version != 1 && version != kVersion)
            return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported Mesh Product version"};
        if (Read<uint32_t>(cursor) != static_cast<uint32_t>(AssetType::StaticMesh) || Read<uint32_t>(cursor) != 0)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Mesh Product type or flags"};
        const auto     vertexCount = Read<uint32_t>(cursor);
        const auto     indexCount  = Read<uint32_t>(cursor);
        const auto     source      = Read<uint64_t>(cursor);
        const auto     local       = Read<uint64_t>(cursor);
        if (version == 2 && size < kHeaderBytes)
            return AssetError {AssetErrorCode::InvalidData, "Truncated Mesh Product header"};
        const auto     authoredSource = version == 2 ? Read<uint64_t>(cursor) : source;
        const auto     authoredLocal  = version == 2 ? Read<uint64_t>(cursor) : local;
        const uint64_t expected =
            (version == 1 ? 40 : kHeaderBytes) + uint64_t(vertexCount) * kVertexBytes + uint64_t(indexCount) * 4;
        if (!vertexCount || !indexCount || indexCount % 3 || expected != size || ((source != 0) != (local != 0)) ||
            ((authoredSource != 0) != (authoredLocal != 0)))
            return AssetError {AssetErrorCode::InvalidData, "Invalid Mesh Product counts or material identity"};
        MeshData result;
        if (source)
        {
            std::ostringstream id;
            id << std::hex << std::setw(16) << std::setfill('0') << source;
            result.Material = AssetID(SourceAssetID::FromString(id.str()), local);
        }
        if (authoredSource)
        {
            std::ostringstream id;
            id << std::hex << std::setw(16) << std::setfill('0') << authoredSource;
            result.AuthoredMaterial = AssetID(SourceAssetID::FromString(id.str()), authoredLocal);
        }
        result.Vertices.resize(vertexCount);
        result.Indices.resize(indexCount);
        for (auto& vertex : result.Vertices)
            if (!ReadFloats(cursor, vertex.Position) || !ReadFloats(cursor, vertex.Normal) ||
                !ReadFloats(cursor, vertex.UV) || !ReadFloats(cursor, vertex.Color) ||
                !ReadFloats(cursor, vertex.Tangent))
                return AssetError {AssetErrorCode::InvalidData, "Non-finite Mesh Product vertex"};
        for (auto& index : result.Indices)
        {
            index = Read<uint32_t>(cursor);
            if (index >= vertexCount)
                return AssetError {AssetErrorCode::InvalidData, "Mesh Product index out of range"};
        }
        return result;
    }
} // namespace Kans
