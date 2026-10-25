#pragma once
#include <map>
#include <optional>
#include <string>
#include "Kans3D/Asset/Runtime/TextureData.h"

namespace Kans
{
    struct TextureImportSettings
    {
        // Qualified: Kans::RenderHardware also declares TextureUsage (MSVC C2872 in some translation units).
        ::Kans::TextureUsage             Usage = ::Kans::TextureUsage::Color;
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
