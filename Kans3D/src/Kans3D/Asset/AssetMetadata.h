#pragma once
#include "Kans3D/Asset/Asset.h"
#include <cstdint>
#include <filesystem>
namespace Kans {
class AssetMetadata {
public:
  AssetMetadata() {}
  ~AssetMetadata() = default;
  bool IsValid() { return assetID.IsValid(); }

public:
  std::filesystem::path FilePath;
  AssetType Type = AssetType::None;
  AssetID assetID;
  bool IsDataLoad = false;
};

} // namespace Kans
