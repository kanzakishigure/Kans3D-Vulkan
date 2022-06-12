#include "hzpch.h"
#include "Kans3D/Utilities/FileSystem/FileSystem.h"
namespace Kans{


	bool KansFileSystem::Exists(const std::string& filepath)
	{
		return std::filesystem::exists(filepath);
	}

}