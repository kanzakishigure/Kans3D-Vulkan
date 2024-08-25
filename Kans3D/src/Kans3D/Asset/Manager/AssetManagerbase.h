#pragma once
#include "Kans3D/Asset/Asset.h"
#include <unordered_set>
#include <unordered_map>
namespace Kans
{
	
	class AssetManagerbase 
	{
	public: 
		AssetManagerbase() = default;
		virtual ~AssetManagerbase() = default;

		virtual Ref<Asset> GetAsset(AssetHandle handle) = 0;
		virtual void AddAsset(Ref<Asset> asset) = 0;
		virtual bool RemoveAsset(AssetHandle handle) = 0;
		virtual bool ReloadAsset(AssetHandle handle) = 0;

		virtual bool IsMemoryAsset(AssetHandle handle) = 0;
		virtual bool IsAssetLoaded(AssetHandle handle) = 0;

		virtual std::unordered_set<AssetHandle> GetAllAssetsWithType(AssetType type) = 0;
		virtual const std::unordered_map<AssetHandle, Ref<Asset>>& GetMemoryAssets() = 0;
		virtual const std::unordered_map<AssetHandle, Ref<Asset>>& GetRegisteredAssets() = 0;

		
	};
}