#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
namespace Kans {
// hash function interface
class Hash {
public:
  static void Init();
  static void Shutdown();
  static constexpr size_t None = 0;
  static std::string GenerateMD5Hash(const std::string &source);
  static uint64_t Generate64MD5Hash(const std::string &source);
  static size_t HashCombine(const size_t seed, const size_t value);
};

} // namespace Kans
