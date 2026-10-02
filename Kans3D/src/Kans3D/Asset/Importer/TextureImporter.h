#pragma once
#include "LegacyAssetImporterBase.h"
#include <filesystem>
namespace Kans
{
	class TextureImporter :public LegacyAssetImporterBase
	{
	public:
		void Import(const Ref<AssetMetadata>& metadata, Ref<Asset>& asset) const override;
		bool TryLoadData(const Ref<AssetMetadata>& metadata, Ref<Asset>& asset) const override;

	private:
		const std::filesystem::path m_Path;
	};
}