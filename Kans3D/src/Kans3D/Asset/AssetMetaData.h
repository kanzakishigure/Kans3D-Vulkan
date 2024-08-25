#pragma once
#include "Kans3D/Asset/Asset.h"
#include <filesystem>
namespace Kans
{
	class AssetMetaData
	{
	public:
		AssetMetaData() {}
		~AssetMetaData() = default;
		bool IsValid() { return Handle != AssetHandle::None; }
	public:

		std::filesystem::path FilePath;
		AssetType Type = AssetType::None;
		AssetHandle Handle = AssetHandle::None;
		bool IsDataLoad = false;
				
	};

	

}