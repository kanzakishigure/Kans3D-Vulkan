#include "Loader.h"

namespace Kans {
std::variant<Ref<StaticMesh>, AssetError>
Loader<StaticMesh>::Load(const AssetLoadContext &context) const {
  if (!context.ID.IsValid() || context.Type != AssetType::StaticMesh ||
      !context.Data || !context.Size)
    return AssetError{AssetErrorCode::InvalidArgument, "Invalid StaticMesh Product input"};
  // Do not fall back to FBX/Assimp or the old .ksmesh source-reference format.
  return AssetError{AssetErrorCode::NotImplemented,
                    "StaticMesh Product format and CPU decoder are not implemented"};
}
} // namespace Kans
