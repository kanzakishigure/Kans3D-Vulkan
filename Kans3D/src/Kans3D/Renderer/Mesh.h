#pragma once
#include "glm/glm.hpp"
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"
#include "Kans3D/Renderer/RHI/Buffer.h"
#include "Kans3D/Core/Math/BindingBox.h"
#include "Kans3D/Renderer/Material.h"
#include "Kans3D/Renderer/MaterialAsset.h"

#include "Kans3D/Asset/Asset.h"

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags


namespace Kans
{
	//c++有个优秀的性质，struct在内存中是连续存储的
	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 Texturecroods;
		glm::vec4 BaseColor;
		glm::vec3 Tangent;
		glm::vec3 Bitangent;

	};
	struct Index
	{
		uint32_t V0, V1, V2;
	};
	struct Trangle
	{
		uint32_t i0, i1, i2;
	};
	class SubMesh
	{
	public:
		//we storage the SubMeshed with BaseOffset and count
		uint32_t BaseVertex			= 0;
		uint32_t BaseIndex			= 0;
		uint32_t VertexCount		= 0;
		uint32_t IndexCount			= 0;

		uint32_t MaterialIndex		= 0;

		glm::mat4 Transform{ 1.0f }; // World transform
		glm::mat4 LocalTransform{ 1.0f };
		BindBox BoundingBox;
	};

	class MeshSource : public Asset
	{
	public:
		MeshSource(const std::string& path);
		MeshSource() = default;

		MeshSource(const std::vector<Vertex>& verteices, const std::vector<Index>& indices);
		~MeshSource();
		void ProcessNode(const aiNode* node, const aiScene* scene);
		SubMesh ProcessMesh(const aiMesh* mesh, const aiScene* scene,float meshoffset);

		const std::vector<SubMesh>& GetSubMesh() { return m_SubMeshes; }

		void SetMaterial(const std::vector<Ref<Material>>& material) { m_Material = material; }
		const std::vector<Ref<Material>>& GetMaterial() { return m_Material; }
		
		const Ref<Shader>& GetMeshShader() { return m_MeshShader; }

		const std::vector<Ref<VertexArray>>& GetVertexArray() const { return m_VertexArray; }
		
		const std::string& GetLoadPath() const { return m_LoadPath; }
	private:
		//temp function to renderer mesh
		void GenVertexArry();
		

	private:
		Scope<Assimp::Importer> m_Importer;

		std::vector <Vertex> m_Verteices;
		std::vector <Index> m_Indices;
		std::vector<SubMesh> m_SubMeshes;

		std::vector<Ref<VertexArray>> m_VertexArray;
		std::vector<Ref<Material>> m_Material;
		Ref<Shader> m_MeshShader;

		const aiScene* m_Scene;
		glm::mat4 m_Transform;

		std::string m_LoadPath;
		//用于物理碰撞计算，光线追踪加速
		BindBox m_BindingBox;

		friend class Entity;
	};

	class Mesh :public Asset
	{
	public:
		Mesh(const MeshSource& source);
		~Mesh();
	private:
		std::vector<uint32_t> m_SubMesh;
		Ref<MaterialTable> m_MarterialTable;
		Ref<MeshSource> m_MeshSourcel;
	};
	class StaticMesh :public Asset
	{
	public:
		StaticMesh(Ref<MeshSource> source);
		~StaticMesh();
		Ref<MeshSource> GetMeshSource() { return m_MeshSource; }
		Ref<MeshSource> GetMeshSource() const { return m_MeshSource; }
		void SetMeshAsset(Ref<MeshSource> meshAsset) { m_MeshSource = meshAsset; }

		Ref<MaterialTable> GetMaterials() const { return m_MarterialTable; }

		void SetSubMesh(const std::vector<uint32_t>& submesh);
		const std::vector<uint32_t>& GetSubMesh() { return m_SubMesh; }
	private:
		std::vector<uint32_t> m_SubMesh;
		Ref<MaterialTable> m_MarterialTable;
		Ref<MeshSource> m_MeshSource;

	};



}