#pragma once
#include "Kans3D/Asset/Runtime/TextureData.h"

namespace Kans
{
    [[nodiscard]] std::variant<std::vector<std::byte>, AssetError> EncodeTextureProduct(const TextureData& texture);
    [[nodiscard]] std::variant<TextureData, AssetError> DecodeTextureProduct(const std::byte* bytes, size_t size);
} // namespace Kans
