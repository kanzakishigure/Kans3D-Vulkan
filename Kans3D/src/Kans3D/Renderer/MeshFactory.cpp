#include "kspch.h"
#include "MeshFactory.h"
#include "Kans3D/Core/Math/Math.h"
namespace Kans
{
	Ref<StaticMesh> MeshFactory::CreatCube(const glm::vec3& size)
	{
		std::vector<Vertex> vertices;

		vertices.resize(8);
		vertices[0].Position = { -size.x / 2.0f, -size.y / 2.0f,  size.z / 2.0f };
		vertices[1].Position = { size.x / 2.0f, -size.y / 2.0f,  size.z / 2.0f };
		vertices[2].Position = { size.x / 2.0f,  size.y / 2.0f,  size.z / 2.0f };
		vertices[3].Position = { -size.x / 2.0f,  size.y / 2.0f,  size.z / 2.0f };
		vertices[4].Position = { -size.x / 2.0f, -size.y / 2.0f, -size.z / 2.0f };
		vertices[5].Position = { size.x / 2.0f, -size.y / 2.0f, -size.z / 2.0f };
		vertices[6].Position = { size.x / 2.0f,  size.y / 2.0f, -size.z / 2.0f };
		vertices[7].Position = { -size.x / 2.0f,  size.y / 2.0f, -size.z / 2.0f };

		vertices[0].Normal = { -1.0f, -1.0f,  1.0f };
		vertices[1].Normal = { 1.0f, -1.0f,  1.0f };
		vertices[2].Normal = { 1.0f,  1.0f,  1.0f };
		vertices[3].Normal = { -1.0f,  1.0f,  1.0f };
		vertices[4].Normal = { -1.0f, -1.0f, -1.0f };
		vertices[5].Normal = { 1.0f, -1.0f, -1.0f };
		vertices[6].Normal = { 1.0f,  1.0f, -1.0f };
		vertices[7].Normal = { -1.0f,  1.0f, -1.0f };

		std::vector<Index> indices;
		indices.resize(12);
		indices[0] = { 0, 1, 2 };
		indices[1] = { 2, 3, 0 };
		indices[2] = { 1, 5, 6 };
		indices[3] = { 6, 2, 1 };
		indices[4] = { 7, 6, 5 };
		indices[5] = { 5, 4, 7 };
		indices[6] = { 4, 0, 3 };
		indices[7] = { 3, 7, 4 };
		indices[8] = { 4, 5, 1 };
		indices[9] = { 1, 0, 4 };
		indices[10] = { 3, 2, 6 };
		indices[11] = { 6, 7, 3 };
		
		

		auto meshsource = CreateRef<MeshSource>(vertices, indices);
		auto mesh = CreateRef<StaticMesh>(meshsource);
		return mesh;
	}

	Kans::Ref<Kans::StaticMesh> MeshFactory::CreatCloudSpot(const float count,const glm::vec3 size )
	{
		std::vector<Vertex> vertices;
		vertices.resize(count);
		for (size_t i = 0; i < count; i++)
		{
			vertices[i].Position = { -size.x+Random::Float()* size.x*2,-size.y + Random::Float() * size.y*2,-size.z + Random::Float() * size.z*2 };
		}
		std::vector<Index> indices;
		indices.resize(count);
		auto meshsource = CreateRef<MeshSource>(vertices, indices);
		auto mesh		= CreateRef<StaticMesh>(meshsource);
		return mesh;
	}

}