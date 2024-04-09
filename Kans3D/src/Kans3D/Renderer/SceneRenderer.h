#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Renderer/RHI/FrameBuffer.h"

#include "Kans3D/Renderer/Resource/Mesh.h"
#include "Kans3D/Renderer/Resource/SceneCamera.h"
#include "Kans3D/Renderer/Resource/RenderScene.h"

namespace Kans
{
	struct SceneRendererCamera
	{
		Camera camera;
		glm::mat4 viewMatrix;
		glm::vec3 Position;
	};
	struct SceneInfo
	{
		DirLight dirLight;
		PointLight pointLight;
		SceneRendererCamera sceneCamera;
	};
	struct SceneRendererSpecification
	{
		bool SwapChainTarget = false;
	};
	
	class Scene;
	class SceneRenderer : public RefCounter
	{
		public:
			SceneRenderer(RenderScene* renderScene, SceneRendererSpecification spec = SceneRendererSpecification());
			~SceneRenderer(){};

			void Init();
			void PrepareEnvironment();
			void BeginScene(SceneInfo info);
			void EndScene();
			void SetScene(RenderScene* scene);
			void SetViewportSize(uint32_t width, uint32_t height);
			std::vector<uint32_t> GetViewportSize() { return { m_ViewportWidth, m_ViewportHeight }; }

			void SubmitStaticMesh(Ref<StaticMesh> mesh,Ref<MaterialTable> materialTabel,glm::mat4 transform);
			void SubmitPBRStaticMesh(Ref<StaticMesh> mesh, Ref<MaterialTable> materialTabel, glm::mat4 transform);

			void SubmitStaticMesh(Ref<StaticMesh> mesh, Ref<Shader> shader);
			void SubmitStaticMeshStencil(Ref<StaticMesh> mesh, glm::mat4 transform);
			void SubmitStaticMeshPostEffect(Ref<StaticMesh> mesh,Ref<Texture2D> attachment, glm::mat4 transform);
			void SubmitStaticMeshOutLine(Ref<StaticMesh> mesh, glm::mat4 transform);
			void SubmitStaticMeshToneshading(Ref<StaticMesh> mesh, glm::mat4 transform);

			void SubmitSpotCloud(Ref<StaticMesh> mesh, Ref<MaterialTable> materialTabel, glm::mat4 transform);
			void SubmitMeshEntity(Ref<StaticMesh> mesh, UUID uuid, glm::mat4 transform);
			void SubmitGrid( float size,float scale, glm::mat4 transform);
			void SubmitSkybox(Ref<TextureCube> skybox);


			uint32_t GetOutput(uint32_t index)const;
			const Environment& GetEnvironment() { return m_Environment; }
			const Ref<TextureCube> GetSkybox() { return m_SkyBox; }
		public:
			void SubmitStaticMeshDebugNormal(Ref<StaticMesh> mesh, glm::mat4 transform);
			void SubmitStaticMeshDebug(Ref<StaticMesh> mesh, glm::mat4 transform);
			void SubmitToneCharactorShader(Ref<StaticMesh> mesh, glm::mat4 transform);
		private:
			//SceneRenderer did't handle the life of Scnene;
			RenderScene* m_RenderScene;
			SceneInfo m_SceneInfo;
			SceneRendererSpecification m_Specification;

			uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
			Ref<FrameBuffer> m_FrameBuffer;
			
			Environment m_Environment;
			Ref<TextureCube> m_SkyBox;
			Ref<Texture2D> m_BrdfLUT;
			bool m_Active = false;
	};
}