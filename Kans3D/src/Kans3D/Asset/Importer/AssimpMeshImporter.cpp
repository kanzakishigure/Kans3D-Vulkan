#include "kspch.h"
#include "AssimpMeshImporter.h"


#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags


const uint32_t importFlag =
aiProcess_CalcTangentSpace  //�������߿ռ�
| aiProcess_Triangulate //��֤һ��ÿ��ͼԪ�Ļ�����λ�������Σ�����ֶ������
| aiProcess_JoinIdenticalVertices
| aiProcess_SortByPType
| aiProcess_GenSmoothNormals;//����ƽ�����ߣ���֤ʵ�����Ч����ʱ�򲻻����Ӳ�߶��ѵĽ��

namespace Kans
{

	AssimpMeshImporter::AssimpMeshImporter(const std::filesystem::path& path)
		:m_Path(path)
	{
		
	}

	Ref<MeshSource> AssimpMeshImporter::ImportToMeshSource()
	{
		Ref<MeshSource> meshSource = CreateRef<MeshSource>();


		CORE_INFO_TAG("Mesh", "Loading mesh: {0}", m_Path.string());


		Assimp::Importer importer = Assimp::Importer();
		const aiScene* scene = importer.ReadFile(m_Path.string(), importFlag);

		
		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
		{
			CORE_ERROR_TAG("Mesh", "Failed to load mesh file: {0}", m_Path.string());
			CORE_ERROR_TAG("Mesh", importer.GetErrorString());
			return nullptr;
		}




		/*
		
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
		*/
		return nullptr;
	}

}