#pragma once
#include "Kans3D/Asset/AssetMetaData.h"
#include "AssetImporterbase.h"
namespace Kans
{
	class AssetImporter
	{
	public:
		static void Init();
		static void Import(const Ref<AssetMetaData>& metadata, const Ref<Asset>& asset);

	private:
		static std::unordered_map<AssetType, Scope<AssetImporterbase>> s_Importers;

	};
}