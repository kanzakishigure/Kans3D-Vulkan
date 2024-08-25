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
		m_AssetRegistry.LoadAllAssetMetaData();
	}

	Ref<Asset> EditorAssetManager::GetAsset(AssetHandle handle)
	{
		//return the memoryAssets
		if (m_MemoryAssets.count(handle))
		{
			return m_MemoryAssets.at(handle);
		}

		if (m_RegisteredAssets.count(handle))
		{
			return m_RegisteredAssets.at(handle);
		}

		//try load asset 
		Ref<AssetMetaData> metadata = m_AssetRegistry.GetAssetMetaData(handle);
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
			m_RegisteredAssets[metadata->Handle] = asset;
		}
		else
		{
			asset =  m_RegisteredAssets[metadata->Handle];
		}
		
		return asset;
	}

	
	void EditorAssetManager::AddAsset(Ref<Asset> asset)
	{
		
	}

}