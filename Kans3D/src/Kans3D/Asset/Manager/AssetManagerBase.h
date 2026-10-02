#pragma once
#include "Kans3D/Asset/Asset.h"
#include <unordered_set>
#include <unordered_map>
namespace Kans
{

	class AssetManagerBase
	{
	public:
		AssetManagerBase() = default;
		virtual ~AssetManagerBase() = default;

		virtual Ref<Asset> GetAsset(AssetID assetID) = 0;
		virtual void AddAsset(Ref<Asset> asset) = 0;
		virtual bool RemoveAsset(AssetID assetID) = 0;
		virtual bool ReloadAsset(AssetID assetID) = 0;

		virtual bool IsMemoryAsset(AssetID assetID) = 0;
		virtual bool IsAssetLoaded(AssetID assetID) = 0;

		virtual std::unordered_set<AssetID> GetAllAssetsWithType(AssetType type) = 0;
		virtual const std::unordered_map<AssetID, Ref<Asset>>& GetMemoryAssets() = 0;
		virtual const std::unordered_map<AssetID, Ref<Asset>>& GetRegisteredAssets() = 0;


	};
}