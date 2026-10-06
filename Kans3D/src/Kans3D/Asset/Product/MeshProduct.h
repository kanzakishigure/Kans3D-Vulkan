#pragma once

#include <cstddef>
#include <variant>
#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Asset/Runtime/MeshData.h"

namespace Kans
{
    [[nodiscard]] std::variant<std::vector<std::byte>, AssetError> EncodeMeshProduct(const MeshData& mesh);
    [[nodiscard]] std::variant<MeshData, AssetError> DecodeMeshProduct(const std::byte* bytes, size_t size);
} // namespace Kans
