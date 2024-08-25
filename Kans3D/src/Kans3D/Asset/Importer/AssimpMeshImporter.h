#pragma once

#include "Kans3D/Renderer/Resource/Mesh.h"
#include <filesystem>
namespace Kans
{
	class AssimpMeshImporter
	{
	public:
		AssimpMeshImporter(const std::filesystem::path& path);

		Ref<MeshSource> ImportToMeshSource();


		

	private:
		const std::filesystem::path m_Path;
	};
}