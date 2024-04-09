#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Renderer/Resource/Environment.h"
#include "Kans3D/Scene/Scene.h"
#include "Kans3D/Editor/EditorCamera.h"
namespace Kans
{
	class Scene;
	class SceneRenderer;
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
		bool EnableOutline = false;
		bool EnableToneShader = false;
		bool EnableStencil = false;
		bool EnableDebugNormal = false;
		bool EnableDefaultShader = true;
		bool EnableGridShader = true;


	};

	struct  RenderResource
	{
		PointLight pointLight;
		DirLight dirLight;
		PipLineState Piplinestate;

	};

	
	class RenderScene :public Asset
	{
	public:
		RenderScene(const Ref<Scene>& logicScene);
		~RenderScene();
		
		void PrepareEnvironment(const std::filesystem::path& envMapPath);
		void UpdateRenderable();
		void OnRenderEditor(Ref<SceneRenderer> renderer, TimeStep ts, const EditorCamera& camera);
		void OnRenderRuntime(Ref<SceneRenderer> renderer, TimeStep ts);

		void OnViewportResize(uint32_t width, uint32_t height);

		const RenderResource& GetRenderResource() { return m_RenderResource; }
		void SetRenderResource(const RenderResource& resource) { m_RenderResource = resource; }

	private:

		Ref<Scene> m_LogicScene;

		EntityMap m_2DRenderable;
		EntityMap m_3DRenderable;
		Ref<SceneRenderer> m_SceneRenderer;
		
		
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
		
		//temp for create Environment
		RenderResource m_RenderResource;
		std::filesystem::path m_EnvironmentPath;

		friend class Scene;
		friend class SceneRenderer;
		
	};

	
}