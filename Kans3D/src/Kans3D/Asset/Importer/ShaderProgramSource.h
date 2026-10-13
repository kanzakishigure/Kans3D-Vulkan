#pragma once
#include "Kans3D/Asset/Runtime/ShaderData.h"
namespace Kans
{
    std::variant<ShaderOutputSpace, AssetError> ParseShaderOutputSpace(const std::string& text);
    struct ShaderStageSource
    {
        ShaderProductStage Stage;
        AssetPath          Path;
    };
    struct ShaderPassSource
    {
        std::string                    Name, Tag;
        std::vector<ShaderStageSource> Stages;
        ShaderPassState                State;
    };
    [[nodiscard]] std::variant<std::vector<ShaderPassSource>, AssetError>
    ParseShaderProgramPasses(const std::string& text);
    // Versioned tool manifest:
    //
    //   KansShaderProgram 1
    //   vertex=Toon.vert                  # stage paths, relative to the manifest
    //   fragment=Toon.frag
    //   default ShadowColor = 0.6 0.5 0.7 1   # material parameter defaults
    //   hint ShadowColor = color              # editor widgets: toggle, color,
    //   hint Softness = range 0 1             #   range <min> <max>
    //
    // Blank lines and lines starting with '#' are ignored.
    [[nodiscard]] std::variant<std::vector<ShaderStageSource>, AssetError>
    ParseShaderProgramSource(const std::string& text);
    [[nodiscard]] std::variant<ShaderParameterDefaults, AssetError>
    ParseShaderProgramDefaults(const std::string& text);
    [[nodiscard]] std::variant<ShaderParameterHints, AssetError> ParseShaderProgramHints(const std::string& text);
    // group <Group Name> = <member> <member> ... (ordered, unique members).
    [[nodiscard]] std::variant<std::vector<MaterialParameterGroup>, AssetError>
    ParseShaderProgramGroups(const std::string& text);
    // texture <sampler name> = white | black | normal (white when omitted).
    [[nodiscard]] std::variant<ShaderTextureFallbacks, AssetError>
    ParseShaderProgramTextureFallbacks(const std::string& text);
} // namespace Kans
