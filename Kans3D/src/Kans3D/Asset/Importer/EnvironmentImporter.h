#pragma once
#include "AssetImporterbase.h"
#include "Kans3D/Asset/AssetMetaData.h"
#include <filesystem>
namespace Kans
{
	class EnvironmentImporter :public AssetImporterbase
	{
	public:
		void Importe(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const override;
		bool TryLoadData(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const override;
		
	private:
  void PrepareEnvironment();
	private:
		const std::filesystem::path m_Path;
	};
}