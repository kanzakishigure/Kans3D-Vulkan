#include "kspch.h"
#include "ShaderProgramSource.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <sstream>

namespace Kans
{
    std::variant<std::vector<ShaderStageSource>, AssetError> ParseShaderProgramSource(const std::string& text)
    {
        if (text.empty() || text.size() > 65536 || text.find('\0') != std::string::npos)
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
            if (line.empty() || line[0] == '#' || line.rfind("default ", 0) == 0 || line.rfind("hint ", 0) == 0)
                continue;
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

    std::variant<ShaderParameterDefaults, AssetError> ParseShaderProgramDefaults(const std::string& text)
    {
        if (text.size() > 65536 || text.find('\0') != std::string::npos)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Shader program text budget"};
        ShaderParameterDefaults result;
        std::istringstream      lines(text);
        std::string             line;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.rfind("default ", 0) != 0)
                continue;
            const auto split = line.find('=');
            std::istringstream name(line.substr(8, split == std::string::npos ? std::string::npos : split - 8));
            std::string        key, extra;
            if (split == std::string::npos || !(name >> key) || (name >> extra))
                return AssetError {AssetErrorCode::InvalidData, "Shader default must be 'default <name> = <values>'"};
            std::istringstream  input(line.substr(split + 1));
            std::vector<double> values;
            std::string         token;
            while (input >> token)
            {
                char*  end   = nullptr;
                double value = std::strtod(token.c_str(), &end);
                if (token == "true" || token == "false")
                    value = token == "true" ? 1.0 : 0.0;
                else if (!end || *end || !std::isfinite(value))
                    return AssetError {AssetErrorCode::InvalidData, "Shader default '" + key + "' has a non-number"};
                values.push_back(value);
            }
            if (values.empty() || values.size() > 4096 || key.size() > 256 || !result.emplace(key, values).second)
                return AssetError {AssetErrorCode::InvalidData, "Invalid or duplicate Shader default '" + key + "'"};
        }
        return result;
    }

    std::variant<ShaderParameterHints, AssetError> ParseShaderProgramHints(const std::string& text)
    {
        if (text.size() > 65536 || text.find('\0') != std::string::npos)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Shader program text budget"};
        ShaderParameterHints result;
        std::istringstream   lines(text);
        std::string          line;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.rfind("hint ", 0) != 0)
                continue;
            const auto         split = line.find('=');
            std::istringstream name(line.substr(5, split == std::string::npos ? std::string::npos : split - 5));
            std::string        key, extra, kind;
            if (split == std::string::npos || !(name >> key) || (name >> extra))
                return AssetError {AssetErrorCode::InvalidData, "Shader hint must be 'hint <name> = <widget>'"};
            std::istringstream  input(line.substr(split + 1));
            ShaderParameterHint hint;
            input >> kind;
            if (kind == "toggle")
                hint.Type = ShaderParameterHint::Kind::Toggle;
            else if (kind == "color")
                hint.Type = ShaderParameterHint::Kind::Color;
            else if (kind == "range" && (input >> hint.Min >> hint.Max) && std::isfinite(hint.Min) &&
                     std::isfinite(hint.Max) && hint.Min < hint.Max)
                hint.Type = ShaderParameterHint::Kind::Range;
            else
                return AssetError {AssetErrorCode::InvalidData,
                                   "Shader hint '" + key + "' must be toggle, color or range <min> <max>"};
            if ((input >> extra) || key.size() > 256 || !result.emplace(key, hint).second)
                return AssetError {AssetErrorCode::InvalidData, "Invalid or duplicate Shader hint '" + key + "'"};
        }
        return result;
    }
} // namespace Kans
