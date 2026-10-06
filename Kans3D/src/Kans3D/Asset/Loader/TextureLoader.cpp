#include "kspch.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Loader.h"

namespace Kans
{
    std::variant<Ref<TextureAsset>, AssetError> Loader<TextureAsset>::Load(const AssetLoadContext& context) const
    {
        if (!context.ID.IsValid() || context.Type != AssetType::Texture || !context.Data || !context.Size)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid Texture Product input"};
        auto decoded = DecodeTextureProduct(context.Data, context.Size);
        if (const auto* error = std::get_if<AssetError>(&decoded))
            return *error;
        auto asset     = CreateRef<TextureAsset>(std::get<TextureData>(std::move(decoded)));
        asset->assetID = context.ID;
        return asset;
    }
} // namespace Kans
