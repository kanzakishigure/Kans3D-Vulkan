#pragma once
#include "AssetImporterBase.h"
namespace Kans
{
    // Compiles GLSL stages (.vert/.frag/.comp) or a .shaderprog stage list to a
    // SPIR-V Shader Product. Quoted includes are relative and resolved inside the
    // asset root; <Kans3D/...> includes are engine headers (MaterialShaderInterface.h).
    class ShaderImporter final : public AssetImporterBase
    {
    public:
        uint32_t GetVersion() const override { return 6; }
        [[nodiscard]] std::variant<ImportInputs, AssetError>
        DiscoverInputs(const ImportContext& context) const override;
        [[nodiscard]] std::variant<ImportResult, AssetError> Import(const ImportContext& context) const override;
    };
} // namespace Kans
