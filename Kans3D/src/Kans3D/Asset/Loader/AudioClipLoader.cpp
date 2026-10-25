#include "kspch.h"
#include "Kans3D/Asset/Product/AudioProduct.h"
#include "Loader.h"

namespace Kans
{
    std::variant<Ref<AudioClipAsset>, AssetError> Loader<AudioClipAsset>::Load(const AssetLoadContext& context) const
    {
        if (!context.ID.IsValid() || context.Type != AssetType::AudioClip)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid AudioClip Product input"};
        auto decoded = DecodeAudioProduct(context.Data, context.Size);
        if (auto* error = std::get_if<AssetError>(&decoded))
            return *error;
        auto asset     = CreateRef<AudioClipAsset>(std::get<AudioClipData>(std::move(decoded)));
        asset->assetID = context.ID;
        return asset;
    }
} // namespace Kans
