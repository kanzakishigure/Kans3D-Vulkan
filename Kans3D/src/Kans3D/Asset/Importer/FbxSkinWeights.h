#pragma once
#include <cstdint>
#include <vector>
namespace Kans
{
    // Empty for other formats. Binary FBX is adapted in memory; source bytes stay untouched.
    struct FbxSourceGeometry
    {
        std::vector<std::vector<uint32_t>> Polygons;
    };
    inline constexpr float FbxSourceColorMarker = -32768.0f;
    std::vector<char>      PrepareBinaryFbx(const std::vector<char>&        source,
                                            std::vector<FbxSourceGeometry>* geometries = nullptr);
} // namespace Kans
