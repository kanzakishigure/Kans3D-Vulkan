#include "kspch.h"

#include "Mesh.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Utilities/MeshUtils.h"

namespace Kans
{

	MeshSource::MeshSource(const std::vector<Vertex>& verteices, const std::vector<Index>& indices)
		:m_Verteices(verteices), m_Indices(indices)
	{
		m_MeshShader = Renderer::GetShaderLibrary()->Get("PBRStaticMesh");
	
		Ref<Material> mtl = Material::Create(m_MeshShader, "Default Material");
		
		
		
		
		std::vector<Ref<Material>> materials;
		materials.push_back(mtl);
		m_MaterialTable = CreateRef<MaterialTable>(m_Materials.size());
		for (size_t i = 0; i < materials.size(); i++)
		{
			auto materialAsset = CreateRef<MaterialAsset>(materials[i]);
			materialAsset->SetDiffuseMap(Renderer::GetWhiteTexture());
			materialAsset->SetSpecularMap(Renderer::GetWhiteTexture());
			materialAsset->SetNormalMap(Renderer::GetWhiteTexture());

			materialAsset->SetAlbedoMap(Renderer::GetWhiteTexture());
			materialAsset->SetMetalMap(Renderer::GetBlackTexture());
			materialAsset->SetRoughMap(Renderer::GetWhiteTexture());
			materialAsset->SetAOMap(Renderer::GetWhiteTexture());
			m_MaterialTable->SetMaterial(i, materialAsset);
		}


		SubMesh submesh;
		submesh.BaseVertex = 0;
		submesh.BaseIndex = 0;
		submesh.IndexCount = (uint32_t)indices.size();
		submesh.Transform = glm::mat4(1.0f);
		m_SubMeshes.reserve(1);
		m_SubMeshes.push_back(submesh);


		Ref<VertexBuffer> vertexb = VertexBuffer::Create((float*)m_Verteices.data(), (uint32_t)(m_Verteices.size() * sizeof(Vertex)));
		Ref<IndexBuffer>indexb = IndexBuffer::Create((uint32_t*)m_Indices.data(), indices.size() * 3u);


		BufferLayout layout = {
			{ShaderDataType::Float3,"a_Position"},
			{ShaderDataType::Float3,"a_Normal"},
			{ShaderDataType::Float2,"a_TextureCroods"},
			{ShaderDataType::Float4,"a_BaseColor"},
			{ShaderDataType::Float4,"a_Tangent"}
		};


		vertexb->SetBufferLayout(layout);

		m_VertexArray.push_back(VertexArray::Create());
		m_VertexArray[0]->AddVertexBuffer(vertexb);
		m_VertexArray[0]->SetIndexBuffer(indexb);

		

	}

	MeshSource::~MeshSource()
	{
		// PendingGpuTexture 的析构函数会自动释放 stbi 数据
		m_PendingTextures.clear();
	}

	// ============================================================
	// FinalizeGpuResources — 主线程调用，创建所有延迟的 GPU 资源
	// 必须在 OpenGL context 绑定的线程上调用（即主线程）
	// ============================================================
	void MeshSource::FinalizeGpuResources()
	{
		PROFILE_FUCTION();

		if (m_GpuReady)
			return;

		CORE_INFO_TAG("Mesh", "FinalizeGpuResources — creating GPU buffers for {} submeshes, {} pending textures",
		              m_SubMeshes.size(), m_PendingTextures.size());

		// ═══ 步骤 1: 创建 VA/VB/IB ═══
		{
			const double offset = 1.0 / 8000.0;
			std::vector<glm::vec3> smoothNormals;
			Utils::MeshUtils::SmoothNormalHash(m_Verteices, smoothNormals);
			Utils::MeshUtils::SetMeshOffset(m_Verteices, m_SubMeshes, static_cast<float>(offset));
			Utils::MeshUtils::BackMeshNormal(m_Verteices, smoothNormals);

			BufferLayout layout = {
				{ShaderDataType::Float3, "a_Position"},
				{ShaderDataType::Float3, "a_Normal"},
				{ShaderDataType::Float2, "a_TextureCroods"},
				{ShaderDataType::Float4, "a_BaseColor"},
				{ShaderDataType::Float4, "a_Tangent"},
			};

			for (auto& submesh : m_SubMeshes)
			{
				auto VA = VertexArray::Create();
				VA->Bind();

				auto VB = VertexBuffer::Create(
					reinterpret_cast<float*>(&m_Verteices[submesh.BaseVertex]),
					submesh.VertexCount * sizeof(Vertex));
				VB->Bind();
				VB->SetBufferLayout(layout);
				VA->AddVertexBuffer(VB);

				auto IB = IndexBuffer::Create(
					reinterpret_cast<uint32_t*>(&m_Indices[submesh.BaseIndex]),
					submesh.IndexCount * 3);
				IB->Bind();
				VA->SetIndexBuffer(IB);

				m_VertexArray.push_back(VA);
			}
			CORE_INFO_TAG("Mesh", "  VA/VB/IB — {} vertex arrays created", m_VertexArray.size());
		}

		// ═══ 步骤 2: 创建纹理并注入 MaterialAsset ═══
		{
			TextureSpecification spec;

			for (auto& pendingTex : m_PendingTextures)
			{
				spec.Width = pendingTex.Width;
				spec.Height = pendingTex.Height;
				spec.Format = pendingTex.Format;

				Ref<Texture2D> tex = Texture2D::Create(spec, pendingTex.PixelData);

				// 注入到对应 MaterialAsset
				if (m_MaterialTable && m_MaterialTable->HasMaterial(pendingTex.MaterialIndex))
				{
					const auto& materialAsset = m_MaterialTable->GetMaterialAsset(pendingTex.MaterialIndex);
					if (materialAsset)
					{
						materialAsset->GetMaterial()->SetTexture(
							pendingTex.TextureUniformName, tex);
					}
				}

				// pendingTex 析构时自动 free stbi 数据
			}

			CORE_INFO_TAG("Mesh", "  Textures — {} textures created and assigned", m_PendingTextures.size());
			m_PendingTextures.clear();
		}

		m_GpuReady = true;
		CORE_INFO_TAG("Mesh", "FinalizeGpuResources — complete");
	}

	StaticMesh::StaticMesh(Ref<MeshSource> source)
		:m_MeshSource(source)
	{
		//submesh
		SetSubMesh({});
		//material
		m_MaterialTable = source->GetMaterialTable();
	}
	
	StaticMesh::~StaticMesh()
	{

	}

	void StaticMesh::SetMaterial(Ref<Material> material,uint32_t submeshIndex)
	{
		m_MaterialTable->SetMaterial(submeshIndex, CreateRef<MaterialAsset>(material));
	}

	void StaticMesh::SetSubMesh(const std::vector<uint32_t>& submesh)
	{
		if (!submesh.empty())
		{
			
			m_SubMesh = submesh;
		}
		else
		{
			const std::vector<SubMesh>& SourceSubmesh = m_MeshSource->GetSubMesh();
			m_SubMesh.clear();
			m_SubMesh.resize(SourceSubmesh.size());
			for (size_t i = 0; i < SourceSubmesh.size(); i++)
			{
				m_SubMesh[i] = (uint32_t)i;
			}
		}
	}

}