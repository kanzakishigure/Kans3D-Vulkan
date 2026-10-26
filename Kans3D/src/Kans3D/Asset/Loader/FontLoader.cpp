#include "kspch.h"
#include "Kans3D/Asset/Product/FontProduct.h"
#include "Loader.h"
namespace Kans
{
    std::variant<Ref<FontAsset>, AssetError> Loader<FontAsset>::Load(const AssetLoadContext& c) const
    {
        if (!c.ID.IsValid() || c.Type != AssetType::Font)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid Font load input"};
        auto d = DecodeFontProduct(c.Data, c.Size);
        if (auto* e = std::get_if<AssetError>(&d))
            return *e;
        auto a     = CreateRef<FontAsset>(std::get<FontData>(std::move(d)));
        a->assetID = c.ID;
        return a;
    }
} // namespace Kans
