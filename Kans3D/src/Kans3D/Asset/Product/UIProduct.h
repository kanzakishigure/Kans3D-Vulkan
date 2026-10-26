#pragma once
#include "Kans3D/UI/UIAsset.h"
namespace Kans
{
    inline constexpr uint32_t                        UIProductVersion = 1;
    std::variant<std::vector<std::byte>, AssetError> EncodeUIProduct(const UIData&);
    std::variant<UIData, AssetError>                 DecodeUIProduct(const std::byte*, size_t);
} // namespace Kans
