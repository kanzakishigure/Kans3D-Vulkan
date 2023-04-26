#include "EditorLayer.h"
#include <imgui.h>
#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


//TestInclude
#include <Kans3D/Core/UUID.h>
#include <Kans3D/ImGui/Colors.h>
#include <Kans3D/Renderer/MeshFactory.h>
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ShowImguiDemo		true
#define ShowEditorUI		true
#define EnbaleDocking		true
#define TestLoadModel		true	
#define CrashTest			false
#define SpotCloudTest		false
#define NativeScript		false
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Kans
{


	EditorLayer::EditorLayer()
		:m_CameraController(1920.0f / 1080.0f), Layer("EditorLayer")
	{	

	}
	void EditorLayer::OnAttach()
	{
		HZ_PROFILE_FUCTION();


		//Resource Init
		EditorResources::Init();

		//Renderer2D init
		Renderer2D::Init();
		//FrameBuffer init
		{
			
			FrameBufferSpecification spec;
			spec.Width = Application::Get().GetWindow().GetWidth();
			spec.Height = Application::Get().GetWindow().GetHeight();
			//Color                           normal													Depth
			spec.AttachmentSpecification = { FrameBufferTextureFormat::RGBA8 ,FrameBufferTextureFormat::RGBA8,FrameBufferTextureFormat::Depth };
			m_Framebuffer = FrameBuffer::Create(spec);

		}
		
		// scene init
		{

			
			//Create Scene
			{
				m_ActiveScene = CreateRef<Scene>("TestDemo");
			
				auto pointlight = m_ActiveScene->CreateEntity("PointLight");

				auto& plightCMP = pointlight.AddComponent<PointLightComponent>();
				plightCMP.Diffuse_Intensity = glm::vec3(1.0);
				plightCMP.Specular_Intensity = glm::vec3(1.0);
				plightCMP.Ambient_Intensity = glm::vec3(1.0);
				auto& CMP = pointlight.GetComponent<TransformComponent>();
				CMP.Position = { 10.0,20.0,3.3 };

				auto DirLight = m_ActiveScene->CreateEntity("DirLight");
				auto& dirCMP = DirLight.AddComponent<DirLightComponent>();
				dirCMP.Diffuse_Intensity = glm::vec3(1.0);
				dirCMP.Specular_Intensity = glm::vec3(1.0);
				dirCMP.Ambient_Intensity = glm::vec3(1.0);
			}
			//ref BackScene
			{
				auto RefEntity = m_ActiveScene->CreateEntity("RefEntity");
				auto& spritCMP = RefEntity.AddComponent<SpriteRendererComponent>();
				spritCMP.Texture = Kans::Texture2D::Create("assets/textures/GY.png");
				auto& transformCMP = RefEntity.GetComponent<TransformComponent>();
				transformCMP.Scale = { 19.2f,10.8f,1.0f };
				transformCMP.Position = { 2.3f,0.0f,-12.0f };
			}

			
			//Create Scene camera
			{
				m_CameraEntity = m_ActiveScene->CreateEntity("mainCamera");
				auto& transfromCMP = m_CameraEntity.GetComponent<TransformComponent>();
				transfromCMP.Position = { 0.0f,0.7f,1.5f };
				auto& cmp = m_CameraEntity.AddComponent<CameraComponent>();
				cmp.SceneCamera.SetViewportSize(1920, 1080);
			}

			// load Mesh test
#if TestLoadModel
			{
				auto GY_LightEntity = m_ActiveScene->CreateEntity("GY_Light");
				auto& meshCMP = GY_LightEntity.AddComponent<StaticMeshComponent>();
				auto& materialCMP = GY_LightEntity.AddComponent<MaterialComponent>();
				auto meshSrouce = CreateRef<MeshSource>("assets/model/GY_Light/GY_Light.fbx");
				meshCMP.StaticMesh = CreateRef<StaticMesh>(meshSrouce);
				meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterials();
				materialCMP.MaterialTable = meshCMP.MaterialTable;
				auto& TransformCMP = GY_LightEntity.GetComponent<TransformComponent>();
				TransformCMP.Position = { 0.0f,-1.2f,-3.0f };
				TransformCMP.Rotation = { glm::radians(-20.0f),0.0f,glm::radians(0.0f) };
				TransformCMP.Scale = { glm::vec3(0.15f) };

				//Temp Function
				//Init Material
				Utils::MaterialUtils::InitMaterial(materialCMP.MaterialTable);
			}
#endif
			// createMesh test
#if CrashTest
			// the performance is sucks
			{
				for (int i = 0; i <1 ; i++)
				{
					{
						std::string name = fmt::format("Cube_{0}", i);
						auto CubeEntity = m_ActiveScene->CreateEntity(name);
						auto& meshCMP = CubeEntity.AddComponent<StaticMeshComponent>();
						auto& materialCMP = CubeEntity.AddComponent<MaterialComponent>();
						meshCMP.StaticMesh = Kans::MeshFactory::CreatCube(glm::vec3(1.0f));
						meshCMP.MaterialTable = meshCMP.StaticMesh->GetMaterials();
						materialCMP.MaterialTable = meshCMP.MaterialTable;
						auto& TransformCMP = CubeEntity.GetComponent<TransformComponent>();
						TransformCMP.Position = { 0.0f,0.0f,-3.0f };
						TransformCMP.Rotation = { 0.0f,0.0f,0.0f };
						TransformCMP.Scale = { 0.5f,0.5f,0.5f };
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
					m_SceneHierachyPanel.SetSceneContext(m_ActiveScene);
					
			}
				
			m_ActiveScene->OnRuntimeStart();
		}
		//Scene Renderer Init
		{
			m_StaticMeshRenderer = CreateRef<SceneRenderer>(m_ActiveScene);
			m_StaticMeshRenderer->SetFrameBuffer(m_Framebuffer);
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

		HZ_PROFILE_FUCTION();
		CLIENT_INFO("Editor", "editor layer call detach");

		Renderer2D::Shutdown();
		EditorResources::ShutDown();

		
	}

	void EditorLayer::OnUpdate(TimeStep ts)
	{
		HZ_PROFILE_FUCTION();

		//resize
		{
			if (FrameBufferSpecification spec = m_Framebuffer->GetSpecification();
				m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
				(spec.Width !=(uint32_t) m_ViewportSize.x || spec.Height != (uint32_t)m_ViewportSize.y))
			{
				m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
				m_CameraController.OnResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
				m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			}
		}
	
		//update
		{
			if(!m_ViewportFocused)
			m_CameraController.OnUpdate(ts);

		}

		//updateScene
		{
			m_ActiveScene->OnUpdate(ts);

		}
		//renderer
		Renderer2D::ResetStats();
		m_Framebuffer->Bind();
		OpenGLRenderCommand ::SetClearColor({ 0.02f, 0.02f, 0.02f, 1.0f });
		OpenGLRenderCommand::Clear();
	
		//rendering
		//此处应该有renderpass
		{
			HZ_PROFILE_SCOPE("rendering")
			m_ActiveScene->OnRenderer(m_StaticMeshRenderer, ts);
		}
		m_Framebuffer->Unbind();

	}

	void EditorLayer::OnEvent(Event& e)
	{
		HZ_PROFILE_FUCTION();
		m_CameraController.OnEvent(e);
	}
	
	void EditorLayer::OnImGuiRender()
	{
		HZ_PROFILE_FUCTION();
		
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

		auto resource = m_ActiveScene->GetRenderResource();
		ImGui::Separator();
		ImGui::Checkbox("EnableToneShader", &resource.Piplinestate.EnableToneShader);
		ImGui::Checkbox("EnableOutline", &resource.Piplinestate.EnableOutline);
		ImGui::Checkbox("EnableDebugNormal", &resource.Piplinestate.EnableDebugNormal);
		ImGui::Checkbox("EnableStencil", &resource.Piplinestate.EnableStencil);
		ImGui::Checkbox("EnableDefaultShader", &resource.Piplinestate.EnableDefaultShader);
		m_ActiveScene->SetRenderResource(resource);
		
		

		ImGui::End();

	}
	//HierachyPanel
	m_SceneHierachyPanel.OnImguiRender(true);
	m_ContentBrowserPanel.OnImguiRender(true);
	//Viewport
		//Color FrameBuffer
		if (1)
		{
			ImGui::Begin("ViewPort1");
			ImVec2 viewportsize = ImGui::GetContentRegionAvail();
			if (m_ViewportSize != *(glm::vec2*) & viewportsize)
			{
				
				m_ViewportSize = { viewportsize.x,viewportsize.y };
				//m_Framebuffer->Resize((uint32_t)viewportsize.x, (uint32_t)viewportsize.y);
				//m_CameraController.OnResize((uint32_t)viewportsize.x, (uint32_t)viewportsize.y);
			}
			uint64_t colorframebufferID = (uint64_t)m_Framebuffer->GetColorAttachmentRendererID();
			ImGui::Image((void*)colorframebufferID, viewportsize, ImVec2(0, 1), ImVec2(1, 0));

			ImGui::End();
		}

		//Depth FrameBuffer
		if(0)
		{
			ImGui::Begin("ViewPort2");
			ImVec2 viewportsize = ImGui::GetContentRegionAvail();

			if (m_ViewportSize != *(glm::vec2*) & viewportsize)
			{
				//HZ_INFO("ViewportSize:  {0}   {1}", viewportsize.x, viewportsize.y);
				m_ViewportSize = { viewportsize.x,viewportsize.y };
				//m_Framebuffer->Resize(viewportsize.x, viewportsize.y);
			}


			uint64_t depthframebufferID =(uint64_t)m_Framebuffer->GetColorAttachmentRendererID(1);
			ImGui::Image((void*)depthframebufferID, viewportsize, ImVec2(0, 1), ImVec2(1, 0));

			ImGui::End();
		}
#endif	

		
	}

}


