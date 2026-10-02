#pragma once
#include "AssetMetadata.h"
#include "Kans3D/Asset/Asset.h"
#include <filesystem>
#include <unordered_map>
namespace Kans
{

	class AssetRegistry
	{
	public:
		AssetRegistry();
		~AssetRegistry() = default;

		void LoadAllAssetMetadata();
		const Ref<AssetMetadata> GetAssetMetadata(AssetID handle) const;

	private:
		std::unordered_map<AssetID, Ref<AssetMetadata>> m_AssetMetadatas;

	};

}