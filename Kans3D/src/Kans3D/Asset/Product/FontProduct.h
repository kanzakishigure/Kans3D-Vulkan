#pragma once
#include "Kans3D/Text/FontAsset.h"
namespace Kans
{
    inline constexpr size_t                          FontProductMaximumBytes = 256ull * 1024 * 1024;
    std::variant<std::vector<std::byte>, AssetError> EncodeFontProduct(const FontData&);
    std::variant<FontData, AssetError>               DecodeFontProduct(const std::byte*, size_t);
} // namespace Kans
