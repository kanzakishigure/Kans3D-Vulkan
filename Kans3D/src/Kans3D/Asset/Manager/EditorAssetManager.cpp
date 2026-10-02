#include "kspch.h"
#include "EditorAssetManager.h"
#include "Kans3D/Asset/Importer/AssetImporter.h"
namespace Kans
{

	EditorAssetManager::EditorAssetManager()
	{
		//load asset metadata cache to registry
		AssetImporter::Init();
		//load initial scene asset to memory
		m_AssetRegistry.LoadAllAssetMetadata();
	}

	Ref<Asset> EditorAssetManager::GetAsset(AssetID assetID)
	{
		//return the memoryAssets
		if (m_MemoryAssets.count(assetID))
		{
			return m_MemoryAssets.at(assetID);
		}

		if (m_RegisteredAssets.count(assetID))
		{
			return m_RegisteredAssets.at(assetID);
		}

		//try load asset 
		Ref<AssetMetadata> metadata = m_AssetRegistry.GetAssetMetadata(assetID);
		if (metadata == nullptr)
		{
			return nullptr;
		}
		if (!metadata->IsValid())
		{
			return nullptr;
		}

		Ref<Asset> asset = nullptr;
		if (!metadata->IsDataLoad)
		{
			AssetImporter::Import(metadata,asset);
			m_RegisteredAssets[metadata->assetID] = asset;
		}
		else
		{
			asset =  m_RegisteredAssets[metadata->assetID];
		}
		
		return asset;
	}

	
	void EditorAssetManager::AddAsset(Ref<Asset> asset)
	{
		
	}

}