#pragma once

#include <cstddef>
#include <variant>
#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Asset/Runtime/ModelData.h"

namespace Kans
{
    [[nodiscard]] std::variant<std::vector<std::byte>, AssetError> EncodeMaterialProduct(const MaterialData& data);
    [[nodiscard]] std::variant<MaterialData, AssetError> DecodeMaterialProduct(const std::byte* bytes, size_t size);
    [[nodiscard]] std::variant<std::vector<std::byte>, AssetError> EncodeModelProduct(const ModelData& data);
    [[nodiscard]] std::variant<ModelData, AssetError> DecodeModelProduct(const std::byte* bytes, size_t size);
} // namespace Kans
