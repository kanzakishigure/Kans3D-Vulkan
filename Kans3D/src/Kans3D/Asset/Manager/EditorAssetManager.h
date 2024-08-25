#pragma once

#include "AssetManagerbase.h"
#include "Kans3D/Asset/AssetRegistry.h"
namespace Kans
{
	class EditorAssetManager : AssetManagerbase
	{

	public:
		EditorAssetManager();

		Ref<Asset> GetAsset(AssetHandle handle) override;
		void AddAsset(Ref<Asset> asset) override;
		bool RemoveAsset(AssetHandle handle) override;
		bool ReloadAsset(AssetHandle handle) override;
		bool IsMemoryAsset(AssetHandle handle) override;
		bool IsAssetLoaded(AssetHandle handle) override;

		std::unordered_set<AssetHandle> GetAllAssetsWithType(AssetType type) override;
		const std::unordered_map<AssetHandle, Ref<Asset>>& GetMemoryAssets() override;
		const std::unordered_map<AssetHandle, Ref<Asset>>& GetRegisteredAssets() override;

	private:
		std::unordered_map<AssetHandle, Ref<Asset>> m_MemoryAssets;
		std::unordered_map<AssetHandle, Ref<Asset>> m_RegisteredAssets;	

		AssetRegistry m_AssetRegistry;


	};
}