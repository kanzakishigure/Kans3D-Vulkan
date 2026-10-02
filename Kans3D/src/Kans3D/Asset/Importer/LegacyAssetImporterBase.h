#pragma once
#include "Kans3D/Asset/AssetMetadata.h"
namespace Kans {
// Unregistered legacy stubs only. Not part of the Product pipeline.
class LegacyAssetImporterBase {
public:
  virtual ~LegacyAssetImporterBase() = default;
  virtual void Import(const Ref<AssetMetadata>&, Ref<Asset>&) const = 0;
  virtual bool TryLoadData(const Ref<AssetMetadata>&, Ref<Asset>&) const = 0;
};
}
