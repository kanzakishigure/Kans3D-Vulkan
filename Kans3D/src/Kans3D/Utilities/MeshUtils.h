#pragma once
#include "Kans3D/Renderer/Resource/Mesh.h"
namespace Kans::Utils
{
	enum class OffsetType
	{
		Default = 0,
		NormalDirection,
		FiexdOffset
	};
	class MeshUtils
	{
	public:
		static void SmoothNormal(std::vector<Vertex>& verteices, std::vector<glm::vec3>& smoothedNormal);
		static void FastSmoothNormal(std::vector<Vertex>& verteices);
		static void SetMeshOffset(std::vector<Vertex>& verteices, const std::vector<SubMesh>& submushes, float offset);
		static void BackMeshNormal(std::vector<Vertex>& verteices, const std::vector<glm::vec3>& backedNormal);
	};

	

}