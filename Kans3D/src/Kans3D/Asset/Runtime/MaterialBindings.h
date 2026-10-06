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
    // Validates that a binding recipe covers every Shader descriptor instance and
    // packs its uniform blocks. Pure CPU; used when importing Materials.
    [[nodiscard]] std::variant<std::vector<MaterialUniformBlock>, AssetError>
    PackMaterialUniforms(const MaterialData&          data,
                         const ShaderProgramCode&     code,
                         const MaterialBindingRecipe& recipe,
                         uint64_t                     maximumUniformBytes = 64 * 1024 * 1024);
} // namespace Kans
