#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace Kans
{
    std::string ComputeContentDigest(const std::vector<std::byte>& bytes);
    bool        IsContentDigest(const std::string& digest);
} // namespace Kans
