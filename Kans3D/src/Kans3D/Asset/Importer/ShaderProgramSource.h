#pragma once
#include "Kans3D/Asset/Runtime/ShaderData.h"
namespace Kans
{
    struct ShaderStageSource
    {
        ShaderProductStage Stage;
        AssetPath          Path;
    };
    // Versioned tool manifest. Paths are relative to its containing directory.
    [[nodiscard]] std::variant<std::vector<ShaderStageSource>, AssetError>
    ParseShaderProgramSource(const std::string& text);
} // namespace Kans
