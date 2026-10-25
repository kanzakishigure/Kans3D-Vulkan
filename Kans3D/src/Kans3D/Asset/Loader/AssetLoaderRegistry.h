#pragma once
#include <functional>
#include <map>
#include "Loader.h"

namespace Kans
{
    using AssetDecodeFn = std::function<std::variant<Ref<Asset>, AssetError>(const AssetLoadContext&)>;

    // Maps a Product type to the function that decodes its bytes into a CPU asset.
    class AssetLoaderRegistry
    {
    public:
        [[nodiscard]] std::variant<std::monostate, AssetError> Register(AssetType type, AssetDecodeFn decoder);
        const AssetDecodeFn*                                   Find(AssetType type) const;
        static AssetLoaderRegistry                             CreateDefault();

    private:
        std::map<AssetType, AssetDecodeFn> m_Decoders;
    };
} // namespace Kans
