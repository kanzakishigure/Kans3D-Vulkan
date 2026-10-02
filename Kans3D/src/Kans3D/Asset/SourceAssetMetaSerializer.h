#pragma once

#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Asset/SourceAssetMetaData.h"

#include <filesystem>
#include <variant>

namespace Kans {

class SourceAssetMetaSerializer {
public:
  // The source path is project-relative and cannot be inferred from a .kmeta
  // file alone. Filesystem observations remain the metadata defaults.
  static std::variant<SourceAssetMetadata, AssetError>
  Load(const std::filesystem::path &metaPath, const AssetPath &sourcePath);
  static std::variant<std::monostate, AssetError>
  Save(const std::filesystem::path &metaPath,
       const SourceAssetMetadata &metadata);
};

} // namespace Kans
