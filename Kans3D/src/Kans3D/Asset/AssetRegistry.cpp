#include "kspch.h"
#include "AssetRegistry.h"

namespace Kans
{

	AssetRegistry::AssetRegistry()
	{

	}

	void AssetRegistry::LoadAllAssetMetaData()
	{

	}

	const  Ref<AssetMetaData>& AssetRegistry::GetAssetMetaData(AssetHandle handle) const
	{
		if (m_AssetMetaDatas.find(handle) != m_AssetMetaDatas.end())
		{
			return m_AssetMetaDatas.at(handle);
		}
		return nullptr;
	}

}