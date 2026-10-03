#pragma once

#include <functional>
#include <sys/stat.h>
#include "Kans3D/Asset/AssetMetadata.h"

namespace Kans
{
    // AssetManager is a static class provide utility function for manager asset
    class AssetManager
    {

    public:
        using AssetChangeEventFn = std::function<void>;

    public:
        static void SetAssetChangeCallBack(const AssetChangeEventFn& callback);

        static const AssetMetadata& GetMetadata(AssetID assetID);
        static const AssetMetadata& GetMetadata(const std::filesystem::path& path);
        static const AssetMetadata& GetMetadata(const Ref<Asset>& asset) { return GetMetadata(asset->assetID); };

        static const Ref<Asset>& LoadRAMAsset(AssetID assetID) { return s_RAMAssets.at(assetID); }
        static const Ref<Asset>& LoadDiskAsset(AssetID assetID) { return s_LoadedAssets.at(assetID); }

    private:
        // The mapping of the disk asset we loaded to our project
        static std::unordered_map<AssetID, Ref<Asset>> s_LoadedAssets;
        // we don't need to load all asset in our RAM we Just Load
        static std::unordered_map<AssetID, Ref<Asset>> s_RAMAssets;
    };
} // namespace Kans
