#include "kspch.h"
#include "ShaderProgramSource.h"
#include <algorithm>
#include <set>
#include <sstream>

namespace Kans
{
    std::variant<std::vector<ShaderStageSource>, AssetError> ParseShaderProgramSource(const std::string& text)
    {
        if (text.empty() || text.size() > 8192 || text.find('\0') != std::string::npos)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Shader program text budget"};
        std::istringstream lines(text);
        std::string        line;
        const auto         next = [&] {
            if (!std::getline(lines, line))
                return false;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        };
        if (!next() || line != "KansShaderProgram 1")
            return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported Shader program header"};
        std::vector<ShaderStageSource> result;
        std::set<ShaderProductStage>   stages;
        while (next())
        {
            const auto split = line.find('=');
            if (split == std::string::npos)
                return AssetError {AssetErrorCode::InvalidData, "Invalid Shader program stage field"};
            const auto key   = line.substr(0, split);
            const auto path  = line.substr(split + 1);
            const auto stage = key == "vertex"   ? ShaderProductStage::Vertex :
                               key == "fragment" ? ShaderProductStage::Fragment :
                                                   ShaderProductStage::Compute;
            if ((key != "vertex" && key != "fragment" && key != "compute") || !stages.insert(stage).second ||
                path.size() > 4096 || path.find('\\') != std::string::npos || path.find(':') != std::string::npos)
                return AssetError {AssetErrorCode::InvalidData, "Invalid or duplicate Shader program stage"};
            AssetPath logical(std::filesystem::u8path(path));
            if (!logical.IsValid() ||
                std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                return AssetError {AssetErrorCode::InvalidData, "Invalid Shader program stage path"};
            result.push_back({stage, std::move(logical)});
        }
        if ((stages.count(ShaderProductStage::Compute) && stages.size() != 1) ||
            (!stages.count(ShaderProductStage::Compute) &&
             (stages.size() != 2 || !stages.count(ShaderProductStage::Vertex) ||
              !stages.count(ShaderProductStage::Fragment))))
            return AssetError {AssetErrorCode::InvalidData, "Shader program requires vertex+fragment or compute"};
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.Stage < b.Stage; });
        return result;
    }
} // namespace Kans
