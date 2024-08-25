#pragma once
#include "Kans3D/Asset/AssetMetaData.h"
namespace Kans
{
	class AssetImporterbase
	{
	public:
		virtual void Importe(const Ref<AssetMetaData>& metadata, const Ref<Asset>& asset) const = 0;
		virtual bool TryLoadData(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const = 0;
	};
}