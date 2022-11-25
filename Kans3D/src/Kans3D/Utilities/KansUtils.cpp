#include "kspch.h"
#include "Kans3D/Utilities/KansUtils.h"
namespace Kans {
	
	namespace Utils {
		bool IsSameVector(glm::vec3& v1, glm::vec3& v2)
		{
			return (v1.x == v2.x) && (v1.y == v2.y) && (v1.z == v2.z);
		}

	}
	void MeshUtil::SmoothNormal(std::vector<Vertex>& verteices, std::vector<glm::vec3>& smoothnormal)
	{
		HZ_PROFILE_FUCTION();
		//std::vector<Vertex> SmoothNormalVertex;
		std::vector<glm::vec3*> sameposition;
		smoothnormal.reserve(verteices.size());
		for (int i = 0; i < verteices.size(); i++)
		{
			smoothnormal.emplace_back(verteices[i].Normal);
		}
		for (int i = 0; i <verteices.size()-1; i++)
		{
			sameposition.clear();
			glm::vec3 norm = verteices[i].Normal;
			for (int j = i+1; j < verteices.size(); j++)
			{
				
				if (verteices[j].Position == verteices[i].Position)
				{
					sameposition.push_back(&smoothnormal[j]);
					norm += verteices[j].Normal;
				}
			}
			norm = glm::normalize(norm);
			for each (auto var in sameposition)
			{
				*var = norm;
			}
		}
	}

	void MeshUtil::FastSmoothNormal(std::vector<Vertex>& verteices)
	{
		//执行速度未得到提升
		HZ_PROFILE_FUCTION();
		std::map<uint32_t, std::vector<uint32_t>> vertexindex;
		std::map <uint32_t, glm::vec3> posiotns;
		std::vector<glm::vec3*> sameposition;
		for (int i = 0; i < verteices.size() - 1; i++)
		{
			auto it = posiotns.begin();
			for (;it!= posiotns.end();it++)
			{
				if (it->second == verteices[i].Position)
				{
					vertexindex[it->first].push_back(i);
					break;
				}
			}
			if (it == posiotns.end())
			{
				posiotns[i] = verteices[i].Position;
			}	
		}
		for (auto& vertex : vertexindex)
		{
			glm::vec3 norm=glm::vec3(1.0);
			for (auto& index : vertex.second)
			{
				norm += verteices[index].Normal;
			}
			norm = normalize(norm);
			for (auto& index : vertex.second)
			{
				verteices[index].Normal = norm;
			}
		}

	}

	void MeshUtil::SetMeshOffset(std::vector<Vertex>& verteices, const std::vector<SubMesh>& submushes, float offset)
	{
		HZ_PROFILE_FUCTION();
		glm::vec3 offsetdir = glm::vec3(0.0);
		int offsetindex = 0;
		for (auto & submesh : submushes)
		{
			
			uint32_t count = submesh.BaseVertex + submesh.VertexCount;
			for (uint32_t i = submesh.BaseVertex; i < count; i++)
			{
				offsetdir = verteices[i].Normal* (offset*offsetindex);
				verteices[i].Position += offsetdir;
			}
			offsetindex++;
		}
	}

	void MeshUtil::BackMeshNormal(std::vector<Vertex>& verteices, const std::vector<glm::vec3>& smoothnormal)
	{
		HZ_ASSERT(verteices.size() == smoothnormal.size(), "Cnan't Bake Normal to a diffrentMesh,BakeMesh have Normal Count :{0} ",smoothnormal.size());
		for (int i = 0; i < verteices.size() - 1; i++)
		{
			auto Bitangent = glm::cross(verteices[i].Normal, verteices[i].Tangent);
			glm::mat4 TBN =
			{
				{glm::normalize(verteices[i].Tangent),0.0},
				{glm::normalize(Bitangent),0.0},
				{glm::normalize(verteices[i].Normal),0.0},
				glm::vec4(0.0),
			};
			TBN = glm::transpose(TBN);
			verteices[i].BaseColor = glm::normalize(TBN * glm::vec4(smoothnormal[i], 0.0));
		}
	}


	void MaterialUtil::InitMaterial(const Ref<MaterialTable> materialTable)
	{

		int matCount = materialTable->GetMaterialCount();
#if 0
		for (uint32_t i = 0; i < matCount; i++)
		{
			auto& mat = materialTable->GetMaterialAsset(i)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
		}
#endif


		//tone shader test data

#if 1
		{

		}
		//Face
		{
			auto& mat = materialTable->GetMaterialAsset(0)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
			mat->Set("U_RampSkin", glm::vec2{ 0.95 });
			mat->Set("U_RampTights", glm::vec2{ 0.45 });
			mat->Set("U_RampMetal", glm::vec2{ 0.35 });
			mat->Set("U_RampSoft", glm::vec2{ 0.15 });
			mat->Set("U_RampSilk", glm::vec2{ 0.05 });
		}
		//Hair
		{
			auto& mat = materialTable->GetMaterialAsset(1)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
			mat->Set("U_RampSkin", glm::vec2{ 0.95 });
			mat->Set("U_RampTights", glm::vec2{ 0.45 });
			mat->Set("U_RampMetal", glm::vec2{ 0.35 });
			mat->Set("U_RampSoft", glm::vec2{ 0.15 });
			mat->Set("U_RampSilk", glm::vec2{ 0.05 });
		}
		//Clothes
		{
			auto& mat = materialTable->GetMaterialAsset(2)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
			mat->Set("U_RampSkin", glm::vec2{ 0.95 });
			mat->Set("U_RampTights", glm::vec2{ 0.45 });
			mat->Set("U_RampMetal", glm::vec2{ 0.35 });
			mat->Set("U_RampSoft", glm::vec2{ 0.15 });
			mat->Set("U_RampSilk", glm::vec2{ 0.05 });
		}
		//Skin
		{
			auto& mat = materialTable->GetMaterialAsset(3)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
			mat->Set("U_RampSkin", glm::vec2{ 0.95 });
			mat->Set("U_RampTights", glm::vec2{ 0.45 });
			mat->Set("U_RampMetal", glm::vec2{ 0.35 });
			mat->Set("U_RampSoft", glm::vec2{ 0.15 });
			mat->Set("U_RampSilk", glm::vec2{ 0.05 });
		}


#endif 
	}
}