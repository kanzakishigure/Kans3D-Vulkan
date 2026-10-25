#pragma once
#include "ShaderUniformBlock.h"
namespace Kans
{
    enum class MaterialParameter
    {
        BaseColor,
        Metallic,
        Roughness
    };
    struct MaterialParameterWrite
    {
        MaterialParameter                     Parameter;
        std::vector<ShaderUniformPathElement> Path;
    };
    struct MaterialUniformAssignment
    {
        uint32_t                            Set = 0, Binding = 0, Element = 0;
        std::vector<MaterialParameterWrite> Parameters;
        std::vector<ShaderUniformWrite>     Values;
    };
    struct MaterialTextureAssignment
    {
        uint32_t Set = 0, Binding = 0, Element = 0, TextureIndex = 0;
    };
    struct MaterialBindingRecipe
    {
        std::vector<MaterialUniformAssignment> Uniforms;
        std::vector<MaterialTextureAssignment> Textures;
    };
} // namespace Kans
