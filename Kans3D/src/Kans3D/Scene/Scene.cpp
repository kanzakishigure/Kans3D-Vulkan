#include "kspch.h"

#include <glm/glm.hpp>
#include "Scene.h"
#include "Entity.h"
#include "Components.h"


#include "Kans3D/Scene/ScriptableEntity.h"
#include "Kans3D/Script/ScriptEngine.h"

namespace Kans
{	

	Scene::Scene(const std::string& name)
		:m_Name(name)
	{
	
		
	}

	Scene::~Scene()
	{

	}

	void Scene::OnUpdate(TimeStep ts)
	{

		//update ScripteableEntity
		{
			
			m_Registry.view<NativeScriptComponent>().each([=](auto entity,auto& nsc) {
				
				if (!nsc.Instance)
				{
					nsc.Instance =  nsc.InstantiateFunction();
					nsc.Instance->m_Entity = Entity{ entity,this };
					nsc.Instance->OnCreate();
				}
				
				nsc.Instance->OnUpdate(ts);
				nsc.Instance->OnDestory();
				nsc.DestoryInstanceFunction(&nsc);
				});
		}
		//update Script
		auto view = m_Registry.view<ScriptComponent>();
		for (auto entity : view)
		{
			Entity e = { entity,this };
			ScriptEngine::OnUpdateEntity(e,ts);
		}
	}	

	
	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{

		m_ViewportWidth = width;
		m_ViewportHeight = height;

		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			auto& cameraCMP = view.get<CameraComponent>(entity);
			if (!cameraCMP.FixedAspectRatio)
			{
				cameraCMP.SceneCamera.SetViewportSize(width, height);
			}
			
		}
	}

	Entity Scene::CreateEntity(const std::string name)
	{
		UUID id = UUID();
		return CreateEntityWithID(id, name,false);
	}
	Kans::Entity Scene::CreateEntityWithID(UUID uuid, const std::string& name, bool runtimeMap)
	{
		Entity entity = { m_Registry.create(),this };
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<IDComponent>(uuid);
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

		CORE_ASSERT(m_EntityMap.find(uuid)== m_EntityMap.end(),nullptr);
		m_EntityMap[uuid] = entity;
		return entity;
	}
	void Scene::DestroyEntity(Entity entity)
	{
		auto&id = entity.GetComponent<IDComponent>().ID;
		m_EntityMap.erase(id);
		m_Registry.destroy(entity);
	}

	Entity Scene::GetEntityByUUID(UUID uuid) const
	{
		if (m_EntityMap.find(uuid) != m_EntityMap.end())
		{
			return m_EntityMap.at(uuid);
		}
		return {};
	}

	void Scene::OnRuntimeStart()
	{
	
		//Init 
		ScriptEngine::OnRuntimeStart(this);
		auto view = m_Registry.view<ScriptComponent>();
		for (auto entity : view)
		{
			Entity e = { entity,this };
			ScriptEngine::OnCreateEntity(e);

		}
	}

	void Scene::OnRuntimeStop()
	{
		ScriptEngine::OnRuntimeEnd();
		m_EntityMap.clear();
	}

	Kans::Entity Scene::GetCameraEntity()
	{
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			auto [cameraCMP] = view.get(entity);
			if (cameraCMP.Primary)
			{
				return {entity ,this};
			}

		}
		return {};
	}

	//To use entt use the call back function we should implement the function advance
	template<typename T>
	void Scene::OnComponentAdd(Entity entity, T& component)
	{
		static_assert(sizeof(T)==0);
	}




	template<>
	void Scene::OnComponentAdd<CameraComponent>(Entity entity, CameraComponent& component)
	{
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
			component.SceneCamera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
	}
	
	template<>
	void Scene::OnComponentAdd<TransformComponent>(Entity entity, TransformComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<StaticMeshComponent>(Entity entity, StaticMeshComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<TagComponent>(Entity entity, TagComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<NativeScriptComponent>(Entity entity, NativeScriptComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<SpriteRendererComponent>(Entity entity, SpriteRendererComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<DirLightComponent>(Entity entity, DirLightComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<PointLightComponent>(Entity entity, PointLightComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<MaterialComponent>(Entity entity, MaterialComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<IDComponent>(Entity entity, IDComponent& component)
	{

	}
	template<>
	void Scene::OnComponentAdd<ScriptComponent>(Entity entity, ScriptComponent& component)
	{

	}
}