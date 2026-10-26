#include "kspch.h"
#include "Kans3D/Asset/Product/UIProduct.h"
#include "Loader.h"
namespace Kans
{
    std::variant<Ref<UIAsset>, AssetError> Loader<UIAsset>::Load(const AssetLoadContext& c) const
    {
        if (!c.ID.IsValid() || c.Type != AssetType::UI)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid UI load input"};
        auto r = DecodeUIProduct(c.Data, c.Size);
        if (auto e = std::get_if<AssetError>(&r))
            return *e;
        auto a     = CreateRef<UIAsset>(std::get<UIData>(std::move(r)));
        a->assetID = c.ID;
        return a;
    }
} // namespace Kans
