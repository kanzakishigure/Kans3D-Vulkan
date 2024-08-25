#pragma once

#include "Kans3D/Asset/AssetMetaData.h"
#include<functional>

namespace Kans
{
	// AssetManager is a static class provide utility function for manager asset 
	class AssetManager
	{

	public: 
		using AssetChangeEventFn = std::function<void>;
	public:
		
		static void SetAssetChangeCallBack(const AssetChangeEventFn& callback);

		static const AssetMetaData& GetMetaData(AssetHandle handle);
		static const AssetMetaData& GetMetaData(const std::filesystem::path& path);
		static const AssetMetaData& GetMetaData(const Ref<Asset>& asset) { return GetMetaData(asset->Handle); };

		

	private:
		//The mapping of the disk asset we loaded to our project
		std::unordered_map<AssetHandle, Ref<Asset>> s_LoadedAssets;
		//we don't need to load all asset in our RAM we Just Load 
		std::unordered_map<AssetHandle, Ref<Asset>> s_RAMAssets;
	};
}

