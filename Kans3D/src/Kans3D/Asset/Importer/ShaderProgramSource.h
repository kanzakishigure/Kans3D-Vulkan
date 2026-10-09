#pragma once
#include "Kans3D/Asset/Runtime/ShaderData.h"
namespace Kans
{
    struct ShaderStageSource
    {
        ShaderProductStage Stage;
        AssetPath          Path;
    };
    // Versioned tool manifest:
    //
    //   KansShaderProgram 1
    //   vertex=Toon.vert                  # stage paths, relative to the manifest
    //   fragment=Toon.frag
    //   default ShadowColor = 0.6 0.5 0.7 1   # material parameter defaults
    //
    // Blank lines and lines starting with '#' are ignored.
    [[nodiscard]] std::variant<std::vector<ShaderStageSource>, AssetError>
    ParseShaderProgramSource(const std::string& text);
    [[nodiscard]] std::variant<ShaderParameterDefaults, AssetError>
    ParseShaderProgramDefaults(const std::string& text);
} // namespace Kans
