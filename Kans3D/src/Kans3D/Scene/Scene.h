#pragma once
#include <entt.hpp>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Core/UUID.h"


namespace  Kans
{
	class RenderScene;
	class SceneRenderer;
	class Entity;
	
	using EntityMap = std::unordered_map<UUID, Entity>;

	

	//the scene to the GamePlayer system
	class Scene : public Asset
	{

	public:
		Scene(const std::string& name = "UntitledScene");
		~Scene();

		void OnUpdate(TimeStep ts);
		
		void OnViewportResize(uint32_t width, uint32_t height);

		Entity CreateEntity(const std::string name = std::string());
		Entity Scene::CreateEntityWithID(UUID uuid, const std::string& name, bool runtimeMap);
		void DestroyEntity(Entity entity);
		Entity GetEntityByUUID(UUID uuid) const;

		void OnRuntimeStart();
		void OnRuntimeStop();
		

		Entity GetCameraEntity();

		const std::string GetName() const { return m_Name; }
		void SetName(const std::string& name ) { m_Name = name; }

		

	private:
		template<typename T> 
		void OnComponentAdd(Entity entity, T& component );
	private:
		entt::registry m_Registry;
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
		std::string m_Name;
		EntityMap m_EntityMap;
	private:

		
		friend class Entity;
		friend class SceneHierachyPanel;
		friend class RenderScene;
		friend class SceneSerializer;
	};
	

	


}