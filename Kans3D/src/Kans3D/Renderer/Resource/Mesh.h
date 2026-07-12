#pragma once
#include "glm/glm.hpp"
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"
#include "Kans3D/Renderer/RHI/Buffer.h"
#include "Kans3D/Renderer/RHI/RHITypeProxy.h"
#include "Kans3D/Core/Math/BindingBox.h"
#include "Kans3D/Renderer/Resource/Material.h"
#include "Kans3D/Renderer/Resource/MaterialAsset.h"

#include "Kans3D/Asset/Asset.h"

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags


namespace Kans
{
	// ============================================================
	// PendingGpuTexture — 延迟 GPU 纹理创建数据
	// 工作线程加载图片到内存，主线程据此创建 OpenGL 纹理
	// ============================================================
	struct PendingGpuTexture
	{
		Buffer PixelData;              // stbi_load 返回的原始像素数据
		uint32_t Width = 0;
		uint32_t Height = 0;
		RHIFormat Format = RHI_FORMAT_MAX_ENUM;
		uint32_t MaterialIndex = 0;    // 对应 MaterialTable 中的 slot
		std::string TextureUniformName; // 材质 uniform 名称

		~PendingGpuTexture()
		{
			if (PixelData.Data)
			{
				free(PixelData.Data);
				PixelData.Data = nullptr;
			}
		}

		// 禁止拷贝（有 owning raw pointer）
		PendingGpuTexture() = default;
		PendingGpuTexture(PendingGpuTexture&& other) noexcept
			: PixelData(other.PixelData), Width(other.Width), Height(other.Height),
			  Format(other.Format), MaterialIndex(other.MaterialIndex),
			  TextureUniformName(std::move(other.TextureUniformName))
		{
			other.PixelData.Data = nullptr;
			other.PixelData.Size = 0;
		}
		PendingGpuTexture& operator=(PendingGpuTexture&& other) noexcept
		{
			if (this != &other)
			{
				if (PixelData.Data) free(PixelData.Data);
				PixelData = other.PixelData;
				Width = other.Width;
				Height = other.Height;
				Format = other.Format;
				MaterialIndex = other.MaterialIndex;
				TextureUniformName = std::move(other.TextureUniformName);
				other.PixelData.Data = nullptr;
				other.PixelData.Size = 0;
			}
			return *this;
		}
		PendingGpuTexture(const PendingGpuTexture&) = delete;
		PendingGpuTexture& operator=(const PendingGpuTexture&) = delete;
	};

	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 Texturecroods;
		glm::vec4 BaseColor;
		glm::vec4 Tangent;


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

		
		const Ref<MaterialTable>& GetMaterialTable() { return m_MaterialTable; }
		
		const Ref<Shader>& GetMeshShader() { return m_MeshShader; }

		const std::vector<Ref<VertexArray>>& GetVertexArray() const { return m_VertexArray; }
		
		const std::string& GetLoadPath() const { return m_LoadPath; }

		// ---- 延迟 GPU 资源创建（方案一：CPU/GPU 分离）----
		bool IsGpuReady() const { return m_GpuReady; }

		// 从工作线程调用：存储待创建的资源数据（无 GL 调用）
		void AddPendingTexture(PendingGpuTexture&& tex)
		{
			m_PendingTextures.push_back(std::move(tex));
		}

		// 从主线程调用：创建所有延迟的 GPU 资源（纹理 + VA/VB/IB）
		void FinalizeGpuResources();

	private:
		//temp function to renderer mesh
		void GenVertexArry();
		

	private:
		Scope<Assimp::Importer> m_Importer;

		std::vector <Vertex> m_Verteices;
		std::vector <Index> m_Indices;
		std::vector<SubMesh> m_SubMeshes;

		std::vector<Ref<VertexArray>> m_VertexArray;
		std::vector<Ref<MaterialAsset>> m_Materials;
		Ref<MaterialTable> m_MaterialTable;
		Ref<Shader> m_MeshShader;

		const aiScene* m_Scene;
		glm::mat4 m_Transform;

		std::string m_LoadPath;
		//?????????????????????????
		BindBox m_BindingBox;

		// 延迟 GPU 资源
		bool m_GpuReady = false;
		std::vector<PendingGpuTexture> m_PendingTextures;

		friend class Entity;
		friend class AssimpMeshImporter;
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

		void StaticMesh::SetMaterial(Ref<Material> material, uint32_t submeshIndex);
		void SetMeshAsset(Ref<MeshSource> meshAsset) { m_MeshSource = meshAsset; }

		Ref<MaterialTable> GetMaterialTable() const { return m_MaterialTable; }

		void SetSubMesh(const std::vector<uint32_t>& submesh);
		const std::vector<uint32_t>& GetSubMesh() { return m_SubMesh; }
	private:
		std::vector<uint32_t> m_SubMesh;
		Ref<MaterialTable> m_MaterialTable;
		Ref<MeshSource> m_MeshSource;

	};

	class DynamicMesh :public Asset
	{
	public:
		DynamicMesh(Ref<MeshSource> source);
		~DynamicMesh();
		Ref<MeshSource> GetMeshSource() { return m_MeshSource; }
		Ref<MeshSource> GetMeshSource() const { return m_MeshSource; }

		void DynamicMesh::SetMaterial(Ref<Material> material, uint32_t submeshIndex);
		void SetMeshAsset(Ref<MeshSource> meshAsset) { m_MeshSource = meshAsset; }

		Ref<MaterialTable> GetMaterialTable() const { return m_MaterialTable; }

		void SetSubMesh(const std::vector<uint32_t>& submesh);
		const std::vector<uint32_t>& GetSubMesh() { return m_SubMesh; }
	private:
		std::vector<uint32_t> m_SubMesh;
		Ref<MaterialTable> m_MaterialTable;
		Ref<MeshSource> m_MeshSource;

	};



}