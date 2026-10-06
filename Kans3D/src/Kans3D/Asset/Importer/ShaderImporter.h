#pragma once
#include "AssetImporterBase.h"
namespace Kans
{
    // Compiles GLSL stages (.vert/.frag/.comp) or a .shaderprog stage list to a
    // SPIR-V Shader Product. Includes are relative and resolved inside the asset root.
    class ShaderImporter final : public AssetImporterBase
    {
    public:
        uint32_t GetVersion() const override { return 4; }
        [[nodiscard]] std::variant<ImportInputs, AssetError>
        DiscoverInputs(const ImportContext& context) const override;
        [[nodiscard]] std::variant<ImportResult, AssetError> Import(const ImportContext& context) const override;
    };
} // namespace Kans
