#pragma once

#include "SourceAssetDatabase.h"
#include <filesystem>
#include <variant>

namespace Kans {
class SourceAssetDatabaseSerializer {
public:
  // Local, rebuildable snapshot. Load checks schema/root/records, not
  // freshness; callers must reconcile with sources and .kmeta before trusting
  // this data.
  [[nodiscard]] static std::variant<SourceAssetDatabase, AssetError>
  Load(const std::filesystem::path &cachePath,
       const std::filesystem::path &assetRoot);

  [[nodiscard]] static std::variant<std::monostate, AssetError>
  Save(const std::filesystem::path &cachePath,
       const std::filesystem::path &assetRoot,
       const SourceAssetDatabase &database);
};
} // namespace Kans
