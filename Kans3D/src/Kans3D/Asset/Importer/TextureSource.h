#pragma once
#include <filesystem>
#include "TextureImportSettings.h"

namespace Kans
{
    // Owned CPU decoding. Never modifies the legacy renderer's stb configuration.
    [[nodiscard]] std::variant<TextureData, AssetError> ParseTextureSource(const std::filesystem::path& file,
                                                                           const TextureImportSettings& settings = {});
} // namespace Kans
