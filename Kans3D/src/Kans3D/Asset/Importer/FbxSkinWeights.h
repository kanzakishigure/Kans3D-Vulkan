#pragma once
#include <vector>
namespace Kans
{
    // Empty for other formats. Binary FBX is adapted in memory; source bytes stay untouched.
    std::vector<char> PrepareBinaryFbx(const std::vector<char>& source);
} // namespace Kans
