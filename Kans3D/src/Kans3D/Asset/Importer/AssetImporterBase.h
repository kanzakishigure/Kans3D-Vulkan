#pragma once
#include "Kans3D/Asset/AssetMetadata.h"
namespace Kans
{
	class AssetImporterBase
	{
	public:
		virtual void Import(const Ref<AssetMetadata>& metadata,  Ref<Asset>& asset) const = 0;
		virtual bool TryLoadData(const Ref<AssetMetadata>& metadata, Ref<Asset>& asset) const = 0;
	};
}