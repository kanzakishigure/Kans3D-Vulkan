#include "kspch.h"

#include <glm/glm.hpp>
#include "Scene.h"
#include "Entity.h"
#include "Components.h"

#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/SceneRenderer.h"
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
				Entity entt = { entity,this };
				OpenGLRenderCommand::EnableSetStencil(true);
				OpenGLRenderCommand::StencilOp(StencilOption::KEEP, StencilOption::KEEP, StencilOption::REPLACE);
				OpenGLRenderCommand::SetStencilFunc(StencilFunction::ALWAYS, 1, 0xff);
				OpenGLRenderCommand::SetStencilMask(0xff);
				auto [transformCMP, meshCMP] = group.get(entity);
				//renderer->SubmitStaticMesh(meshCMP.StaticMesh, meshCMP.MaterialTable, transformCMP.GetTransform());
				//ToneShader
				OpenGLRenderCommand::EnableCullFace(false);
				if(m_RenderResource.Piplinestate.EnableToneShader)
				{
					Entity e = { entity,this };

					renderer->SubmitToneCharactorShader(meshCMP.StaticMesh, transformCMP.GetTransform());
				}
				OpenGLRenderCommand::EnableCullFace(true);


				//Stencil
				if(m_RenderResource.Piplinestate.EnableStencil)
				{
					OpenGLRenderCommand::SetStencilFunc(StencilFunction::NOTEQUAL, 1, 0xff);
					OpenGLRenderCommand::SetStencilMask(0x00);
					TransformComponent ts = transformCMP;
					ts.Scale *= 1.007f;
					renderer->SubmitStaticMeshStencil(meshCMP.StaticMesh, ts.GetTransform());
					OpenGLRenderCommand::SetStencilMask(0xff);
					OpenGLRenderCommand::EnableSetStencil(false);

				}
				
				// Stroke
				if(m_RenderResource.Piplinestate.EnableOutline)
				{
					OpenGLRenderCommand::EnableCullFace(true);
					OpenGLRenderCommand::CullFace(CullFaceOption::FRONT);
					renderer->SubmitStaticMeshOutLine(meshCMP.StaticMesh, transformCMP.GetTransform());
					OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
					OpenGLRenderCommand::EnableCullFace(false);
				}
				//DebugNormalShader
				if(m_RenderResource.Piplinestate.EnableDebugNormal)
				{
					renderer->SubmitStaticMeshDebugNormal(meshCMP.StaticMesh, transformCMP.GetTransform());
				}

				//StaticMesh
				if (m_RenderResource.Piplinestate.EnableDefaultShader)
				{
					if (entt.HasComponent<MaterialComponent>())
					{
						Entity e = { entity,this };
						auto& materialCMP = e.GetComponent<MaterialComponent>();

						OpenGLRenderCommand::EnableCullFace(true);
						OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
						renderer->SubmitStaticMesh(meshCMP.StaticMesh, materialCMP.MaterialTable, transformCMP.GetTransform());
						OpenGLRenderCommand::EnableCullFace(false);
					}
				}
				//Spot cloud
				if (false)
				{
					Entity e = { entity,this };
					auto& materialCMP = e.GetComponent<MaterialComponent>();

					OpenGLRenderCommand::EnableCullFace(true);
					renderer->SubmitSpotCloud(meshCMP.StaticMesh, materialCMP.MaterialTable, transformCMP.GetTransform());
					OpenGLRenderCommand::EnableCullFace(false);
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