#include "AssetImporter.h"
#include <unordered_set>
namespace Kans {
std::variant<ImportResult, AssetError>
AssetImporter::Import(const ImportContext &context) const {
  if (!context.SourceID.IsValid() || !context.SourceFile.is_absolute() ||
      !context.StagingDirectory.is_absolute())
    return AssetError{AssetErrorCode::InvalidArgument, "Invalid source import context"};
  auto importer = m_Registry.Find(context.SourceFile);
  if (!importer)
    return AssetError{AssetErrorCode::NotFound, "No importer registered for source format"};
  auto result = importer->Import(context);
  if (std::holds_alternative<AssetError>(result))
    return result;
  std::unordered_set<uint64_t> ids;
  std::unordered_set<AssetPath> paths;
  for (const auto &product : std::get<ImportResult>(result).Products) {
    const AssetPath path(product.StagedFile);
    if (!product.LocalID || product.Type == AssetType::None || !path.IsValid())
      return AssetError{AssetErrorCode::InvalidData, "Invalid Product descriptor"};
    if (!ids.insert(product.LocalID).second)
      return AssetError{AssetErrorCode::DuplicateID, "Duplicate Product LocalID"};
    if (!paths.insert(path).second)
      return AssetError{AssetErrorCode::DuplicatePath, "Duplicate staged Product path"};
    for (const auto &dependency : product.Dependencies)
      if (!dependency.IsValid())
        return AssetError{AssetErrorCode::InvalidData, "Invalid Product dependency"};
  }
  return result;
}
}
