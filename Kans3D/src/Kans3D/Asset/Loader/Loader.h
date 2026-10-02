#pragma once
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"
#include <cstddef>
#include <variant>

namespace Kans {
class AssetLoadContext {
public:
  AssetID ID;
  AssetType Type = AssetType::None;
  // Borrowed Product bytes; caller owns storage for the duration of Load().
  const std::byte *Data = nullptr;
  size_t Size = 0;
};

template <typename T> class Loader;
class StaticMesh;
template <> class Loader<StaticMesh> {
public:
  // CPU-only Product decoding. No source paths, file IO, importer or GPU calls.
  [[nodiscard]] std::variant<Ref<StaticMesh>, AssetError>
  Load(const AssetLoadContext &context) const;
};
} // namespace Kans
