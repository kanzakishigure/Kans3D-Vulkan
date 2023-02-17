#pragma once
#include "Kans3D/Asset/Asset.h"
#include <filesystem>
namespace Kans
{
	struct AssetMetaData
	{
		AssetHandle Handle = 0;
		
		std::filesystem::path FilePath;
		bool IsDataLoad = false;
		
		bool IsValid() { return Handle != 0; }
	};
}