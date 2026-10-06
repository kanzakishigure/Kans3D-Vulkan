#include "kspch.h"
#include "ModelData.h"
#include <algorithm>
#include <cmath>
namespace Kans
{
    std::variant<std::monostate, AssetError> ValidateMaterialData(const MaterialData& data)
    {
        if ((!data.Shader.IsValid() && data.Shader != AssetID {}) || data.Name.size() > 4096 ||
            data.Name.find('\0') != std::string::npos || data.Textures.size() > 1000000 ||
            !std::all_of(data.BaseColor.begin(), data.BaseColor.end(), [](float v) { return std::isfinite(v); }) ||
            !std::isfinite(data.Metallic) || !std::isfinite(data.Roughness))
            return AssetError {AssetErrorCode::InvalidData, "Invalid material Product data"};
        for (const auto& binding : data.Textures)
            if (!binding.Texture.IsValid() || binding.Texture == data.Shader || !binding.Semantic ||
                binding.Semantic > MaxMaterialTextureSemantic || binding.UVChannel >= 8)
                return AssetError {AssetErrorCode::InvalidData, "Invalid material Texture binding"};
        if (data.Bindings)
        {
            if (!data.Shader.IsValid())
                return AssetError {AssetErrorCode::InvalidData, "Material binding recipe requires a Shader"};
            for (const auto& assignment : data.Bindings->Textures)
                if (assignment.TextureIndex >= data.Textures.size())
                    return AssetError {AssetErrorCode::InvalidData, "Material recipe references an absent Texture"};
        }
        return std::monostate {};
    }
} // namespace Kans
