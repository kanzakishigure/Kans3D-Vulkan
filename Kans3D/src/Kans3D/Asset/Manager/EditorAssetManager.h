#pragma once

#include "Kans3D/Asset/AssetRegistry.h"
#include "AssetManagerBase.h"
namespace Kans
{
    class EditorAssetManager : AssetManagerBase
    {

    public:
        EditorAssetManager();

        Ref<Asset> GetAsset(AssetID assetID) override;
        void       AddAsset(Ref<Asset> asset) override;
        bool       RemoveAsset(AssetID assetID) override;
        bool       ReloadAsset(AssetID assetID) override;
        bool       IsMemoryAsset(AssetID assetID) override;
        bool       IsAssetLoaded(AssetID assetID) override;

        std::unordered_set<AssetID>                    GetAllAssetsWithType(AssetType type) override;
        const std::unordered_map<AssetID, Ref<Asset>>& GetMemoryAssets() override;
        const std::unordered_map<AssetID, Ref<Asset>>& GetRegisteredAssets() override;

    private:
        std::unordered_map<AssetID, Ref<Asset>> m_MemoryAssets;
        std::unordered_map<AssetID, Ref<Asset>> m_RegisteredAssets;

        AssetRegistry m_AssetRegistry;
    };
} // namespace Kans