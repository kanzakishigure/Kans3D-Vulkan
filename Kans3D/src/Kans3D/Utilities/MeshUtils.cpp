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

	struct Vec3Hash 
	{
		std::size_t operator()(const glm::vec3& v) const {
				// 将浮点数按位转换为整数进行哈希
				std::size_t h1 = std::hash<float>{}(v.x);
				std::size_t h2 = std::hash<float>{}(v.y);
				std::size_t h3 = std::hash<float>{}(v.z);
				return h1 ^ (h2 << 1) ^ (h3 << 2);
		}
	};

// 自定义相等比较函数（支持容差）
	struct Vec3Equal 
	{
		bool operator()(const glm::vec3& a, const glm::vec3& b) const {
				// 使用容差比较，避免浮点精度问题
				const float EPSILON = 1e-6f;
				return glm::distance(a, b) < EPSILON;
		}
	};

	void MeshUtils::SmoothNormalHash(std::vector<Vertex>& verteices, std::vector<glm::vec3>& smoothedNormal)
	{
		PROFILE_FUCTION();


		smoothedNormal.resize(verteices.size());
		
		// 使用 unordered_map 按位置分组，key = 位置，value = {法线累加值, 顶点索引列表}
		std::unordered_map<glm::vec3, std::pair<glm::vec3, std::vector<size_t>>, Vec3Hash, Vec3Equal> positionMap;
		positionMap.reserve(verteices.size() * 2);
		
		// 第一遍：遍历所有顶点，按位置分组并累加法线
		for (size_t i = 0; i < verteices.size(); ++i) {
				const auto& vertex = verteices[i];
				auto it = positionMap.find(vertex.Position);
				
				if (it == positionMap.end()) {
						// 新位置：初始化累加法线和索引列表
						positionMap.emplace(vertex.Position, std::make_pair(vertex.Normal, std::vector<size_t>{i}));
				} else {
						// 已有位置：累加法线并添加索引
						it->second.first += vertex.Normal;
						it->second.second.push_back(i);
				}
		}
		
		// 第二遍：对每个位置，归一化累加法线并赋值给所有共享该位置的顶点
		for (auto& kv : positionMap) {
				glm::vec3 normalizedNormal = glm::normalize(kv.second.first);
				for (size_t index : kv.second.second) {
						smoothedNormal[index] = normalizedNormal;
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