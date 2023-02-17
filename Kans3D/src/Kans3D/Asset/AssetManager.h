#pragma once

#include "Kans3D/Asset/AssetMetaData.h"
#include<functional>

namespace Kans
{
	class AssetManager
	{

	public: 
		using AssetChangeEventFn = std::function<void>;
	public:
		static void Init();
		static void SetAssetChangeCallBack(const AssetChangeEventFn& callback);
		static void ShutDown();

		static const AssetMetaData& GetMetaData(AssetHandle handle);
		static const AssetMetaData& GetMetaData(const std::filesystem::path& path);
		static const AssetMetaData& GetMetaData(const Ref<Asset>& asset) { return GetMetaData(asset->Handle); };

		template<typename AssetType>
		static Ref<AssetType> GetAsset(AssetHandle handle)
		{
			if (m_LoadedAsset.contains(handle))
			{
				Ref<Asset> asset = m_LoadedAsset.at(handle);
				if (asset->IsValid();)
				{
					asset->IncRefCount();
					return (AssetType)asset.get();
				}
			}
			return nullptr;
		}

	private:
		//The mapping of the disk asset we loaded to our project
		std::unordered_map<AssetHandle, Ref<Asset>> s_LoadedAssets;
		//we don't need to load all asset in our RAM we Just Load 
		std::unordered_map<AssetHandle, Ref<Asset>> s_RAMAssets;
	};
}

