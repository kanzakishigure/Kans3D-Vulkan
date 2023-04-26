#pragma once
#include <entt.hpp>
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Core/UUID.h"

#include "Kans3D/Renderer/Mesh.h"
namespace  Kans
{

	struct DirLight
	{
		glm::vec3 Dirction;
		glm::vec3 Diffuse_Intensity;
		glm::vec3 Specular_Intensity;
		glm::vec3 Ambient_Intensity;
	};
	struct PointLight
	{
		glm::vec3 Position;
		glm::vec3 Diffuse_Intensity;
		glm::vec3 Specular_Intensity;
		glm::vec3 Ambient_Intensity;
	};

	struct PipLineState
	{
		bool EnableOutline = true;
		bool EnableToneShader = true;
		bool EnableStencil = false;
		bool EnableDebugNormal = false;
		bool EnableDefaultShader = false;
	};
	struct  RenderResource
	{
		PointLight pointLight;
		DirLight dirLight;
		PipLineState Piplinestate;

	};

	class SceneRenderer;
	class Entity;
	
	using EntityMap = std::unordered_map<UUID, Entity>;
	//BlingPhong Material
	

	//the scene to the gameplayer system
	class Scene : public Asset
	{

	public:
		Scene(const std::string& name = "UntitledScene");
		~Scene();

		void OnUpdate(TimeStep ts);
		void OnRenderer(Ref<SceneRenderer> renderer, TimeStep ts);
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

		const RenderResource& GetRenderResource() { return m_RenderResource; }
		void SetRenderResource(const RenderResource& resource) { m_RenderResource = resource; }

	private:
		template<typename T> 
		void OnComponentAdd(Entity entity, T& component );
	private:
		entt::registry m_Registry;
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
		std::string m_Name;
		EntityMap m_EntityMap;
	private:
		DirLight dirLight;
		PointLight pointLight;
		RenderResource m_RenderResource;
		
		friend class Entity;
		friend class SceneHierachyPanel;
		friend class SceneRenderer;
		friend class SceneSerializer;
	};
	//the scene to the renderer
	class RenderScene : public Asset
	{
	public:
		RenderScene();
		~RenderScene();
	private:
		friend class SceneRenderer;
	};

	


}