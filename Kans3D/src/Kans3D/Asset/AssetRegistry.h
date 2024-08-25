#pragma once
#include "AssetMetaData.h"
namespace Kans
{

	class AssetRegistry
	{
	public:
		AssetRegistry();
		~AssetRegistry() = default;

		void LoadAllAssetMetaData();
		const Ref<AssetMetaData>& GetAssetMetaData(AssetHandle handle) const;

	private:
		std::map<UUID, Ref<AssetMetaData>> m_AssetMetaDatas;

	};

}