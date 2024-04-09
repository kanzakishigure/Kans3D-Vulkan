#include "kspch.h"
#include "MeshUtils.h"
namespace Kans::Utils {
	

	bool IsSameVector(glm::vec3& v1, glm::vec3& v2)
	{
		return (v1.x == v2.x) && (v1.y == v2.y) && (v1.z == v2.z);
	}

	void MeshUtils::SmoothNormal(std::vector<Vertex>& verteices, std::vector<glm::vec3>& smoothedNormal)
	{
		PROFILE_FUCTION();
		//std::vector<Vertex> SmoothNormalVertex;
		std::vector<glm::vec3*> sameposition;
		smoothedNormal.reserve(verteices.size());
		for (int i = 0; i < verteices.size(); i++)
		{
			smoothedNormal.emplace_back(verteices[i].Normal);
		}
		for (int i = 0; i <verteices.size()-1; i++)
		{
			sameposition.clear();
			glm::vec3 norm = verteices[i].Normal;
			for (int j = i+1; j < verteices.size(); j++)
			{
				
				if (verteices[j].Position == verteices[i].Position)
				{
					sameposition.push_back(&smoothedNormal[j]);
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

	void MeshUtils::FastSmoothNormal(std::vector<Vertex>& verteices)
	{
		//执行速度未得到提升
		PROFILE_FUCTION();
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

	void MeshUtils::SetMeshOffset(std::vector<Vertex>& verteices, const std::vector<SubMesh>& submushes, float offset)
	{
		PROFILE_FUCTION();
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

	void MeshUtils::BackMeshNormal(std::vector<Vertex>& verteices, const std::vector<glm::vec3>& backedNormal)
	{
		CORE_ASSERT(verteices.size() == backedNormal.size(), "Cnan't Bake Normal to a diffrentMesh,BakeMesh have Normal Count :{0} ", backedNormal.size());
		for (int i = 0; i < verteices.size() - 1; i++)
		{
			glm::vec3 tangent = { verteices[i].Tangent.x, 
								  verteices[i].Tangent.y, 
								  verteices[i].Tangent.z };
			auto Bitangent = glm::cross(verteices[i].Normal, tangent)*verteices[i].Tangent.w;
			glm::mat4 TBN =
			{
				{glm::normalize(tangent),0.0},
				{glm::normalize(Bitangent),0.0},
				{glm::normalize(verteices[i].Normal),0.0},
				glm::vec4(0.0),
			};
			TBN = glm::transpose(TBN);
			verteices[i].BaseColor = glm::normalize(TBN * glm::vec4(backedNormal[i], 0.0));
		}
	}


	
}