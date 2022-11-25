#include "kspch.h"
#include "SceneSerializer.h"
#include "Entity.h"
#include "Components.h"
#include "Core/UUID.h"

#include <yaml-cpp/yaml.h>
namespace Kans
{
	
	SceneSerializer::SceneSerializer(const Ref<Scene>& Scene)
		:m_Scene(Scene)
	{

	}

	void SceneSerializer::Serialize(const std::filesystem::path& path)
	{
		YAML::Emitter yamlos;
		yamlos << YAML::BeginMap;
		yamlos << YAML::Key   << "Scene";
		yamlos << YAML::Value <<  m_Scene->GetName();
		
		auto entities = m_Scene->m_Registry.view<TagComponent>();
		for (entt::entity entity : entities)
		{
			yamlos << YAML::BeginMap;
			yamlos << YAML::Key << "Entity";
			UUID id ;
			yamlos << YAML::Value << (uint32_t)id;
			Entity e ={ entity,m_Scene.get()};
			if (e.HasComponent<TagComponent>())
			{
				yamlos << YAML::Key << "TagComponent";
				yamlos << YAML::BeginMap;
				yamlos << YAML::Key << "Tag";
				yamlos << YAML::Value << e.GetComponent<TagComponent>().Tag;
				yamlos << YAML::EndMap;
			}
			yamlos << YAML::EndMap;
		}

		yamlos << YAML::EndMap;

		std::ofstream fout(path);
		fout << yamlos.c_str();
	}

	void SceneSerializer::SerializeRuntime(const std::filesystem::path& path)
	{

	}

}