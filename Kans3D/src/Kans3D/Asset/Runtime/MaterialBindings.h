#pragma once
#include "ModelData.h"
#include "ShaderUniformBlock.h"

namespace Kans
{
    struct MaterialUniformBlock
    {
        uint32_t               Set = 0, Binding = 0, Element = 0;
        std::vector<std::byte> Bytes;
    };
    // Validates the Shader against the material contract (MaterialShaderInterface.h)
    // and packs the recipe's uniform blocks. The recipe may only bind descriptor
    // set 1 and must cover every material uniform block; samplers without a
    // texture are left to the renderer's default. Pure CPU.
    [[nodiscard]] std::variant<std::vector<MaterialUniformBlock>, AssetError>
    PackMaterialUniforms(const MaterialData&          data,
                         const ShaderProgramCode&     code,
                         const MaterialBindingRecipe& recipe,
                         uint64_t                     maximumUniformBytes = 64 * 1024 * 1024,
                         ShaderDomain                 domain              = ShaderDomain::Surface);
} // namespace Kans
