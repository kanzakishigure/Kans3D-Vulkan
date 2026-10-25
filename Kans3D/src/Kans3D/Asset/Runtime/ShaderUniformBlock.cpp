#include "kspch.h"
#include "ShaderUniformBlock.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace Kans
{
    std::variant<std::vector<std::byte>, AssetError>
    PackShaderUniformBlock(const ShaderProgramCode&               code,
                           uint32_t                               set,
                           uint32_t                               binding,
                           const std::vector<ShaderUniformWrite>& writes,
                           uint64_t                               maximumBytes)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "Uniform packing requires binary32");
        if (!maximumBytes || maximumBytes > 512ull * 1024 * 1024 || writes.size() > 4096 || set > 31 || binding > 65535)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid uniform packing limits/binding"};
        try
        {
            auto inspected = InspectShaderProgram(code);
            if (const auto* error = std::get_if<AssetError>(&inspected))
                return *error;
            const ShaderResourceBinding* selected = nullptr;
            for (const auto& stage : std::get<std::vector<ShaderStageReflection>>(inspected))
                for (const auto& resource : stage.Bindings)
                    if (resource.Set == set && resource.Binding == binding)
                        selected = &resource;
            if (!selected)
                return AssetError {AssetErrorCode::NotFound, "Uniform binding is absent"};
            if (selected->Kind != ShaderBindingKind::UniformBuffer || selected->BlockBytes > maximumBytes)
                return AssetError {AssetErrorCode::InvalidArgument, "Binding is not a bounded uniform block"};
            std::vector<std::byte>       output(static_cast<size_t>(selected->BlockBytes), std::byte {0});
            std::unordered_set<uint64_t> occupied;
            for (const auto& write : writes)
            {
                if (write.Path.empty() || write.Path.size() > 17)
                    return AssetError {AssetErrorCode::InvalidArgument, "Invalid uniform member path"};
                const auto*                members = &selected->Members;
                const ShaderUniformMember* member  = nullptr;
                uint64_t                   base = 0, end = selected->BlockBytes;
                for (const auto& step : write.Path)
                {
                    if (step.Member >= members->size())
                        return AssetError {AssetErrorCode::InvalidArgument, "Uniform member index is out of range"};
                    member = &(*members)[step.Member];
                    if (step.ArrayElement >= member->ArrayCount || member->Offset > end - base)
                        return AssetError {AssetErrorCode::InvalidArgument, "Uniform array index/span is out of range"};
                    base += member->Offset;
                    const auto displacement = uint64_t(step.ArrayElement) * member->ArrayStride;
                    if (displacement > end - base)
                        return AssetError {AssetErrorCode::InvalidData, "Uniform array displacement exceeds span"};
                    base += displacement;
                    const auto span = member->ArrayStride ? member->ArrayStride : member->Bytes;
                    if (span > end - base)
                        return AssetError {AssetErrorCode::InvalidData, "Uniform element exceeds parent span"};
                    end     = base + span;
                    members = &member->Members;
                }
                if (member->Type == ShaderUniformType::Struct)
                    return AssetError {AssetErrorCode::InvalidArgument,
                                       "Uniform writes must select a scalar/vector/matrix leaf"};
                const bool floating    = std::holds_alternative<std::vector<float>>(write.Values);
                const bool signedValue = std::holds_alternative<std::vector<int32_t>>(write.Values);
                if ((member->Type == ShaderUniformType::Float && !floating) ||
                    (member->Type == ShaderUniformType::Signed && !signedValue) ||
                    ((member->Type == ShaderUniformType::Unsigned || member->Type == ShaderUniformType::Boolean) &&
                     (floating || signedValue)))
                    return AssetError {AssetErrorCode::InvalidArgument, "Uniform value scalar type mismatch"};
                const auto count = std::visit([](const auto& values) { return values.size(); }, write.Values);
                if (count != size_t(member->Components) * member->Columns)
                    return AssetError {AssetErrorCode::InvalidArgument, "Uniform value shape mismatch"};
                for (uint32_t column = 0; column < member->Columns; ++column)
                    for (uint32_t row = 0; row < member->Components; ++row)
                    {
                        const size_t index = size_t(column) * member->Components + row;
                        uint32_t     word  = 0;
                        if (floating)
                        {
                            const auto value = std::get<std::vector<float>>(write.Values)[index];
                            if (!std::isfinite(value))
                                return AssetError {AssetErrorCode::InvalidArgument, "Non-finite uniform value"};
                            std::memcpy(&word, &value, 4);
                        }
                        else if (signedValue)
                            word = static_cast<uint32_t>(std::get<std::vector<int32_t>>(write.Values)[index]);
                        else
                            word = std::get<std::vector<uint32_t>>(write.Values)[index];
                        if (member->Type == ShaderUniformType::Boolean && word > 1)
                            return AssetError {AssetErrorCode::InvalidArgument, "Boolean uniforms require zero or one"};
                        const auto displacement =
                            member->Columns == 1 ? uint64_t(row) * 4 :
                            member->RowMajor     ? uint64_t(row) * member->MatrixStride + uint64_t(column) * 4 :
                                                   uint64_t(column) * member->MatrixStride + uint64_t(row) * 4;
                        if (displacement > end - base || 4 > end - base - displacement)
                            return AssetError {AssetErrorCode::InvalidData, "Uniform scalar exceeds reflected span"};
                        const auto offset = base + displacement;
                        if (!occupied.insert(offset).second)
                            return AssetError {AssetErrorCode::InvalidArgument, "Overlapping uniform writes"};
                        for (size_t byte = 0; byte < 4; ++byte)
                            output[static_cast<size_t>(offset) + byte] =
                                static_cast<std::byte>((word >> (8 * byte)) & 255);
                    }
            }
            return output;
        }
        catch (const std::exception& error)
        {
            return AssetError {AssetErrorCode::InvalidData,
                               "Uniform packing failed: " + std::string(error.what()).substr(0, 2048)};
        }
    }
} // namespace Kans
