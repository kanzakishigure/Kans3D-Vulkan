#pragma once

#include <filesystem>
#include "Kans3D/Asset/Product/TextureProduct.h"

namespace Kans
{
    // Runtime display files use stb's legacy byte/HDR decoding, not asset import metadata/color transforms.
    // Returns top-left RGBA pixels. The Texture2D upload handles the device's row origin.
    std::variant<TextureData, AssetError> DecodeTextureFileForDisplay(const std::filesystem::path& absolutePath,
                                                                      TextureColorSpace            colorSpace);
} // namespace Kans
