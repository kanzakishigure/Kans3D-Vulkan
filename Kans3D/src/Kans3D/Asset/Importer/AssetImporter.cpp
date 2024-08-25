#include "kspch.h"
#include "AssetImporter.h"


#include "TextureImporter.h"
#include "StaticMeshImporter.h"

namespace Kans
{
	std::unordered_map<AssetType, Scope<AssetImporterbase>> AssetImporter::s_Importers;
	void AssetImporter::Init()
	{
		s_Importers.clear();
		s_Importers[AssetType::Texture] = CreateScope<TextureImporter>();
		s_Importers[AssetType::StaticMesh] = CreateScope<StaticMeshImporter>();
	}

	void AssetImporter::Import(const Ref<AssetMetaData>& metadata, const Ref<Asset>& asset)
	{
		if (s_Importers.find(metadata->Type) == s_Importers.end())
		{
			CORE_ERROR("Import Asset [{}] fail, don't have the Importer", metadata->FilePath);
		}
		s_Importers.at(metadata->Type)->Importe(metadata, asset);
		return;
	}
}



