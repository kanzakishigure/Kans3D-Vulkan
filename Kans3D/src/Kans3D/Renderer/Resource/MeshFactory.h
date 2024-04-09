#pragma once
#include "Mesh.h"
namespace Kans {
	class MeshFactory
	{
	
	public:
		static  Ref<StaticMesh> CreatCube(const glm::vec3& size);
		static  Ref<StaticMesh> GetCube();
		static  Ref<StaticMesh> CreatCloudSpot(const float size,const glm::vec3 value);

		static  Ref<VertexArray> CreatQuad(const glm::vec2 size);

	private:
		static  Ref<StaticMesh> Cube;
	};
}