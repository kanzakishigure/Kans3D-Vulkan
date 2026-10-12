#pragma once
#include <fstream>
#include <sstream>
#include "Kans3D/Asset/Importer/ShaderImporter.h"
namespace Kans
{
    inline std::string MultiPassText(const std::string& name)
    {
        std::ifstream input(std::string(KANS_TEST_MULTIPASS_DIR) + "/" + name, std::ios::binary);
        if (!input)
            throw std::runtime_error("Missing multi-pass fixture " + name);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }
    inline std::map<std::string, std::string> MultiPassSources()
    {
        std::map<std::string, std::string> sources;
        for (const auto& name : {"Surface.vert", "Surface.frag", "Hull.vert", "Hull.frag", "Parameters.glsl"})
            sources.emplace(name, MultiPassText(name));
        return sources;
    }
    inline ShaderData MultiPassShader()
    {
        auto compiled = CompileEmbeddedShaderProgram(MultiPassText("Outline.shaderprog"), MultiPassSources());
        if (auto* error = std::get_if<AssetError>(&compiled))
            throw std::runtime_error(error->Message);
        return std::get<ShaderData>(std::move(compiled));
    }
} // namespace Kans
