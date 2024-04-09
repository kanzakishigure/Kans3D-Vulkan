#include "kspch.h"
#include "MeshFactory.h"
#include "Kans3D/Core/Math/Math.h"
namespace Kans
{
	Ref<StaticMesh> MeshFactory::Cube = nullptr;
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

	Kans::Ref<Kans::StaticMesh> MeshFactory::GetCube()
	{
		if (MeshFactory::Cube == nullptr)
		{
			MeshFactory::Cube = CreatCube(glm::vec3(2.0f));
			return MeshFactory::Cube;
		}
		return MeshFactory::Cube;
	}

	Kans::Ref<Kans::StaticMesh> MeshFactory::CreatCloudSpot(const float count, const glm::vec3 size)
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

	Ref<VertexArray> MeshFactory::CreatQuad(const glm::vec2 size)
	{

		struct QuadVertex
		{
			glm::vec3 Position;
			glm::vec2 TextureCroods;
		};





		
		std::vector<QuadVertex> vertices;

		vertices.resize(4);
		vertices[0].Position = { -size.x / 2.0f  , 0.0f, -size.y / 2.0f  };
		vertices[1].Position = {  size.x / 2.0f  , 0.0f, -size.y / 2.0f  };
		vertices[2].Position = {  size.x / 2.0f  , 0.0f,  size.y / 2.0f, };
		vertices[3].Position = { -size.x / 2.0f  , 0.0f,  size.y / 2.0f, };
	
		vertices[0].TextureCroods = { 0.0f,  0.0f };
		vertices[1].TextureCroods = { 1.0f,  0.0f };
		vertices[2].TextureCroods = { 1.0f,  1.0f };
		vertices[3].TextureCroods = { 0.0f,  1.0f };



		std::vector<Index> indices;
		indices.resize(2);
		indices[0] = { 0, 1, 2 };
		indices[1] = { 2, 3, 0 };
		

		BufferLayout layout = {
			{ShaderDataType::Float3,"a_Position"},
			{ShaderDataType::Float2,"a_TextureCroods"},

		};

		Ref<VertexBuffer> vertexb = VertexBuffer::Create((float*)vertices.data(), (uint32_t)(vertices.size() * sizeof(Vertex)));
		Ref<IndexBuffer>indexb = IndexBuffer::Create((uint32_t*)indices.data(), indices.size() * 3u);
		if (indexb.get() == nullptr || vertexb.get() == nullptr)
		{
			CORE_ERROR_TAG("MeshFactory", "CreatQuad fail");
			return nullptr;
		}
		vertexb->SetBufferLayout(layout);
		Ref<VertexArray> va = VertexArray::Create();
		va->AddVertexBuffer(vertexb);
		va->SetIndexBuffer(indexb);
		va->Unbind();
		return va;
	}

}