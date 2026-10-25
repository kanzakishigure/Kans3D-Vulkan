#include "kspch.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Loader.h"

namespace Kans
{
    std::variant<Ref<MeshAsset>, AssetError> Loader<MeshAsset>::Load(const AssetLoadContext& context) const
    {
        if (!context.ID.IsValid() || context.Type != AssetType::StaticMesh || !context.Data || !context.Size)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid mesh Product input"};
        auto decoded = DecodeMeshProduct(context.Data, context.Size);
        if (const auto* error = std::get_if<AssetError>(&decoded))
            return *error;
        auto asset     = CreateRef<MeshAsset>(std::get<MeshData>(std::move(decoded)));
        asset->assetID = context.ID;
        return asset;
    }
} // namespace Kans
