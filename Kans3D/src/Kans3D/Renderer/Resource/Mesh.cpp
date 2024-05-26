#include "kspch.h"

#include "Mesh.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Utilities/MeshUtils.h"
#include "Kans3D/FileSystem/FileSystem.h"
const uint32_t importFlag = 
aiProcess_CalcTangentSpace  //�������߿ռ�
|aiProcess_Triangulate //��֤һ��ÿ��ͼԪ�Ļ�����λ�������Σ�����ֶ������
|aiProcess_JoinIdenticalVertices 
|aiProcess_SortByPType
|aiProcess_GenSmoothNormals;//����ƽ�����ߣ���֤ʵ�����Ч����ʱ�򲻻����Ӳ�߶��ѵĽ��


namespace Kans
{

	
	
	MeshSource::MeshSource(const std::string& path)
		:m_LoadPath(path)
	{
		size_t index =  m_LoadPath.find_last_of("/");
		m_LoadPath =  m_LoadPath.substr(0, index);

		Scope<Assimp::Importer>m_Importer = std::make_unique<Assimp::Importer>();
		m_Scene =  m_Importer->ReadFile(path,importFlag);
		CORE_INFO("load mesh path {0}", path.c_str());
		if (!m_Scene || m_Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !m_Scene->mRootNode)
		{
			CORE_ERROR(m_Importer->GetErrorString());
			CORE_ASSERT(m_Scene, "assimp load file fail ");
		}


		m_MeshShader = Renderer::GetShaderLibrary()->Get("StaticMeshShader");
		
	
		m_SubMeshes.reserve(m_Scene->mNumMeshes);
		ProcessNode(m_Scene->mRootNode, m_Scene);
		m_MaterialTable = CreateRef<MaterialTable>(m_Materials.size());

		for (size_t i = 0; i < m_Materials.size(); i++)
		{
			m_MaterialTable->SetMaterial(i, m_Materials[i]);
		}
		m_Materials.clear();

		GenVertexArry();


	}

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
			auto& materialAsset = CreateRef<MaterialAsset>(materials[i]);
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
		submesh.Transform = m_Transform;
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
		
	}



	void MeshSource::ProcessNode(const aiNode* node, const aiScene* scene)
	{
		//HZ_INFO("process level: {0}", level);
		//HZ_INFO("node-meshNum:  {0}", node->mNumMeshes);
		//processCurrentNode
		for (size_t i = 0; i < node->mNumMeshes; i++)
		{

			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			m_SubMeshes.push_back(ProcessMesh(mesh, scene, 0));

		}

		//ProsessChildNode
		for (size_t i = 0; i < node->mNumChildren; i++)
		{
			ProcessNode(node->mChildren[i], m_Scene);

		}
		
	}

	SubMesh MeshSource::ProcessMesh(const aiMesh* mesh, const aiScene* scene, float meshoffset)
	{
		SubMesh submesh;
		submesh.BaseVertex = (uint32_t)m_Verteices.size();
		submesh.BaseIndex = (uint32_t)m_Indices.size();
		submesh.VertexCount = 0;
		submesh.IndexCount = 0;
		//Vertex
		for (size_t i = 0; i < mesh->mNumVertices; i++)
		{
			Vertex v;

			
			v.Normal =	  { mesh->mNormals[i].x,
						    mesh->mNormals[i].y,
						    mesh->mNormals[i].z		};

			v.Position =  { mesh->mVertices[i].x ,
						    mesh->mVertices[i].y ,
						    mesh->mVertices[i].z	};
			
			glm::vec3 tangent =   { mesh->mTangents[i].x,
									mesh->mTangents[i].y,
									mesh->mTangents[i].z, };
			
			glm::vec3 bitangent = { mesh->mBitangents[i].x,
									mesh->mBitangents[i].y,
									mesh->mBitangents[i].z	};
			
			//we just stored the tangent 
			float w = glm::dot(glm::cross(v.Normal, tangent), bitangent) > 0 ? 1.0f:-1.0f;
			v.Tangent = { mesh->mTangents[i].x,
						  mesh->mTangents[i].y,
						  mesh->mTangents[i].z, 
						  w						};
			
			
			
			if (mesh->mTextureCoords[0]) // �����Ƿ����������ꣿ
			{
				v.Texturecroods = { mesh->mTextureCoords[0][i].x,
									mesh->mTextureCoords[0][i].y,};
			}
			else
			{
				v.Texturecroods = glm::vec2(0.0f);
			}
			if (mesh->HasVertexColors(i))
			{
				v.BaseColor = {	mesh->mColors[i]->r,
								mesh->mColors[i]->g,
								mesh->mColors[i]->b,
								mesh->mColors[i]->a };
				CORE_TRACE("have basecolor[{},{},{},{}]", mesh->mColors[i]->r, mesh->mColors[i]->g, mesh->mColors[i]->b,mesh->mColors[i]->a);
			}
			else
			{
				v.BaseColor = glm::vec4(0.0f);
				
			}

			m_Verteices.push_back(v);
			submesh.VertexCount++;
		}
		//Index
		for (size_t i = 0; i < mesh->mNumFaces; i++)
		{
			aiFace face = mesh->mFaces[i];
			for (size_t j = 0; j < face.mNumIndices; j+=3)
			{
				m_Indices.push_back({ face.mIndices[j],face.mIndices[j+1],face.mIndices[j+2]});
				submesh.IndexCount++;
			}
		}
		//Material
		{
			PROFILE_SCOPE("Import mesh Material ");
			if (scene->HasMaterials())
			{
				aiMaterial* aimaterial = scene->mMaterials[mesh->mMaterialIndex];
				std::string mtlName = aimaterial->GetName().C_Str();
				Ref<Material> mtl = Material::Create(m_MeshShader, mtlName);
				Ref<MaterialAsset> mtlAsset = CreateRef<MaterialAsset>(mtl);

				//General Attributes
				{
					TextureSpecification spec;
					//Normal
					{
						int materialcount = 0;
						materialcount = aimaterial->GetTextureCount(aiTextureType_NORMALS);
						if (materialcount)
						{

							if (materialcount)
							{
								aiString aistr;
								aimaterial->GetTexture(aiTextureType_NORMALS, 0, &aistr);
								CORE_INFO("{0} Normal texture: {1} ", mtlName.c_str(), aistr.C_Str());
								std::string texturepath = m_LoadPath + "/" + aistr.C_Str();
								Ref<Texture2D> texture = Texture2D::Create(spec, texturepath);
								mtlAsset->SetNormalMap(texture);
								
							}
						}
						else
						{
							CORE_WARN("{0} don't have Normal texture", mtlName.c_str());
						}
					}
				}
				//BlingPhong material
				
				{
					TextureSpecification spec;
					//DIFFUSE;
					{
						int materialcount = 0;
						materialcount = aimaterial->GetTextureCount(aiTextureType_DIFFUSE);
						if (materialcount)
						{
							aiString aistr = {};
							aimaterial->GetTexture(aiTextureType_DIFFUSE, 0, &aistr);
							CORE_INFO("{0} DIFFUSE texture: {1} ", mtlName.c_str(), aistr.C_Str());
							std::string texturepath = m_LoadPath + "/" + aistr.C_Str();
							Ref<Texture2D> texture = Texture2D::Create(spec,texturepath);
							mtlAsset->SetDiffuseMap(texture);
							
							{
								//LightMap
								std::string Lightmappath = texturepath;
								size_t index = Lightmappath.find_last_of(".");
								if(index!= std::string::npos)
								Lightmappath.insert(index, "_Light");
								if (KansFileSystem::Exists(Lightmappath))
								{
									Ref<Texture2D> lighttexture = Texture2D::Create(spec,Lightmappath);
									mtl->SetTexture(MaterialAsset::GetToneLightMapLocation(), lighttexture);
									CORE_INFO("{0} Light texture: {1} ", mtlName.c_str(), Lightmappath.c_str());
								}
								else
								{
									CORE_WARN("{0} don't have Light texture", mtlName.c_str());
									mtl->SetTexture(MaterialAsset::GetToneLightMapLocation(), Renderer::GetWhiteTexture());
								}
							}
							{
								//RampMap
								std::string Rampmappath = texturepath;
								size_t index = Rampmappath.find_last_of(".");
								if (index != std::string::npos)
								Rampmappath.insert(index, "_Ramp");

								if (KansFileSystem::Exists(Rampmappath))
								{
									Ref<Texture2D> ramptexture = Texture2D::Create(spec,Rampmappath);
									mtl->SetTexture(MaterialAsset::GetToneRampMapLocation(), ramptexture);
									CORE_INFO("{0} Ramp texture: {1} ", mtlName.c_str(), Rampmappath.c_str());
								}
								else
								{
									CORE_WARN("{0} don't have Ramp texture", mtlName.c_str());
									mtl->SetTexture(MaterialAsset::GetToneRampMapLocation(), Renderer::GetWhiteTexture());
								}
							}
						}
						else
						{
							CORE_WARN("{0} don't have DIFFUSE texture", mtlName.c_str());
							mtlAsset->SetSpecularMap(Renderer::GetWhiteTexture());
						}
					}
					//SPECULAR
					{
						int materialcount = 0;
						materialcount = aimaterial->GetTextureCount(aiTextureType_SPECULAR);
						if (materialcount)
						{
							aiString aistr = {};
							aimaterial->GetTexture(aiTextureType_SPECULAR, 0, &aistr);
							CORE_INFO("{0} SPECULAR texture: {1} ", mtlName.c_str(), aistr.C_Str());
							std::string texturepath = m_LoadPath + "/" + aistr.C_Str();
							Ref<Texture2D> texture = Texture2D::Create(spec,texturepath);
							
							mtlAsset->SetSpecularMap(texture);
						}
						else
						{
							CORE_WARN("{0} don't have Specular texture", mtlName.c_str());
							mtlAsset->SetSpecularMap(Renderer::GetBlackTexture());
						}
					}
					
					//shininess
					{
						float shininess;
						if (aimaterial->Get(AI_MATKEY_SHININESS, shininess) != aiReturn_SUCCESS)
							shininess = 80.0f; // Default value
						//mtl->Set(MaterialAsset::GetShininessLocation(), shininess);
					}
				}
				//PBR material
				{
					TextureSpecification spec;

					//Albedo
					{
						int materialcount = 0;
						materialcount = aimaterial->GetTextureCount(aiTextureType_BASE_COLOR);
						if (materialcount)
						{

							if (materialcount)
							{
								aiString aistr;
								aimaterial->GetTexture(aiTextureType_BASE_COLOR, 0, &aistr);
								CORE_INFO("{0} Albedo texture: {1} ", mtlName.c_str(), aistr.C_Str());
								std::string texturepath = m_LoadPath + "/" + aistr.C_Str();
								if (KansFileSystem::Exists(texturepath))
								{
									Ref<Texture2D> texture = Texture2D::Create(spec, texturepath);
									mtlAsset->SetAlbedoMap(texture);
								}
								else
								{
									mtlAsset->SetAlbedoMap(Renderer::GetWhiteTexture());
									CORE_WARN("{0} invalid Albedo texture path", mtlName.c_str());
								}
								
							}
						}
						else
						{
							mtlAsset->SetAOMap(Renderer::GetWhiteTexture());
							CORE_WARN("{0} don't have Albedo texture", mtlName.c_str());
						}
					}
					//AO
					{
						int materialcount = 0;
						materialcount = aimaterial->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION);
						if (materialcount)
						{

							if (materialcount)
							{
								aiString aistr;
								aimaterial->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &aistr);
								CORE_INFO("{0} AO texture: {1} ", mtlName.c_str(), aistr.C_Str());
								std::string texturepath = m_LoadPath + "/" + aistr.C_Str();
								if (KansFileSystem::Exists(texturepath))
								{
									Ref<Texture2D> texture = Texture2D::Create(spec, texturepath);
									mtlAsset->SetAOMap(texture);
								}
								else
								{
									mtlAsset->SetAOMap(Renderer::GetWhiteTexture());
									CORE_WARN("{0} invalid AO texture path", mtlName.c_str());
								}
								
							}
						}
						else
						{
							mtlAsset->SetAOMap(Renderer::GetWhiteTexture());

							CORE_WARN("{0} don't have AO texture", mtlName.c_str());
						}
					}
					//Rough
					{
						int materialcount = 0;
						materialcount = aimaterial->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS);
						if (materialcount)
						{

							if (materialcount)
							{
								aiString aistr;
								aimaterial->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &aistr);
								CORE_INFO("{0} Rough texture: {1} ", mtlName.c_str(), aistr.C_Str());
								std::string texturepath = m_LoadPath + "/" + aistr.C_Str();
								

								if (KansFileSystem::Exists(texturepath))
								{
									Ref<Texture2D> texture = Texture2D::Create(spec, texturepath);
									mtlAsset->SetRoughMap(texture);
									
								}
								else
								{
									mtlAsset->SetRoughMap(Renderer::GetWhiteTexture());
									CORE_WARN("{0} invalid Rough texture path", mtlName.c_str());
								}
							}
						}
						else
						{
							mtlAsset->SetRoughMap(Renderer::GetWhiteTexture());
							CORE_WARN("{0} don't have Rough texture", mtlName.c_str());
						}
					}

					//Metal
					{
						int materialcount = 0;
						materialcount = aimaterial->GetTextureCount(aiTextureType_METALNESS);
						if (materialcount)
						{

							if (materialcount)
							{
								aiString aistr;
								aimaterial->GetTexture(aiTextureType_METALNESS, 0, &aistr);
								CORE_INFO("{0} Metalness texture: {1} ", mtlName.c_str(), aistr.C_Str());
								std::string texturepath = m_LoadPath + "/" + aistr.C_Str();
								

								if (KansFileSystem::Exists(texturepath))
								{
									Ref<Texture2D> texture = Texture2D::Create(spec, texturepath);
									mtlAsset->SetMetalMap(texture);
								}
								else
								{
									mtlAsset->SetMetalMap(Renderer::GetBlackTexture());
									CORE_WARN("{0} invalid Metalness texture path", mtlName.c_str());
								}
							}
						}
						else
						{
							mtlAsset->SetMetalMap(Renderer::GetBlackTexture());
							CORE_WARN("{0} don't have Metalness texture", mtlName.c_str());
						}
					}
				}

				m_Materials.push_back(mtlAsset);
			}
			else
			{
				//TO do use the defualt material;
			}
		}
		
		
		return submesh;
	}

	void MeshSource::GenVertexArry()
	{
		PROFILE_FUCTION()
		double offset = 1.0 / 8000;
		std::vector<glm::vec3> SmoothNormal;
		Utils::MeshUtils::SmoothNormal(m_Verteices, SmoothNormal);
		Utils::MeshUtils::SetMeshOffset(m_Verteices, m_SubMeshes, offset);
		Utils::MeshUtils::BackMeshNormal(m_Verteices, SmoothNormal);
		BufferLayout layout = {
			{ShaderDataType::Float3,"a_Position"},
			{ShaderDataType::Float3,"a_Normal"},
			{ShaderDataType::Float2,"a_TextureCroods"},
			{ShaderDataType::Float4,"a_BaseColor"},
			{ShaderDataType::Float4,"a_Tangent"}
		};

		for (auto& submesh : m_SubMeshes)
		{
			auto& VA = VertexArray::Create();
			VA->Bind();
			auto& VB = VertexBuffer::Create((float*)&(m_Verteices[submesh.BaseVertex]), submesh.VertexCount * sizeof(Vertex));


			VB->Bind();
			VB->SetBufferLayout(layout);
			VA->AddVertexBuffer(VB);

			auto& IB = IndexBuffer::Create((uint32_t*)&(m_Indices[submesh.BaseIndex]), 3 * submesh.IndexCount);
			IB->Bind();
			VA->SetIndexBuffer(IB);
			m_VertexArray.push_back(VA);
		}
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