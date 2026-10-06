#pragma once
#include "Kans3D/Asset/Runtime/ShaderData.h"
namespace Kans
{
    std::variant<std::vector<std::byte>, AssetError> EncodeShaderProduct(const ShaderProgramCode& code);
    std::variant<ShaderData, AssetError>             DecodeShaderProduct(const std::byte* bytes, size_t size);
} // namespace Kans
