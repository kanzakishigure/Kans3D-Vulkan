#include "kspch.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Loader.h"

namespace Kans
{
    std::variant<Ref<ModelAsset>, AssetError> Loader<ModelAsset>::Load(const AssetLoadContext& context) const
    {
        if (!context.ID.IsValid() || context.Type != AssetType::Model || !context.Data || !context.Size)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid Model Product input"};
        auto decoded = DecodeModelProduct(context.Data, context.Size);
        if (const auto* error = std::get_if<AssetError>(&decoded))
            return *error;
        auto asset     = CreateRef<ModelAsset>(std::get<ModelData>(std::move(decoded)));
        asset->assetID = context.ID;
        return asset;
    }

    std::variant<Ref<MaterialDataAsset>, AssetError>
    Loader<MaterialDataAsset>::Load(const AssetLoadContext& context) const
    {
        if (!context.ID.IsValid() || context.Type != AssetType::Material || !context.Data || !context.Size)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid Material Product input"};
        auto decoded = DecodeMaterialProduct(context.Data, context.Size);
        if (const auto* error = std::get_if<AssetError>(&decoded))
            return *error;
        auto asset     = CreateRef<MaterialDataAsset>(std::get<MaterialData>(std::move(decoded)));
        asset->assetID = context.ID;
        return asset;
    }
} // namespace Kans
