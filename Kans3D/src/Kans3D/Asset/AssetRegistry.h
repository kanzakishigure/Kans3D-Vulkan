#pragma once
#include <filesystem>
#include <unordered_map>
#include "Kans3D/Asset/Asset.h"
#include "AssetMetadata.h"
namespace Kans
{

    class AssetRegistry
    {
    public:
        AssetRegistry();
        ~AssetRegistry() = default;

        void                     LoadAllAssetMetadata();
        const Ref<AssetMetadata> GetAssetMetadata(AssetID handle) const;

    private:
        std::unordered_map<AssetID, Ref<AssetMetadata>> m_AssetMetadatas;
    };

} // namespace Kans