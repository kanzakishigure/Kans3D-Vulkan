#pragma once
#include "ShaderData.h"
namespace Kans
{
    struct ShaderUniformPathElement
    {
        uint32_t Member       = 0;
        uint32_t ArrayElement = 0;
    };
    struct ShaderUniformWrite
    {
        std::vector<ShaderUniformPathElement> Path;
        // Matrices are supplied column-major, independent of the declared layout.
        std::variant<std::vector<float>, std::vector<int32_t>, std::vector<uint32_t>> Values;
    };
    // Fresh validated reflection, zero padding and canonical little-endian words.
    // Returns one buffer instance; descriptor-array selection belongs to binding.
    [[nodiscard]] std::variant<std::vector<std::byte>, AssetError>
    PackShaderUniformBlock(const ShaderProgramCode&               code,
                           uint32_t                               set,
                           uint32_t                               binding,
                           const std::vector<ShaderUniformWrite>& writes,
                           uint64_t                               maximumBytes = 16 * 1024 * 1024);
} // namespace Kans
