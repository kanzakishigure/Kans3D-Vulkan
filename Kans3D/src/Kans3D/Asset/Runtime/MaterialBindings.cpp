#include "kspch.h"
#include "MaterialBindings.h"
#include "MaterialShaderInterface.h"
#include <map>
#include <set>
#include <tuple>

namespace Kans
{
    std::variant<std::vector<MaterialUniformBlock>, AssetError>
    PackMaterialUniforms(const MaterialData&          data,
                         const ShaderProgramCode&     code,
                         const MaterialBindingRecipe& recipe,
                         uint64_t                     maximumUniformBytes)
    {
        if (!maximumUniformBytes || maximumUniformBytes > 512ull * 1024 * 1024 || recipe.Uniforms.size() > 4096 ||
            recipe.Textures.size() > 4096 - recipe.Uniforms.size())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid Material recipe budgets"};
        auto valid = ValidateMaterialData(data);
        if (auto error = std::get_if<AssetError>(&valid))
            return *error;
        try
        {
            std::vector<MaterialUniformBlock> result;
            auto                              inspected = InspectShaderProgram(code);
            if (const auto* error = std::get_if<AssetError>(&inspected))
                return *error;
            const auto& reflection = std::get<std::vector<ShaderStageReflection>>(inspected);
            auto        contract   = ValidateMaterialShaderInterface(reflection);
            if (const auto* error = std::get_if<AssetError>(&contract))
                return *error;
            std::map<std::pair<uint32_t, uint32_t>, ShaderResourceBinding> resources;
            for (auto& binding : MaterialShaderResources(reflection))
                resources.emplace(std::make_pair(binding.Set, binding.Binding), std::move(binding));
            size_t required = 0;
            for (const auto& entry : resources)
            {
                if (entry.second.Count > 4096 - required)
                    return AssetError {AssetErrorCode::InvalidData, "Material descriptor count exceeds limits"};
                if (entry.second.Kind == ShaderBindingKind::UniformBuffer)
                    required += entry.second.Count;
            }
            std::set<std::tuple<uint32_t, uint32_t, uint32_t>> assigned;
            const auto assign = [&](uint32_t set, uint32_t binding, uint32_t element, ShaderBindingKind kind) {
                const auto found = resources.find({set, binding});
                return found != resources.end() && found->second.Kind == kind && element < found->second.Count &&
                       assigned.emplace(set, binding, element).second;
            };
            uint64_t bytes  = 0;
            size_t   writes = 0;
            for (const auto& assignment : recipe.Uniforms)
            {
                if (!assign(assignment.Set, assignment.Binding, assignment.Element, ShaderBindingKind::UniformBuffer) ||
                    assignment.Parameters.size() > 4096 - writes ||
                    assignment.Values.size() > 4096 - writes - assignment.Parameters.size())
                    return AssetError {AssetErrorCode::InvalidArgument,
                                       "Invalid/duplicate Material uniform assignment"};
                writes += assignment.Parameters.size() + assignment.Values.size();
                std::vector<ShaderUniformWrite> values;
                for (const auto& parameter : assignment.Parameters)
                {
                    if (parameter.Path.empty() || parameter.Path.size() > 17)
                        return AssetError {AssetErrorCode::InvalidArgument, "Invalid Material parameter path"};
                    std::vector<float> value;
                    switch (parameter.Parameter)
                    {
                        case MaterialParameter::BaseColor:
                            value.assign(data.BaseColor.begin(), data.BaseColor.end());
                            break;
                        case MaterialParameter::Metallic:
                            value = {data.Metallic};
                            break;
                        case MaterialParameter::Roughness:
                            value = {data.Roughness};
                            break;
                        default:
                            return AssetError {AssetErrorCode::InvalidArgument, "Unknown Material parameter"};
                    }
                    values.push_back({parameter.Path, std::move(value)});
                }
                for (const auto& value : assignment.Values)
                {
                    if (value.Path.empty() || value.Path.size() > 17 ||
                        std::visit([](const auto& v) { return v.size(); }, value.Values) > 16)
                        return AssetError {AssetErrorCode::InvalidArgument, "Invalid Material constant write"};
                    values.push_back(value);
                }
                const auto size = resources.at({assignment.Set, assignment.Binding}).BlockBytes;
                if (size > maximumUniformBytes - bytes)
                    return AssetError {AssetErrorCode::InvalidArgument, "Material uniform byte budget exceeded"};
                auto packed = PackShaderUniformBlock(code, assignment.Set, assignment.Binding, values, size);
                if (const auto* error = std::get_if<AssetError>(&packed))
                    return *error;
                bytes += size;
                result.push_back({assignment.Set,
                                  assignment.Binding,
                                  assignment.Element,
                                  std::get<std::vector<std::byte>>(std::move(packed))});
            }
            for (const auto& assignment : recipe.Textures)
            {
                if (!assign(assignment.Set,
                            assignment.Binding,
                            assignment.Element,
                            ShaderBindingKind::CombinedImageSampler) ||
                    assignment.TextureIndex >= data.Textures.size())
                    return AssetError {AssetErrorCode::InvalidArgument,
                                       "Invalid/duplicate Material sampler assignment"};
            }
            if (result.size() != required)
                return AssetError {AssetErrorCode::InvalidArgument,
                                   "Material recipe does not cover every material uniform block"};
            return result;
        }
        catch (const std::exception&)
        {
            return AssetError {AssetErrorCode::InvalidData, "Cannot pack Material Shader layout"};
        }
    }
} // namespace Kans
