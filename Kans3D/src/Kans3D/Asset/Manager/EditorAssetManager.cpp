#include "kspch.h"
#include "EditorAssetManager.h"
namespace Kans
{

    EditorAssetManager::EditorAssetManager()
    {
        // load asset metadata cache to registry
        // load initial scene asset to memory
        m_AssetRegistry.LoadAllAssetMetadata();
    }

    Ref<Asset> EditorAssetManager::GetAsset(AssetID assetID)
    {
        // return the memoryAssets
        if (m_MemoryAssets.count(assetID))
        {
            return m_MemoryAssets.at(assetID);
        }

        if (m_RegisteredAssets.count(assetID))
        {
            return m_RegisteredAssets.at(assetID);
        }

        // try load asset
        Ref<AssetMetadata> metadata = m_AssetRegistry.GetAssetMetadata(assetID);
        if (metadata == nullptr)
        {
            return nullptr;
        }
        if (!metadata->IsValid())
        {
            return nullptr;
        }

        // Product lookup/byte IO and typed Loader dispatch are not wired yet.
        // Never substitute source importing for runtime asset loading.
        CORE_WARN("Product loading is not implemented for requested asset");
        return nullptr;
    }

    void EditorAssetManager::AddAsset(Ref<Asset> asset) {}

} // namespace Kans
