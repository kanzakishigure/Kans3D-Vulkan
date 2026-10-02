#pragma once
#include "AssetImporterBase.h"
#include "Kans3D/Asset/AssetMetadata.h"
#include <filesystem>
namespace Kans
{
	class EnvironmentImporter :public AssetImporterBase
	{
	public:
		void Import(const Ref<AssetMetadata>& metadata, Ref<Asset>& asset) const override;
		bool TryLoadData(const Ref<AssetMetadata>& metadata, Ref<Asset>& asset) const override;
		
	private:
  void PrepareEnvironment();
	private:
		const std::filesystem::path m_Path;
	};
}