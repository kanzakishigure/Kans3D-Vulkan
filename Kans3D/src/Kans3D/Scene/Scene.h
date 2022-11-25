#pragma once
#include <entt.hpp>
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Renderer/Mesh.h"
namespace  Kans
{
	class SceneRenderer;
	class Entity;
	
	//BlingPhong Material
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

	class Scene
	{

	public:
		Scene(const std::string& name = "UntitledScene");
		~Scene();

		const entt::registry& Reg() const { return m_Registry; }
		entt::registry& Reg() { return m_Registry; }

		void OnUpdate(TimeStep ts);
		void OnRenderer(Ref<SceneRenderer> renderer, TimeStep ts);
		void OnViewportResize(uint32_t width, uint32_t height);

		Entity CreateEntity(const std::string name = std::string());
		void DeleteEntity(Entity entity);

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
	private:
		DirLight dirLight;
		PointLight pointLight;

		
		friend class Entity;
		friend class SceneHierachyPanel;
		friend class SceneRenderer;
		friend class SceneSerializer;
	};

	


}