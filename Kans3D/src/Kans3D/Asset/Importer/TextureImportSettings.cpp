#include "kspch.h"
#include "TextureImportSettings.h"

namespace Kans
{
    std::variant<std::monostate, AssetError> ValidateTextureImportSettings(const TextureImportSettings& settings)
    {
        if (settings.Usage > TextureUsage::Normal ||
            (settings.ColorSpace && *settings.ColorSpace > TextureColorSpace::SRGB) ||
            (settings.AlphaMode && *settings.AlphaMode > TextureAlphaMode::Premultiplied))
            return AssetError {AssetErrorCode::InvalidArgument, "Unknown Texture import setting enum"};
        if ((settings.ColorSpace == TextureColorSpace::SRGB && settings.Usage != TextureUsage::Color) ||
            (settings.AlphaMode == TextureAlphaMode::Premultiplied && settings.Usage != TextureUsage::Color) ||
            (settings.NormalFlipY && settings.Usage != TextureUsage::Normal))
            return AssetError {AssetErrorCode::InvalidArgument,
                               "Incompatible Texture usage/color/alpha/normal settings"};
        return std::monostate {};
    }

    std::variant<TextureImportSettings, AssetError>
    ParseTextureImportSettings(const std::map<std::string, std::string>& values)
    {
        TextureImportSettings result;
        for (const auto& [key, value] : values)
        {
            if (key == "usage")
            {
                if (value == "color")
                    result.Usage = TextureUsage::Color;
                else if (value == "data")
                    result.Usage = TextureUsage::Data;
                else if (value == "normal")
                    result.Usage = TextureUsage::Normal;
                else
                    return AssetError {AssetErrorCode::InvalidArgument, "Invalid Texture usage: " + value};
            }
            else if (key == "color_space")
            {
                if (value == "auto")
                    result.ColorSpace.reset();
                else if (value == "linear")
                    result.ColorSpace = TextureColorSpace::Linear;
                else if (value == "srgb")
                    result.ColorSpace = TextureColorSpace::SRGB;
                else
                    return AssetError {AssetErrorCode::InvalidArgument, "Invalid Texture color_space: " + value};
            }
            else if (key == "alpha_mode")
            {
                if (value == "auto")
                    result.AlphaMode.reset();
                else if (value == "opaque")
                    result.AlphaMode = TextureAlphaMode::Opaque;
                else if (value == "straight")
                    result.AlphaMode = TextureAlphaMode::Straight;
                else if (value == "premultiplied")
                    result.AlphaMode = TextureAlphaMode::Premultiplied;
                else
                    return AssetError {AssetErrorCode::InvalidArgument, "Invalid Texture alpha_mode: " + value};
            }
            else if (key == "generate_mips" || key == "flip_y" || key == "normal_flip_y")
            {
                if (value != "true" && value != "false")
                    return AssetError {AssetErrorCode::InvalidArgument, "Invalid Texture boolean: " + key};
                const bool enabled = value == "true";
                if (key == "generate_mips")
                    result.GenerateMips = enabled;
                else if (key == "flip_y")
                    result.FlipY = enabled;
                else
                    result.NormalFlipY = enabled;
            }
            else
                return AssetError {AssetErrorCode::InvalidArgument, "Unknown Texture import setting: " + key};
        }
        if (const auto validated = ValidateTextureImportSettings(result); std::holds_alternative<AssetError>(validated))
            return std::get<AssetError>(validated);
        return result;
    }
} // namespace Kans
