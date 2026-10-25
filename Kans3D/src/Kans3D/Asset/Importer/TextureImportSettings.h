#pragma once
#include <map>
#include <optional>
#include <string>
#include "Kans3D/Asset/Runtime/TextureData.h"

namespace Kans
{
    struct TextureImportSettings
    {
        TextureUsage                     Usage = TextureUsage::Color;
        std::optional<TextureColorSpace> ColorSpace;
        std::optional<TextureAlphaMode>  AlphaMode;
        bool                             GenerateMips = true;
        bool                             FlipY        = false;
        bool                             NormalFlipY  = false;
    };

    [[nodiscard]] std::variant<TextureImportSettings, AssetError>
    ParseTextureImportSettings(const std::map<std::string, std::string>& values);
    [[nodiscard]] std::variant<std::monostate, AssetError>
    ValidateTextureImportSettings(const TextureImportSettings& settings);
} // namespace Kans
