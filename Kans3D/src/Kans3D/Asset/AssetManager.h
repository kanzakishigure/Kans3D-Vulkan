#pragma once

#include "AssetMetaData.h"

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

	private:
	};
}

