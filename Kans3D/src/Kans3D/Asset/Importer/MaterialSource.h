#pragma once
#include <map>
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/Asset/Runtime/ShaderData.h"

namespace Kans
{
    // Human-authored `.material` source (YAML). Parameters and textures are named:
    //
    //   magic: KansMaterial
    //   version: 3
    //   name: Toon
    //   shader: <Shader AssetID>   # empty: engine Standard
    //   parameters: {Tint: [1, 0.8, 0.8, 1], Steps: 3}
    //   textures: {u_Albedo: <Texture AssetID>}
    //   render: {alpha_mode: mask, alpha_cutoff: 0.5, double_sided: true}
    //
    // Parameter names are set-1 uniform members; textures name set-1 samplers.
    // An empty Shader selects the embedded Standard Shader. Legacy PBR spellings
    // are accepted on input and canonical Standard names are written on save.
    struct MaterialSource
    {
        std::string Name;
        AssetID     Shader;
        // Scalars in declaration order; arrays list their elements consecutively and
        // matrices are column-major.
        std::map<std::string, std::vector<double>> Parameters;
        std::map<std::string, AssetID>              Textures;
        MaterialRenderState                         Render;
    };

    struct MaterialParameterInfo
    {
        std::string       Name;
        ShaderUniformType Type       = ShaderUniformType::Float;
        uint32_t          Components = 1, Columns = 1, ArrayCount = 1;
        // Editor widget declared by the Shader (`hint` lines in its .shaderprog).
        std::optional<ShaderParameterHint> Hint;

        size_t ValueCount() const { return size_t(Components) * Columns * ArrayCount; }
    };

    // Parameters and textures a material can set.
    struct MaterialSchema
    {
        std::vector<MaterialParameterInfo> Parameters;
        std::vector<std::string>           Textures;
        // Values used for parameters a source does not set (declared by the Shader's
        // .shaderprog `default` lines); other parameters are zero.
        std::map<std::string, std::vector<double>> Defaults;
    };

    [[nodiscard]] std::variant<MaterialSource, AssetError> ParseMaterialSource(const std::byte* bytes, size_t size);
    [[nodiscard]] std::string                             SerializeMaterialSource(const MaterialSource& source);

    // Reflected schema of the engine Standard Shader (shader == nullptr) or of a material Shader.
    // Struct members of material uniform blocks are not exposed and stay zero.
    [[nodiscard]] std::variant<MaterialSchema, AssetError> DescribeMaterial(const ShaderData* shader);

    // Builds the runtime material. `shader` must be the Product of `source.Shader`.
    [[nodiscard]] std::variant<MaterialData, AssetError> ResolveMaterialSource(const MaterialSource& source,
                                                                               const ShaderData*     shader);
} // namespace Kans
