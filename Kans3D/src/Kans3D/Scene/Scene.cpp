#include "hzpch.h"

#include <glm/glm.hpp>
#include "Scene.h"
#include "Entity.h"
#include "Components.h"

#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/SceneRenderer.h"
namespace Kans
{	

	Scene::Scene()	
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

		
	}	

	void Scene::OnRenderer(Ref<SceneRenderer> renderer,TimeStep ts)
	{
		Entity cameraEntity = GetCameraEntity();
		if (!cameraEntity)
			return;
		//SceneInfo
		SceneInfo sceneinfo = {};

		CameraComponent& cameraCMP = cameraEntity.GetComponent<CameraComponent>();
		sceneinfo.sceneCamera.camera = cameraCMP.SceneCamera;
		glm::mat4 Cameratransform = cameraEntity.GetComponent<TransformComponent>().GetTransform();
		glm::vec3 camaerapos = cameraEntity.GetComponent<TransformComponent>().Position;
		//Renderer preparation
		{
			//viePosition;
			sceneinfo.sceneCamera.Position = camaerapos;
			sceneinfo.sceneCamera.viewMatrix = glm::inverse(Cameratransform);
			//dirLight
			{
				auto view = m_Registry.view<DirLightComponent>();
				for (auto entity : view)
				{
					auto [dirlightCMP] = view.get(entity);
					sceneinfo.dirLight.Dirction = dirlightCMP.Direction;
					sceneinfo.dirLight.Ambient_Intensity = dirlightCMP.Ambient_Intensity;
					sceneinfo.dirLight.Diffuse_Intensity = dirlightCMP.Diffuse_Intensity;
					sceneinfo.dirLight.Specular_Intensity = dirlightCMP.Specular_Intensity;
				}
			}
			//spotLight
			{
				auto view = m_Registry.view<PointLightComponent,TransformComponent>();
				for (auto entity : view)
				{
					auto [pointlightCMP,transformCMP] = view.get(entity);
					sceneinfo.pointLight.Position = transformCMP.Position;
					sceneinfo.pointLight.Ambient_Intensity = pointlightCMP.Ambient_Intensity;
					sceneinfo.pointLight.Diffuse_Intensity = pointlightCMP.Diffuse_Intensity;
					sceneinfo.pointLight.Specular_Intensity = pointlightCMP.Specular_Intensity;
				}
			}
			
		}
		//Renderer2D
#if 1
		{
			Renderer2D::BeginScene(sceneinfo.sceneCamera.camera.GetProjectMatrix(), Cameratransform);
			auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
			for (auto entity : group)
			{
				auto [transformCMP, spriteRendererCMP] = group.get(entity);
				Renderer2D::DrawQuad(transformCMP.GetTransform(), spriteRendererCMP.Texture, spriteRendererCMP.Color);

			}
			Renderer2D::EndScene();
		}
#endif
#if 1
		//StaticMesh renderer
		{
			renderer->BeginScene(sceneinfo);
			auto group = m_Registry.view<TransformComponent, StaticMeshComponent>();
			for (auto entity : group)
			{
				RenderCommand::EnableSetStencil(true);
				RenderCommand::StencilOp(StencilOption::KEEP, StencilOption::KEEP, StencilOption::REPLACE);
				RenderCommand::SetStencilFunc(StencilFunction::ALWAYS, 1, 0xff);	
				RenderCommand::SetStencilMask(0xff);
				auto [transformCMP, meshCMP] = group.get(entity);
				//renderer->SubmitStaticMesh(meshCMP.StaticMesh, meshCMP.MaterialTable, transformCMP.GetTransform());
				//ToneShader
				//RenderCommand::EnableCullFace(false);
				if(1)
				{
					Entity e = { entity,this };
					auto& materialCMP = e.GetComponent<MaterialComponent>();
					//renderer->SubmitStaticMeshToneshading(meshCMP.StaticMesh, transformCMP.GetTransform(), data);
					//renderer->SubmitStaticMeshDebug(meshCMP.StaticMesh, transformCMP.GetTransform());
					renderer->SubmitToneCharactorShader(meshCMP.StaticMesh, transformCMP.GetTransform());
				}
				//RenderCommand::EnableCullFace(true);


				//Outline
				if(0)
				{
					RenderCommand::SetStencilFunc(StencilFunction::NOTEQUAL, 1, 0xff);
					RenderCommand::SetStencilMask(0x00);
					TransformComponent ts = transformCMP;
					ts.Scale *= 1.007f;
					renderer->SubmitStaticMeshStencil(meshCMP.StaticMesh, ts.GetTransform());
					RenderCommand::SetStencilMask(0xff);
					RenderCommand::EnableSetStencil(false);

				}
				
				// Stroke
				if(0)
				{
					RenderCommand::EnableCullFace(true);
					RenderCommand::CullFace(CullFaceOption::FRONT);
					renderer->SubmitStaticMeshOutLine(meshCMP.StaticMesh, transformCMP.GetTransform());
					RenderCommand::CullFace(CullFaceOption::BACK);
					RenderCommand::EnableCullFace(false);
				}
				//DebugNormalShader
				if(0)
				{
					renderer->SubmitStaticMeshDebugNormal(meshCMP.StaticMesh, transformCMP.GetTransform());
				}
				
				//renderer->SubmitMeshPost()
			}
			renderer->EndScene();
		}
#endif	
		// Post effect
		{
			
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
		Entity e = { m_Registry.create(),this };
		e.AddComponent<TransformComponent>();
		auto& tag = e.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity": name;
		return e;
	}

	void Scene::DeleteEntity(Entity entity)
	{
		m_Registry.destroy(entity);
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
		static_assert(false);
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
}