#pragma once
#include "AssetImporterBase.h"

namespace Kans {
// One source model -> complete Mesh/Material/Skeleton/Animation Product set.
// CPU parser extraction and Product encoders are the next implementation step.
// Do not adapt the legacy MeshSource object (it also owns render resources).
class ModelImporter final : public AssetImporterBase {
public:
  uint32_t GetVersion() const override { return 1; }
  std::variant<ImportResult, AssetError>
  Import(const ImportContext &) const override {
    return AssetError{AssetErrorCode::NotImplemented,
                      "Model CPU import and Product generation are not implemented"};
  }
};
}
