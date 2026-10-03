#include "kspch.h"
#include "AssetRegistry.h"

namespace Kans
{

    AssetRegistry::AssetRegistry() {}

    void AssetRegistry::LoadAllAssetMetadata() {}

    const Ref<AssetMetadata> AssetRegistry::GetAssetMetadata(AssetID handle) const
    {
        if (m_AssetMetadatas.find(handle) != m_AssetMetadatas.end())
        {
            return m_AssetMetadatas.at(handle);
        }
        return nullptr;
    }

} // namespace Kans