#pragma once
#include "AssetImporterbase.h"
#include <filesystem>
namespace Kans
{
	class TextureImporter :public AssetImporterbase
	{
	public:
		void Importe(const Ref<AssetMetaData>& metadata, const Ref<Asset>& asset) const override;
		bool TryLoadData(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const override;

	private:
		const std::filesystem::path m_Path;
	};
}