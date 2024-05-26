#include "EditorLayer.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <Kans3D/Platform/OpenGL/OpenGLShader.h>

#include "ImGuizmo.h"
//TestInclude
#include <Kans3D/Core/UUID.h>
#include <Kans3D/ImGui/Colors.h>
#include <Kans3D/Renderer/Resource/MeshFactory.h>
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ShowImguiDemo		false	
#define ShowEditorUI		true
#define EnbaleDocking		true
#define CreatePrimitives	false

#define TestLoadNPRMaterial	false
#define TestLoadSkinMesh	false	
#define TestLoadPBRMaterial false
#define TestCrash			false	

#define SpotCloudTest		false
#define NativeScript		false
#define ShowDepthBuffer		false
#define ShowColorBuffer     true
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Kans
{
	

	EditorLayer::EditorLayer()
		:m_EditorCamera(45.0f,1920,1080,0.1,1000), Layer("EditorLayer")
	{	

	}
	void EditorLayer::OnAttach()
	{
		PROFILE_FUCTION();


		//Resource Init
		EditorResources::Init();


		// scene init
		{
				
			
			//Create Scene
			{
				m_ActiveScene = CreateRef<Scene>("TestDemo");
				m_ActiveScene->OnViewportResize(
					(uint32_t)Application::Get().GetWindow().GetWidth(), 
					(uint32_t)Application::Get().GetWindow().GetHeight());
				auto pointlight = m_ActiveScene->CreateEntity("PointLight");

				auto& plightCMP = pointlight.AddComponent<PointLightComponent>();
				plightCMP.Diffuse_Intensity = glm::vec3(1.0);
				plightCMP.Specular_Intensity = glm::vec3(1.0);
				plightCMP.Ambient_Intensity = glm::vec3(1.0);
				auto& CMP = pointlight.GetComponent<TransformComponent>();
				CMP.Position = { 20.0,20.0,1.0 };

				auto DirLight = m_ActiveScene->CreateEntity("DirLight");
				auto& dirCMP = DirLight.AddComponent<DirLightComponent>();
				dirCMP.Diffuse_Intensity = glm::vec3(1.0);
				dirCMP.Specular_Intensity = glm::vec3(1.0);
				dirCMP.Ambient_Intensity = glm::vec3(1.0);
			}

			//Create Scene camera
			{
				m_CameraEntity = m_ActiveScene->CreateEntity("mainCamera");
				auto& transfromCMP = m_CameraEntity.GetComponent<TransformComponent>();
				transfromCMP.Position = { 0.0f,0.7f,1.5f };
				auto& cmp = m_CameraEntity.AddComponent<CameraComponent>();
				cmp.SceneCamera.SetViewportSize(1920, 1080);
			}
#if CreatePrimitives
			//Create Cube
			{
				auto& cubeEntity = m_ActiveScene->CreateEntity("cube");
				auto& meshCMP = cubeEntity.AddComponent<StaticMeshComponent>();
				meshCMP.StaticMesh = Kans::MeshFactory::CreatCube(glm::vec3(1.0f));
				meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();
				auto& materialCMP = cubeEntity.AddComponent<MaterialComponent>();
				materialCMP.MaterialTable = meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();
				auto& TransformCMP = cubeEntity.GetComponent<TransformComponent>();
				TransformCMP.Position = { 0.0f,0.0f,-3.0f };
				TransformCMP.Rotation = { 0.0f,0.0f,0.0f };
				TransformCMP.Scale = { 1.0f, 1.0f, 1.0f };

			}
#endif
			// load Mesh test
#if TestLoadNPRMaterial
			{

				//ref BackScene
				
				{

					auto RefEntity = m_ActiveScene->CreateEntity("RefEntity");
					auto& spritCMP = RefEntity.AddComponent<SpriteRendererComponent>();

					TextureSpecification spec;
					spritCMP.Texture = Texture2D::Create(spec, "assets/textures/GY.png");
					auto& transformCMP = RefEntity.GetComponent<TransformComponent>();
					transformCMP.Scale = { 19.2f,10.8f,1.0f };
					transformCMP.Position = { 0.0f,0.0f,-10.0f };
					transformCMP.Rotation = { 0.0f,glm::radians(180.0f),0.0f };
				}
				
				auto GY_LightEntity = m_ActiveScene->CreateEntity("GY_Light");
				auto& meshCMP = GY_LightEntity.AddComponent<StaticMeshComponent>();
				auto& materialCMP = GY_LightEntity.AddComponent<MaterialComponent>();
				auto meshSrouce = CreateRef<MeshSource>("assets/model/GY/GY.FBX");
				
				meshCMP.StaticMesh = CreateRef<StaticMesh>(meshSrouce);
				meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();
				Ref<Shader> toneShader = Renderer::GetShaderLibrary()->Get("ToneCharactorShader");

				for (auto& [index, materialAsset] : meshCMP.MaterialTable->GetMaterials())
				{
					const auto& textures = materialAsset->GetMaterial()->GetTextures();
					CORE_TRACE("texture count:{}", textures.size());
					Ref<Material> tonematerial = Material::Create(toneShader, materialAsset->GetMaterial()->GetName());
					Ref<MaterialAsset> toneMaterialAsset = CreateRef<MaterialAsset>(tonematerial);
					toneMaterialAsset->GetMaterial()->SetTextures(textures);
					meshCMP.MaterialTable->SetMaterial(index, toneMaterialAsset);


				}
				
				materialCMP.MaterialTable = meshCMP.MaterialTable;

				auto& TransformCMP = GY_LightEntity.GetComponent<TransformComponent>();
				TransformCMP.Position = { 0.0f,-0.6f,-1.0f };
				TransformCMP.Rotation = { glm::radians(-90.0f),0.0f,glm::radians(0.0f) };
				TransformCMP.Scale = { glm::vec3(1.0f) };

				//Temp Function
				//Init Material
				Utils::MaterialUtils::InitMaterial(meshCMP.MaterialTable);
				Utils::MaterialUtils::InitMaterial(materialCMP.MaterialTable);
			}
#endif
#if TestLoadSkinMesh
			{
				
				auto shibahu_Entity = m_ActiveScene->CreateEntity("shibahu");
				auto& meshCMP = shibahu_Entity.AddComponent<StaticMeshComponent>();
				auto& materialCMP = shibahu_Entity.AddComponent<MaterialComponent>();
				auto meshSrouce = CreateRef<MeshSource>("assets/model/shibahu/scene.gltf");
				meshCMP.StaticMesh = CreateRef<StaticMesh>(meshSrouce);
				meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();
				materialCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();

				auto& TransformCMP = shibahu_Entity.GetComponent<TransformComponent>();
				TransformCMP.Position = { 0.0f,-0.5f,-1.0f };
				TransformCMP.Rotation = { glm::radians(0.0f),0.0f,glm::radians(0.0f) };
				TransformCMP.Scale = { glm::vec3(0.01f) };


			}
#endif

#if TestLoadPBRMaterial
			
			
			{

				auto shibahu_Entity = m_ActiveScene->CreateEntity("modern_coffee_table");
				auto& meshCMP = shibahu_Entity.AddComponent<StaticMeshComponent>();
				auto& materialCMP = shibahu_Entity.AddComponent<MaterialComponent>();
				auto meshSrouce = CreateRef<MeshSource>("assets/model/modern_coffee_table/modern_coffee_table_01_4k.gltf");
				meshCMP.StaticMesh = CreateRef<StaticMesh>(meshSrouce);
				meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();
				materialCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();

				auto& TransformCMP = shibahu_Entity.GetComponent<TransformComponent>();
				TransformCMP.Position = { 0.0f,0.0f,-1.0f };
				TransformCMP.Rotation = { 0.0f,glm::radians(90.0f),0.0f };
				
			}

			{

				auto shibahu_Entity = m_ActiveScene->CreateEntity("boombox_4k");
				auto& meshCMP = shibahu_Entity.AddComponent<StaticMeshComponent>();
				auto& materialCMP = shibahu_Entity.AddComponent<MaterialComponent>();
				auto meshSrouce = CreateRef<MeshSource>("assets/model/boombox_4k/boombox_4k.gltf");
				meshCMP.StaticMesh = CreateRef<StaticMesh>(meshSrouce);
				meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();
				materialCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();

				auto& TransformCMP = shibahu_Entity.GetComponent<TransformComponent>();
				TransformCMP.Position = { 0.0f,0.0f,-1.0f };
				TransformCMP.Scale = { 0.5f,0.5f,0.5f };

			}

			
#endif
			// createMesh test
#if TestCrash
			// the performance is sucks
			{
				for (int i = 0; i <1000 ; i++)
				{
					{
						std::string name = fmt::format("Cube_{0}", i);
						auto CubeEntity = m_ActiveScene->CreateEntity(name);
						auto& meshCMP = CubeEntity.AddComponent<StaticMeshComponent>();
						auto& materialCMP = CubeEntity.AddComponent<MaterialComponent>();
						meshCMP.StaticMesh = Kans::MeshFactory::CreatCube(glm::vec3(1.0f));
						meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();
						materialCMP.MaterialTable = meshCMP.MaterialTable;
						auto& TransformCMP = CubeEntity.GetComponent<TransformComponent>();
						TransformCMP.Position = { 0.0f,0.0f,-3.0f };
						TransformCMP.Rotation = { 0.0f,0.0f,0.0f };
						TransformCMP.Scale = { 0.1f,0.1f,0.1f };
						CubeEntity.AddComponent<ScriptComponent>("Sandbox.Player");
					}
				}

			}
#endif		
			// spotCloud test
#if SpotCloudTest
			{
				auto CubeEntity = m_ActiveScene->CreateEntity("Cloud");
				auto& meshCMP = CubeEntity.AddComponent<StaticMeshComponent>();
				auto& materialCMP = CubeEntity.AddComponent<MaterialComponent>();

				meshCMP.StaticMesh = Kans::MeshFactory::CreatCloudSpot(10000, {1,1,1});
				meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterials();
				materialCMP.MaterialTable = meshCMP.MaterialTable;
				auto& TransformCMP = CubeEntity.GetComponent<TransformComponent>();
				TransformCMP.Position = { 0.0f,1.0f,-5.0f };
				TransformCMP.Rotation = { 0.0f,0.0f,0.0f };
				TransformCMP.Scale =    { 1.0f,1.0f,1.0f };

			}
#endif
			//Native Script
#if NativeScript
				
				{
					class CameracontorlScript : public ScriptableEntity
					{
					public:
						void OnCreate() override
						{

						}

						void OnUpdate(TimeStep ts)override
						{

							auto& transformCMP = GetComponent<TransformComponent>();
							if (Input::IsKeyPressed(Key::W))
							{
								transformCMP.Position.y += ts * 5.0f;
							}
							if (Input::IsKeyPressed(Key::S))
							{
								transformCMP.Position.y -= ts * 5.0f;
							}
							if (Input::IsKeyPressed(Key::A))
							{
								transformCMP.Position.x -= ts * 5.0f;

							}
							if (Input::IsKeyPressed(Key::D))
							{
								transformCMP.Position.x += ts * 5.0f;
							}

						}

						void OnDestory() override
						{
						}
					};


					m_CameraEntity.AddComponent<NativeScriptComponent>().Bind<CameracontorlScript>();

					
				}
#endif
			
			//InitPanel
			{
					m_SceneHierachyPanel.setSceneContext(m_ActiveScene);
					
			}
				
			m_ActiveScene->OnRuntimeStart();
		}
		
		//post Init Renderer pipeline
		{
			m_ActiveRenderScene = CreateRef<RenderScene>(m_ActiveScene);
			m_ActiveRenderScene->PrepareEnvironment("assets/textures/HDR/studio_country_hall_2k.hdr");
			//m_ActiveRenderScene->PrepareEnvironment("assets/textures/HDR/studio_country_hall_2k.hdr");
			RenderPipelineSpecification pipline_init_specification;
			pipline_init_specification.render_scene = m_ActiveRenderScene;
			Renderer::InitRenderPipline(pipline_init_specification);

		}
	
		{
		
			//Renderer2D init
			Renderer2D::Init();
			
		}
		//Scene Renderer Init
		{
			
			m_ViewportRenderer = CreateRef<SceneRenderer>(m_ActiveRenderScene.get());
			m_ViewportRenderer->PrepareEnvironment();  

		}
	}
	
	void EditorLayer::OnDetach()
	{
		//temp function to test script system
		{
			m_ActiveScene->OnRuntimeStop();

			SceneSerializer s(m_ActiveScene);
			s.Serialize("assets/scenes/" + m_ActiveScene->GetName() + ".kans");
		}

		PROFILE_FUCTION();
		CLIENT_INFO_TAG("Editor", "editor layer call detach");

		Renderer2D::Shutdown();
		EditorResources::ShutDown();
		
		m_ActiveRenderScene.reset();
		m_ActiveScene.reset();
		
		
	}

	void EditorLayer::OnUpdate(TimeStep ts)
	{
		PROFILE_FUCTION();

		//resize
		{
			if (auto spec = m_ViewportRenderer->GetViewportSize();
				m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
				(spec[0] !=(uint32_t) m_ViewportSize.x || spec[1] != (uint32_t)m_ViewportSize.y))
			{
				m_ViewportRenderer->SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
				m_EditorCamera.setViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
				m_ActiveRenderScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
				m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			}
		}
	
		//update
		{
			if(!m_ViewportFocused)
			m_EditorCamera.onUpdate(ts);

		}

		//updateScene
		{
			m_ActiveScene->OnUpdate(ts);

		}
		//rendering
		// 此处应该有renderpipline和renderpass
		{
			PROFILE_SCOPE("rendering")
			m_ActiveRenderScene->OnRenderEditor( m_ViewportRenderer,ts,m_EditorCamera);
		}
	}

	void EditorLayer::OnEvent(Event& e)
	{
		PROFILE_FUCTION();
		m_EditorCamera.onEvent(e);

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event) { return OnKeyPressedEvent(event); });
	}
	
	void EditorLayer::OnImGuiRender()
	{
		PROFILE_FUCTION();
		
//Imgui docking Init
#if EnbaleDocking
		static bool p_open = true;
		static bool opt_padding = false;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigWindowsResizeFromEdges = io.BackendFlags & ImGuiBackendFlags_HasMouseCursors;
		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		if (!opt_padding)
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("DockSpace Demo", &p_open, window_flags);
		if (!opt_padding)
			ImGui::PopStyleVar();
			ImGui::PopStyleVar(2);

			
		
		
		
		// DockSpace
		//set the min docking windowsSize tO 340
		auto& style = ImGui::GetStyle();
		float minsize = style.WindowMinSize.x;
		style.WindowMinSize.x = 340;
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		}
		style.WindowMinSize.x = minsize;

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Options"))
			{
				// Disabling fullscreen would allow the window to be moved to the front of other windows,
				// which we can't undo at the moment without finer window depth/z control.
				ImGui::MenuItem("Padding", NULL, &opt_padding);
				ImGui::Separator();

				if (ImGui::MenuItem("Flag: NoSplit", "", (dockspace_flags & ImGuiDockNodeFlags_NoSplit) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoSplit; }
				if (ImGui::MenuItem("Flag: NoResize", "", (dockspace_flags & ImGuiDockNodeFlags_NoResize) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoResize; }
				if (ImGui::MenuItem("Flag: NoDockingInCentralNode", "", (dockspace_flags & ImGuiDockNodeFlags_NoDockingInCentralNode) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoDockingInCentralNode; }
				if (ImGui::MenuItem("Flag: AutoHideTabBar", "", (dockspace_flags & ImGuiDockNodeFlags_AutoHideTabBar) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_AutoHideTabBar; }
				ImGui::Separator();
				if (ImGui::MenuItem("Close", NULL, false, &p_open != NULL))
				{
					p_open = false;
					Application::Get().Close();
				}
				ImGui::EndMenu();
			}


			ImGui::EndMenuBar();
		}
		ImGui::End();
#endif
#if ShowImguiDemo
		ImGui::ShowDemoWindow();
#endif
//Editor UI
#if ShowEditorUI
	//ProjectSpecication
	{
		ImGui::Begin("ProjectSpecication");

		//BlockEvent
		m_ViewportFocused = ImGui::IsWindowFocused();
		m_viewprotHovered = ImGui::IsWindowHovered();
		Application::Get().GetImGuiLayer()->BlockEvents(m_ViewportFocused || m_viewprotHovered);
		//renderStats
		ImGui::Text("Render2DStats");
		
		ImGui::Text("DrawCalls: %d", Renderer2D::GetStats().DrawCalls);
		ImGui::Text("QuadCount: %d", Renderer2D::GetStats().QuadCount);
		ImGui::Text("TotalVertexCount: %d", Renderer2D::GetStats().GetTotalVertexCount());
		ImGui::Separator();
		ImGui::Text("Render3DStats");
		ImGui::Text("ViePort Size: %f , %f", m_ViewportSize.x, m_ViewportSize.y);

		
		if (m_ActiveRenderScene!=nullptr)
		{
			auto resource = m_ActiveRenderScene->GetRenderResource();
			ImGui::Separator();
			ImGui::Checkbox("EnableToneShader", &resource.Piplinestate.EnableToneShader);
			ImGui::Checkbox("EnableOutline", &resource.Piplinestate.EnableOutline);
			ImGui::Checkbox("EnableDebugNormal", &resource.Piplinestate.EnableDebugNormal);
			ImGui::Checkbox("EnableStencil", &resource.Piplinestate.EnableStencil);
			ImGui::Checkbox("EnableDefaultShader", &resource.Piplinestate.EnableDefaultShader);
			ImGui::Checkbox("EnableGridShader", &resource.Piplinestate.EnableGridShader);
			m_ActiveRenderScene->SetRenderResource(resource);
		}
		
	
		ImGui::End();

	}
	//HierachyPanel
	m_SceneHierachyPanel.onImGuiRender(true);
	m_ContentBrowserPanel.onImGuiRender(true);
	//Viewport
		//Color FrameBuffer
	if(ShowColorBuffer)
	{
		
		ImGui::Begin("ViewPort1");
		ImVec2 viewportsize = ImGui::GetContentRegionAvail();
		if (m_ViewportSize != *(glm::vec2*) & viewportsize)
		{
				
			m_ViewportSize = { viewportsize.x,viewportsize.y };
			//m_Framebuffer->Resize((uint32_t)viewportsize.x, (uint32_t)viewportsize.y);
			//m_CameraController.OnResize((uint32_t)viewportsize.x, (uint32_t)viewportsize.y);
		}
		uint64_t colorframebufferID = (uint64_t)m_ViewportRenderer->GetOutput(0);
		ImGui::Image((void*)colorframebufferID, viewportsize, ImVec2(0, 1), ImVec2(1, 0));
		//drag drop to scene

		if (ImGui::BeginDragDropTarget())
		{
			auto data = ImGui::AcceptDragDropPayload("asset_payload");
			if (data)
			{
				std::string path = std::string((char*)data->Data, data->DataSize);
				std::filesystem::path filePath = path;
				if (filePath.has_extension())
				{
					if (filePath.extension() == ".fbx"|| filePath.extension() == ".gltf")
					{
						{

							auto Entity = m_ActiveScene->CreateEntity(filePath.filename().generic_string());
							auto& meshCMP = Entity.AddComponent<StaticMeshComponent>();
							auto& materialCMP = Entity.AddComponent<MaterialComponent>();
							auto meshSrouce = CreateRef<MeshSource>(filePath.generic_string());
							meshCMP.StaticMesh = CreateRef<StaticMesh>(meshSrouce);
							meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();
							materialCMP.MaterialTable = meshCMP.StaticMesh->GetMaterialTable();

							auto& TransformCMP = Entity.GetComponent<TransformComponent>();
							TransformCMP.Position = { 0.0f,0.0f,-1.0f };
							TransformCMP.Rotation = { 0.0f,glm::radians(90.0f),0.0f };

						}
					}

					if (filePath.extension() == ".hdr" )
					{
						{
							m_ViewportRenderer->UpdateEnvironment(path);
						}
					}
				}
				
			}
			ImGui::EndDragDropTarget();
		}
		



		//ImGuizmo
		Entity currentEntity = m_SceneHierachyPanel.getSelectEntity();
		if (currentEntity)
		{
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();
			float widowWidth = ImGui::GetWindowWidth();
			float widowHeight = ImGui::GetWindowHeight();
			ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, widowWidth, widowHeight);

			

			glm::mat4 cameraProjection = m_EditorCamera.GetProjectionMatrix();
			glm::mat4 cameraView = m_EditorCamera.getViewMatrix();
			glm::mat4 transform = currentEntity.GetComponent<TransformComponent>().GetTransform();

			ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection), 
				(ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform));

				
			if (ImGuizmo::IsUsing())
			{
				//currentEntity.GetComponent<TransformComponent>().Position = glm::vec3(transform[3]);

				glm::vec3 translation, scale;
				glm::vec3 rotation;
				Math::DecomposeTransform(transform, translation, rotation, scale);
				auto& tc = currentEntity.GetComponent<TransformComponent>();
				switch (m_GizmoType)
				{
				case ImGuizmo::OPERATION::TRANSLATE:
				{
					tc.Position = translation;
				}
					break;
				case ImGuizmo::OPERATION::ROTATE:
				{
						
						
						
					glm::vec3 originalRotationEuler = tc.Rotation;

					// Map original rotation to range [-180, 180] which is what ImGuizmo gives us
					originalRotationEuler.x = fmodf(originalRotationEuler.x + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();
					originalRotationEuler.y = fmodf(originalRotationEuler.y + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();
					originalRotationEuler.z = fmodf(originalRotationEuler.z + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();

					glm::vec3 deltaRotationEuler = rotation - originalRotationEuler;

					// Try to avoid drift due numeric precision
					if (fabs(deltaRotationEuler.x) < 0.001) deltaRotationEuler.x = 0.0f;
					if (fabs(deltaRotationEuler.y) < 0.001) deltaRotationEuler.y = 0.0f;
					if (fabs(deltaRotationEuler.z) < 0.001) deltaRotationEuler.z = 0.0f;

					

					tc.Rotation += deltaRotationEuler;
				}
					break;
				case ImGuizmo::OPERATION::SCALE:
				{
					if (scale != glm::vec3(1.0f, 1.0f, 1.0f))
						tc.Scale = scale;
				}
					break;

				}
			}
		}

		ImGui::End();
	}

	//Depth FrameBuffer
	if(ShowDepthBuffer)
	{
		ImGui::Begin("ViewPort2");
		ImVec2 viewportsize = ImGui::GetContentRegionAvail();

		if (m_ViewportSize != *(glm::vec2*) & viewportsize)
		{
			//HZ_INFO("ViewportSize:  {0}   {1}", viewportsize.x, viewportsize.y);
			m_ViewportSize = { viewportsize.x,viewportsize.y };
			//m_Framebuffer->Resize(viewportsize.x, viewportsize.y);
		}


		uint64_t depthframebufferID =(uint64_t)m_ViewportRenderer->GetOutput(2);
		ImGui::Image((void*)depthframebufferID, viewportsize, ImVec2(0, 1), ImVec2(1, 0));
		ImGui::End();
	}
#endif	
	}

	bool EditorLayer::OnKeyPressedEvent(KeyPressedEvent& e)
	{
		if (Input::IsMouseButtonPressed(MouseButton::Button1))
			return false;

		switch (e.GetKeyCode())
		{
		case KeyCode::Q:
			m_GizmoType = ImGuizmo::OPERATION::BOUNDS;
			break;
		case KeyCode::W:
			m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
			break;
		case KeyCode::E:
			m_GizmoType = ImGuizmo::OPERATION::ROTATE;
			break;
		case KeyCode::R:
			m_GizmoType = ImGuizmo::OPERATION::SCALE;
			break;
		}

		return false;
	}

}


