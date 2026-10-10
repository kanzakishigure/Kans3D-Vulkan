#include "kspch.h"
#include "MaterialSource.h"
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Product/MaterialRenderStateYaml.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "Kans3D/Asset/Runtime/MaterialShaderInterface.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"

namespace Kans
{
    namespace
    {
        constexpr size_t MaximumEntries = 4096;
        constexpr size_t MaximumValues  = 4096;

        AssetError Invalid(const std::string& message)
        {
            return AssetError {AssetErrorCode::InvalidData, "Material source: " + message};
        }

        bool ValidName(const std::string& name)
        {
            return !name.empty() && name.size() <= 256 &&
                   std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 32 && c != 127; });
        }

        double Number(const YAML::Node& node)
        {
            if (!node.IsScalar())
                throw std::runtime_error("parameter values must be numbers or booleans");
            double value = 0;
            if (YAML::convert<double>::decode(node, value))
                return value;
            bool flag = false;
            if (YAML::convert<bool>::decode(node, flag))
                return flag ? 1.0 : 0.0;
            throw std::runtime_error("parameter value '" + node.Scalar() + "' is not a number");
        }

        void Flatten(const YAML::Node& node, std::vector<double>& values, int depth)
        {
            if (node.IsSequence() && depth < 2)
            {
                for (const auto& element : node)
                    Flatten(element, values, depth + 1);
                return;
            }
            if (values.size() >= MaximumValues)
                throw std::runtime_error("too many parameter values");
            values.push_back(Number(node));
        }

        // Shortest decimal text that reads back as the same float.
        std::string FormatNumber(double value)
        {
            char buffer[32];
            for (int precision = 6; precision <= 9; ++precision)
            {
                std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
                if (static_cast<float>(std::strtod(buffer, nullptr)) == static_cast<float>(value))
                    break;
            }
            return buffer;
        }

        MaterialSource CanonicalSource(MaterialSource source)
        {
            if (!IsStandardShader(source.Shader))
                return source;
            const auto migrate = [](auto& values, const char* oldName, const char* newName) {
                auto node = values.extract(oldName);
                if (!node.empty())
                {
                    if (values.count(newName))
                        throw std::runtime_error(std::string("both legacy and Standard names supplied: ") + newName);
                    node.key() = newName;
                    values.insert(std::move(node));
                }
            };
            migrate(source.Parameters, "base_color", "BaseColor");
            migrate(source.Parameters, "metallic", "Metallic");
            migrate(source.Parameters, "roughness", "Roughness");
            // Old emissive textures emitted without a factor.
            if (source.Textures.count("emissive") && !source.Parameters.count("EmissiveColor"))
                source.Parameters["EmissiveColor"] = {1, 1, 1};
            migrate(source.Textures, "base_color", "BaseColorMap");
            migrate(source.Textures, "normal", "NormalMap");
            migrate(source.Textures, "metallic_roughness", "MetallicRoughnessMap");
            migrate(source.Textures, "occlusion", "OcclusionMap");
            migrate(source.Textures, "emissive", "EmissiveMap");
            return source;
        }

        std::variant<std::vector<ShaderStageReflection>, AssetError> MaterialReflection(const ShaderData& shader)
        {
            auto inspected = InspectShaderProgram(shader.Code);
            if (auto* error = std::get_if<AssetError>(&inspected))
                return std::move(*error);
            auto contract = ValidateMaterialShaderInterface(std::get<std::vector<ShaderStageReflection>>(inspected));
            if (auto* error = std::get_if<AssetError>(&contract))
                return std::move(*error);
            return inspected;
        }
    } // namespace

    std::variant<MaterialSource, AssetError> ParseMaterialSource(const std::byte* bytes, size_t size)
    {
        if (!bytes || !size || size > 1024 * 1024)
            return Invalid("invalid size");
        try
        {
            std::string text(reinterpret_cast<const char*>(bytes), size);
            if (text.find('\0') != std::string::npos)
                return Invalid("contains NUL");
            auto documents = YAML::LoadAll(text);
            if (documents.size() != 1 || !documents.front().IsMap())
                return Invalid("requires one YAML mapping");
            const auto            root = documents.front();
            std::set<std::string> fields;
            for (const auto& entry : root)
            {
                const auto field = entry.first.as<std::string>();
                if (!fields.insert(field).second ||
                    !std::set<std::string> {"magic", "version", "name", "shader", "parameters", "textures", "render"}
                         .count(field))
                    return Invalid("unknown or duplicate field '" + field + "'");
            }
            if (!root["magic"] || root["magic"].as<std::string>() != "KansMaterial")
                return Invalid("missing 'magic: KansMaterial'");
            if (!root["version"] || (root["version"].as<uint32_t>() != 2 && root["version"].as<uint32_t>() != 3))
                return AssetError {AssetErrorCode::UnsupportedVersion, "Material source requires version 2 or 3"};

            MaterialSource source;
            source.Render = ReadMaterialRenderState(root["render"]);
            source.Name = root["name"] ? root["name"].as<std::string>() : std::string();
            if (source.Name.size() > 4096)
                return Invalid("name is too long");
            if (root["shader"] && !root["shader"].IsNull())
            {
                const auto shader = root["shader"].as<std::string>();
                source.Shader     = AssetID::FromString(shader);
                if (!shader.empty() && !source.Shader.IsValid())
                    return Invalid("invalid shader AssetID '" + shader + "'");
            }
            if (const auto parameters = root["parameters"]; parameters && !parameters.IsNull())
            {
                if (!parameters.IsMap() || parameters.size() > MaximumEntries)
                    return Invalid("'parameters' must be a mapping");
                for (const auto& entry : parameters)
                {
                    const auto name = entry.first.as<std::string>();
                    if (!ValidName(name) || source.Parameters.count(name))
                        return Invalid("invalid or duplicate parameter '" + name + "'");
                    Flatten(entry.second, source.Parameters[name], 0);
                }
            }
            if (const auto textures = root["textures"]; textures && !textures.IsNull())
            {
                if (!textures.IsMap() || textures.size() > MaximumEntries)
                    return Invalid("'textures' must be a mapping");
                for (const auto& entry : textures)
                {
                    const auto name = entry.first.as<std::string>();
                    const auto id   = AssetID::FromString(entry.second.as<std::string>());
                    if (!ValidName(name) || !id.IsValid() || !source.Textures.emplace(name, id).second)
                        return Invalid("invalid texture entry '" + name + "'");
                }
            }
            return CanonicalSource(std::move(source));
        }
        catch (const std::exception& error)
        {
            return Invalid(std::string(error.what()).substr(0, 512));
        }
    }

    std::string SerializeMaterialSource(const MaterialSource& input)
    {
        const auto    source = CanonicalSource(input);
        YAML::Emitter out;
        out.SetFloatPrecision(9);
        out << YAML::BeginMap;
        out << YAML::Key << "magic" << YAML::Value << "KansMaterial";
        out << YAML::Key << "version" << YAML::Value << 3;
        out << YAML::Key << "name" << YAML::Value << source.Name;
        out << YAML::Key << "shader" << YAML::Value
            << (source.Shader.IsValid() ? source.Shader.ToString() : std::string());
        EmitMaterialRenderState(out, source.Render);
        out << YAML::Key << "parameters" << YAML::Value << YAML::BeginMap;
        for (const auto& [name, values] : source.Parameters)
        {
            out << YAML::Key << name << YAML::Value;
            if (values.size() == 1)
                out << FormatNumber(values[0]);
            else
            {
                out << YAML::Flow << YAML::BeginSeq;
                for (double value : values)
                    out << FormatNumber(value);
                out << YAML::EndSeq;
            }
        }
        out << YAML::EndMap;
        out << YAML::Key << "textures" << YAML::Value << YAML::BeginMap;
        for (const auto& [name, id] : source.Textures)
            out << YAML::Key << name << YAML::Value << id.ToString();
        out << YAML::EndMap;
        out << YAML::EndMap;
        return std::string(out.c_str()) + "\n";
    }

    std::variant<MaterialSchema, AssetError> DescribeMaterial(const ShaderData* shader)
    {
        if (!shader)
            shader = &GetStandardShader();
        try
        {
            auto reflection = MaterialReflection(*shader);
            if (auto* error = std::get_if<AssetError>(&reflection))
                return std::move(*error);
            MaterialSchema        schema;
            std::set<std::string> names;
            for (const auto& resource :
                 MaterialShaderResources(std::get<std::vector<ShaderStageReflection>>(reflection)))
            {
                if (resource.Count != 1)
                    return Invalid("descriptor arrays are not supported in materials ('" + resource.Name + "')");
                if (resource.Kind == ShaderBindingKind::CombinedImageSampler)
                {
                    if (!names.insert(resource.Name).second)
                        return Invalid("duplicate material name '" + resource.Name + "'");
                    schema.Textures.push_back(resource.Name);
                    continue;
                }
                for (const auto& member : resource.Members)
                {
                    if (member.Type == ShaderUniformType::Struct)
                        continue;
                    if (!names.insert(member.Name).second)
                        return Invalid("duplicate material name '" + member.Name + "'");
                    schema.Parameters.push_back(
                        {member.Name, member.Type, member.Components, member.Columns, member.ArrayCount});
                }
            }
            for (const auto& [name, values] : shader->ParameterDefaults)
            {
                const auto found = std::find_if(schema.Parameters.begin(),
                                                schema.Parameters.end(),
                                                [&](const auto& parameter) { return parameter.Name == name; });
                if (found == schema.Parameters.end() || found->ValueCount() != values.size())
                    return Invalid("Shader default '" + name + "' does not match a material parameter of " +
                                   std::to_string(values.size()) + " values");
                schema.Defaults.emplace(name, values);
            }
            for (const auto& [name, hint] : shader->ParameterHints)
            {
                const auto found = std::find_if(schema.Parameters.begin(),
                                                schema.Parameters.end(),
                                                [&](const auto& parameter) { return parameter.Name == name; });
                if (found == schema.Parameters.end())
                    return Invalid("Shader hint '" + name + "' does not name a material parameter");
                if (hint.Type == ShaderParameterHint::Kind::Color &&
                    (found->Type != ShaderUniformType::Float || found->Columns != 1 || found->ArrayCount != 1 ||
                     found->Components < 3))
                    return Invalid("Shader hint '" + name + "': color needs a vec3 or vec4");
                if (hint.Type == ShaderParameterHint::Kind::Range && found->Type == ShaderUniformType::Boolean)
                    return Invalid("Shader hint '" + name + "': range does not apply to a bool");
                found->Hint = hint;
            }
            for (const auto& [name, role] : shader->TextureFallbacks)
                if (std::find(schema.Textures.begin(), schema.Textures.end(), name) == schema.Textures.end() ||
                    uint32_t(role) > uint32_t(ShaderTextureFallback::Normal))
                    return Invalid("Shader texture fallback '" + name + "' does not name a material sampler");
            return schema;
        }
        catch (const std::exception& error)
        {
            return Invalid(std::string(error.what()).substr(0, 512));
        }
    }

    std::variant<MaterialData, AssetError> ResolveMaterialSource(const MaterialSource& input, const ShaderData* shader)
    {
        MaterialSource source;
        try
        {
            source = CanonicalSource(input);
        }
        catch (const std::exception& error)
        {
            return Invalid(error.what());
        }
        const bool standard = IsStandardShader(source.Shader);
        if ((!standard && !shader) || (!source.Shader.IsValid() && shader && shader != &GetStandardShader() &&
                                       shader != &GetStandardShaderAsset()->GetData()))
            return AssetError {AssetErrorCode::InvalidArgument, "Material source and Shader do not match"};
        if (standard)
            shader = &GetStandardShader();
        auto described = DescribeMaterial(shader);
        if (auto* error = std::get_if<AssetError>(&described))
            return std::move(*error);
        const auto& schema = std::get<MaterialSchema>(described);
        const auto  info   = [&](const std::string& name) -> const MaterialParameterInfo* {
            for (const auto& parameter : schema.Parameters)
                if (parameter.Name == name)
                    return &parameter;
            return nullptr;
        };
        // Parameters the source leaves unset take the schema defaults.
        auto parameters = schema.Defaults;
        for (const auto& [name, values] : source.Parameters)
            parameters[name] = values;
        for (const auto& [name, values] : parameters)
        {
            const auto* parameter = info(name);
            if (!parameter)
                return Invalid("unknown parameter '" + name + "'");
            if (values.size() != parameter->ValueCount())
                return Invalid("parameter '" + name + "' needs " + std::to_string(parameter->ValueCount()) +
                               " values, got " + std::to_string(values.size()));
            for (double value : values)
                if (!std::isfinite(value) ||
                    (parameter->Type != ShaderUniformType::Float &&
                     (value != std::floor(value) ||
                      (parameter->Type == ShaderUniformType::Signed && (value < INT32_MIN || value > INT32_MAX)) ||
                      (parameter->Type == ShaderUniformType::Unsigned && (value < 0 || value > UINT32_MAX)) ||
                      (parameter->Type == ShaderUniformType::Boolean && value != 0 && value != 1))))
                    return Invalid("parameter '" + name + "' has a value of the wrong type");
        }
        for (const auto& [name, id] : source.Textures)
            if (std::find(schema.Textures.begin(), schema.Textures.end(), name) == schema.Textures.end())
                return Invalid("unknown texture '" + name + "'");

        MaterialData data;
        data.Name = source.Name;
        data.Render = source.Render;
        if (!IsValidMaterialRenderState(data.Render))
            return Invalid("invalid render state");
        if (standard)
        {
            const auto& color = parameters.at("BaseColor");
            for (size_t i = 0; i < 4; ++i)
                data.BaseColor[i] = static_cast<float>(color[i]);
            data.Metallic  = static_cast<float>(parameters.at("Metallic")[0]);
            data.Roughness = static_cast<float>(parameters.at("Roughness")[0]);
        }

        try
        {
            data.Shader     = standard ? StandardShaderID() : source.Shader;
            data.Bindings  = MaterialBindingRecipe {};
            auto& recipe   = *data.Bindings;
            auto  inspected = MaterialReflection(*shader);
            if (auto* error = std::get_if<AssetError>(&inspected))
                return std::move(*error);
            for (const auto& resource :
                 MaterialShaderResources(std::get<std::vector<ShaderStageReflection>>(inspected)))
            {
                if (resource.Kind == ShaderBindingKind::CombinedImageSampler)
                {
                    const auto found = source.Textures.find(resource.Name);
                    if (found == source.Textures.end())
                        continue;
                    uint32_t semantic = uint32_t(MaterialTextureSemantic::Unknown);
                    if (standard)
                    {
                        const std::map<std::string, MaterialTextureSemantic> roles = {
                            {"BaseColorMap", MaterialTextureSemantic::BaseColor},
                            {"NormalMap", MaterialTextureSemantic::Normal},
                            {"MetallicRoughnessMap", MaterialTextureSemantic::MetallicRoughness},
                            {"OcclusionMap", MaterialTextureSemantic::AmbientOcclusion},
                            {"EmissiveMap", MaterialTextureSemantic::EmissionColor}};
                        semantic = uint32_t(roles.at(resource.Name));
                    }
                    data.Textures.push_back({semantic, 0, found->second});
                    recipe.Textures.push_back({MaterialDescriptorSet,
                                               resource.Binding,
                                               0,
                                               static_cast<uint32_t>(data.Textures.size() - 1)});
                    continue;
                }
                MaterialUniformAssignment assignment;
                assignment.Set     = MaterialDescriptorSet;
                assignment.Binding = resource.Binding;
                for (uint32_t index = 0; index < resource.Members.size(); ++index)
                {
                    const auto& member = resource.Members[index];
                    const auto  found  = parameters.find(member.Name);
                    if (member.Type == ShaderUniformType::Struct || found == parameters.end())
                        continue;
                    const size_t stride = size_t(member.Components) * member.Columns;
                    for (uint32_t element = 0; element < member.ArrayCount; ++element)
                    {
                        const auto begin = found->second.begin() + element * stride;
                        ShaderUniformWrite write;
                        write.Path = {{index, element}};
                        if (member.Type == ShaderUniformType::Float)
                            write.Values = std::vector<float>(begin, begin + stride);
                        else if (member.Type == ShaderUniformType::Signed)
                            write.Values = std::vector<int32_t>(begin, begin + stride);
                        else
                            write.Values = std::vector<uint32_t>(begin, begin + stride);
                        assignment.Values.push_back(std::move(write));
                    }
                }
                recipe.Uniforms.push_back(std::move(assignment));
            }
            auto packed = PackMaterialUniforms(data, shader->Code, recipe);
            if (auto* error = std::get_if<AssetError>(&packed))
                return std::move(*error);
            return data;
        }
        catch (const std::exception& error)
        {
            return Invalid(std::string(error.what()).substr(0, 512));
        }
    }
} // namespace Kans
