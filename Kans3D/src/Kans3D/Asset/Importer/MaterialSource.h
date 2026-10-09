#pragma once
#include <map>
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/Asset/Runtime/ShaderData.h"

namespace Kans
{
    // Human-authored `.material` source (YAML). Parameters and textures are named:
    //
    //   magic: KansMaterial
    //   version: 2
    //   name: Toon
    //   shader: <Shader AssetID>   # empty: engine PBR
    //   parameters: {Tint: [1, 0.8, 0.8, 1], Steps: 3}
    //   textures: {u_Albedo: <Texture AssetID>}
    //
    // With a Shader, parameter names are members of its set-1 uniform blocks and
    // texture names are its set-1 samplers. Without one, the engine PBR material
    // takes base_color, metallic and roughness, and the textures base_color,
    // normal, metallic_roughness, occlusion and emissive.
    struct MaterialSource
    {
        std::string Name;
        AssetID     Shader;
        // Scalars in declaration order; arrays list their elements consecutively and
        // matrices are column-major.
        std::map<std::string, std::vector<double>> Parameters;
        std::map<std::string, AssetID>              Textures;
    };

    struct MaterialParameterInfo
    {
        std::string       Name;
        ShaderUniformType Type       = ShaderUniformType::Float;
        uint32_t          Components = 1, Columns = 1, ArrayCount = 1;

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

    // Schema of the engine PBR material (shader == nullptr) or of a material Shader.
    // Struct members of material uniform blocks are not exposed and stay zero.
    [[nodiscard]] std::variant<MaterialSchema, AssetError> DescribeMaterial(const ShaderData* shader);

    // Builds the runtime material. `shader` must be the Product of `source.Shader`.
    [[nodiscard]] std::variant<MaterialData, AssetError> ResolveMaterialSource(const MaterialSource& source,
                                                                               const ShaderData*     shader);
} // namespace Kans
