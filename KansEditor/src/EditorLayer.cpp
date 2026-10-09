#include "EditorLayer.h"
#include <imgui.h>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <Kans3D/Platform/OpenGL/OpenGLShader.h>

#include "ImGuizmo.h"
// TestInclude
#include "Kans3D/Core/Log/Log.h"
#include <Kans3D/Core/Hash.h>
#include <Kans3D/Core/UUID.h>
#include <Kans3D/ImGui/Colors.h>
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ShowImguiDemo false
#define ShowEditorUI true
#define EnbaleDocking true

#define NativeScript false
#define ShowDepthBuffer false
#define ShowColorBuffer true
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Kans
{

    EditorLayer::EditorLayer() : m_EditorCamera(45.0f, 1920, 1080, 0.1, 1000), Layer("EditorLayer") {}
    void EditorLayer::OnAttach()
    {
        PROFILE_FUCTION();

        OpenProject();

        // Resource Init
        EditorResources::Init();

        // scene init
        {

            // Create Scene
            {
                m_ActiveScene = CreateRef<Scene>("TestDemo");
                m_ActiveScene->OnViewportResize((uint32_t)Application::Get().GetWindow().GetWidth(),
                                                (uint32_t)Application::Get().GetWindow().GetHeight());
                auto pointlight = m_ActiveScene->CreateEntity("PointLight");

                auto& plightCMP              = pointlight.AddComponent<PointLightComponent>();
                plightCMP.Diffuse_Intensity  = glm::vec3(1.0);
                plightCMP.Specular_Intensity = glm::vec3(1.0);
                plightCMP.Ambient_Intensity  = glm::vec3(1.0);
                auto& CMP                    = pointlight.GetComponent<TransformComponent>();
                CMP.Position                 = {20.0, 20.0, 1.0};

                auto  DirLight            = m_ActiveScene->CreateEntity("DirLight");
                auto& dirCMP              = DirLight.AddComponent<DirLightComponent>();
                dirCMP.Diffuse_Intensity  = glm::vec3(1.0);
                dirCMP.Specular_Intensity = glm::vec3(1.0);
                dirCMP.Ambient_Intensity  = glm::vec3(1.0);
            }

            // Create Scene camera
            {
                m_CameraEntity        = m_ActiveScene->CreateEntity("mainCamera");
                auto& transfromCMP    = m_CameraEntity.GetComponent<TransformComponent>();
                transfromCMP.Position = {0.0f, 0.7f, 1.5f};
                auto& cmp             = m_CameraEntity.AddComponent<CameraComponent>();
                cmp.SceneCamera.SetViewportSize(1920, 1080);
            }
            // Native Script
#if NativeScript

            {
                class CameracontorlScript : public ScriptableEntity
                {
                public:
                    void OnCreate() override {}

                    void OnUpdate(TimeStep ts) override
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

                    void OnDestory() override {}
                };

                m_CameraEntity.AddComponent<NativeScriptComponent>().Bind<CameracontorlScript>();
            }
#endif

            // InitPanel
            {
                m_SceneHierachyPanel.setSceneContext(m_ActiveScene);
            }

            m_ActiveScene->OnRuntimeStart();
        }

        // post Init Renderer pipeline
        {
            m_ActiveRenderScene = CreateRef<RenderScene>(m_ActiveScene);
            RenderPipelineSpecification pipline_init_specification;
            pipline_init_specification.render_scene = m_ActiveRenderScene;
            Renderer::InitRenderPipline(pipline_init_specification);
        }

        {

            // Renderer2D init
            Renderer2D::Init();
        }
        // Scene Renderer Init
        {

            m_ViewportRenderer = CreateRef<SceneRenderer>(m_ActiveRenderScene.get());
            m_ActiveRenderScene->PrepareEnvironment("assets/textures/HDR/studio_country_hall_2k.hdr");
            m_ViewportRenderer->PrepareEnvironment();
            auto configured = RefreshProductAssets();
            if (const auto* error = std::get_if<AssetError>(&configured))
            {
                m_SceneFileError = error->Message;
                CORE_ERROR("Scene Product configuration: {}", error->Message);
            }
        }
    }

    void EditorLayer::OnDetach()
    {
        m_ContentBrowserPanel.SetOpenFileCallback({});
        m_ProductImportPanel.Bind(nullptr);
        if (m_AssetImports)
        {
            auto stopped = m_AssetImports->Shutdown();
            if (const auto* error = std::get_if<AssetError>(&stopped))
                CORE_ERROR("Project import shutdown: {}", error->Message);
        }
        if (m_ActiveScene)
            m_ActiveScene->OnRuntimeStop();
        if (m_ViewportRenderer)
        {
            m_ViewportRenderer->SetAssetManager(nullptr);
            m_SceneHierachyPanel.setAssetManager(nullptr);
            m_ViewportRenderer.reset();
        }
        m_Assets.reset();
        m_SceneHierachyPanel.setSceneContext({});

        PROFILE_FUCTION();
        CLIENT_INFO_TAG("Editor", "editor layer call detach");

        Renderer2D::Shutdown();
        EditorResources::ShutDown();

        m_ActiveRenderScene.reset();
        m_ActiveScene.reset();
        m_AssetImports.reset();
    }

    void EditorLayer::OnUpdate(TimeStep ts)
    {
        PROFILE_FUCTION();
        if (m_AssetImports)
        {
            // Consume source operations before Poll: a just-completed build may
            // publish a newer catalog than this retained immutable notification.
            auto changed = m_AssetImports->ConsumeRuntimeRegistryChange();
            if (const auto* error = std::get_if<AssetError>(&changed))
                m_SceneFileError = error->Message;
            else if (const auto& snapshot = std::get<std::optional<Ref<const AssetRegistry>>>(changed); snapshot)
            {
                auto refreshed = RefreshProductAssets(*snapshot);
                if (const auto* error = std::get_if<AssetError>(&refreshed))
                    m_SceneFileError = error->Message;
            }
            auto completed = m_AssetImports->Poll();
            if (const auto* error = std::get_if<AssetError>(&completed))
                m_SceneFileError = error->Message;
            else if (std::get<bool>(completed))
            {
                // Dependencies can be published even when the root build fails.
                auto refreshed = RefreshProductAssets();
                if (const auto* error = std::get_if<AssetError>(&refreshed))
                    m_SceneFileError = error->Message;
                PlacePendingModel();
            }
        }

        // resize
        {
            if (auto spec = m_ViewportRenderer->GetViewportSize();
                m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
                (spec[0] != (uint32_t)m_ViewportSize.x || spec[1] != (uint32_t)m_ViewportSize.y))
            {
                m_ViewportRenderer->SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                m_EditorCamera.setViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                m_ActiveRenderScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            }
        }

        // update
        {
            const bool rightPressed = Input::IsMouseButtonPressed(MouseButton::ButtonRight);
            m_CameraNavigating      = m_ViewportFocused && rightPressed && (m_ViewportHovered || m_CameraNavigating);
            if (m_CameraNavigating)
                m_EditorCamera.onUpdate(ts);
            else
                m_EditorCamera.resetMotion();
        }

        // updateScene
        {
            m_ActiveScene->OnUpdate(ts);
        }
        // rendering
        {
            PROFILE_SCOPE("rendering")
            m_ActiveRenderScene->OnRenderEditor(m_ViewportRenderer, ts, m_EditorCamera);
        }
    }

    void EditorLayer::OnEvent(Event& e)
    {
        PROFILE_FUCTION();
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event) { return OnKeyPressedEvent(event); });
    }

    void EditorLayer::OnImGuiRender()
    {
        PROFILE_FUCTION();
// Imgui docking Init
#if EnbaleDocking
        static bool               p_open          = true;
        static bool               opt_padding     = false;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        ImGuiIO&                  io              = ImGui::GetIO();
        io.ConfigWindowsResizeFromEdges           = io.BackendFlags & ImGuiBackendFlags_HasMouseCursors;
        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window
        // not dockable into, because it would be confusing to have two docking
        // targets within each others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render
        // our background and handle the pass-thru hole, so we ask Begin() to not
        // render a background.
        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        // Important: note that we proceed even if Begin() returns false (aka window
        // is collapsed). This is because we want to keep our DockSpace() active. If a
        // DockSpace() is inactive, all active windows docked into it will lose their
        // parent and become undocked. We cannot preserve the docking relationship
        // between an active window and an inactive docking, otherwise any change of
        // dockspace/settings would lead to windows being stuck in limbo and never
        // being visible.
        if (!opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace Demo", &p_open, window_flags);
        if (!opt_padding)
            ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);

        // DockSpace
        // set the min docking windowsSize to 20% of window width
        auto& style           = ImGui::GetStyle();
        float minsize         = style.WindowMinSize.x;
        float minPanelWidth   = (float)Application::Get().GetWindow().GetWidth() * 0.2f;
        style.WindowMinSize.x = minPanelWidth;
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }
        style.WindowMinSize.x = minsize;

        if (ImGui::BeginMenuBar())
        {
            DrawSceneFileMenu();
            if (ImGui::BeginMenu("Options"))
            {
                // Disabling fullscreen would allow the window to be moved to the front of
                // other windows, which we can't undo at the moment without finer window
                // depth/z control.
                ImGui::MenuItem("Padding", NULL, &opt_padding);
                ImGui::Separator();

                if (ImGui::MenuItem("Flag: NoSplit", "", (dockspace_flags & ImGuiDockNodeFlags_NoSplit) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_NoSplit;
                }
                if (ImGui::MenuItem("Flag: NoResize", "", (dockspace_flags & ImGuiDockNodeFlags_NoResize) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_NoResize;
                }
                if (ImGui::MenuItem("Flag: NoDockingInCentralNode",
                                    "",
                                    (dockspace_flags & ImGuiDockNodeFlags_NoDockingInCentralNode) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_NoDockingInCentralNode;
                }
                if (ImGui::MenuItem(
                        "Flag: AutoHideTabBar", "", (dockspace_flags & ImGuiDockNodeFlags_AutoHideTabBar) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_AutoHideTabBar;
                }
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
// Editor UI
#if ShowEditorUI
        // ProjectSpecication
        {
            ImGui::Begin("ProjectSpecication");

            // renderStats
            ImGui::Text("Render2DStats");

            ImGui::Text("DrawCalls: %d", Renderer2D::GetStats().DrawCalls);
            ImGui::Text("QuadCount: %d", Renderer2D::GetStats().QuadCount);
            ImGui::Text("TotalVertexCount: %d", Renderer2D::GetStats().GetTotalVertexCount());
            ImGui::Separator();
            ImGui::Text("Render3DStats");
            ImGui::Text("ViePort Size: %f , %f", m_ViewportSize.x, m_ViewportSize.y);

            if (m_ActiveRenderScene != nullptr)
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
        // HierachyPanel
        m_SceneHierachyPanel.onImGuiRender(true);
        m_ContentBrowserPanel.onImGuiRender(true);
        m_ProductImportPanel.Draw();
        // Viewport
        // Color FrameBuffer
        if (ShowColorBuffer)
        {

            ImGui::Begin("ViewPort1");
            m_ViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            m_ViewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
            Application::Get().GetImGuiLayer()->BlockEvents(m_ViewportFocused || m_ViewportHovered);
            // Use the wheel and hover state from the same ImGui frame.
            if (m_ViewportHovered && ImGui::GetIO().MouseWheel != 0.0f)
            {
                MouseScrolledEvent scrollEvent(ImGui::GetIO().MouseWheelH, ImGui::GetIO().MouseWheel);
                m_EditorCamera.onEvent(scrollEvent);
            }
            ImVec2 viewportsize = ImGui::GetContentRegionAvail();
            if (m_ViewportSize != *(glm::vec2*)&viewportsize)
            {

                m_ViewportSize = {viewportsize.x, viewportsize.y};
                // m_Framebuffer->Resize((uint32_t)viewportsize.x,
                // (uint32_t)viewportsize.y);
                // m_CameraController.OnResize((uint32_t)viewportsize.x,
                // (uint32_t)viewportsize.y);
            }
            uint64_t colorframebufferID = (uint64_t)m_ViewportRenderer->GetOutput(0);
            ImGui::Image((void*)colorframebufferID, viewportsize, ImVec2(0, 1), ImVec2(1, 0));
            // drag drop to scene

            if (ImGui::BeginDragDropTarget())
            {
                auto data = ImGui::AcceptDragDropPayload("asset_payload");
                if (data)
                {
                    const std::filesystem::path file(std::string((char*)data->Data, data->DataSize));
                    if (file.extension() == ".kans")
                    {
                        auto opened = OpenScene(file);
                        if (const auto* error = std::get_if<AssetError>(&opened))
                            m_SceneFileError = error->Message;
                    }
                    else if (file.has_extension())
                        PlaceAsset(file);
                }
                ImGui::EndDragDropTarget();
            }

            // ImGuizmo
            Entity currentEntity = m_SceneHierachyPanel.getSelectEntity();
            if (currentEntity)
            {
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist();
                float widowWidth  = ImGui::GetWindowWidth();
                float widowHeight = ImGui::GetWindowHeight();
                ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, widowWidth, widowHeight);

                glm::mat4 cameraProjection = m_EditorCamera.GetProjectionMatrix();
                glm::mat4 cameraView       = m_EditorCamera.getViewMatrix();
                glm::mat4 transform        = currentEntity.GetComponent<TransformComponent>().GetTransform();

                ImGuizmo::Manipulate(glm::value_ptr(cameraView),
                                     glm::value_ptr(cameraProjection),
                                     (ImGuizmo::OPERATION)m_GizmoType,
                                     ImGuizmo::LOCAL,
                                     glm::value_ptr(transform));

                if (ImGuizmo::IsUsing())
                {
                    // currentEntity.GetComponent<TransformComponent>().Position =
                    // glm::vec3(transform[3]);

                    glm::vec3 translation, scale;
                    glm::vec3 rotation;
                    Math::DecomposeTransform(transform, translation, rotation, scale);
                    auto& tc = currentEntity.GetComponent<TransformComponent>();
                    switch (m_GizmoType)
                    {
                        case ImGuizmo::OPERATION::TRANSLATE: {
                            tc.Position = translation;
                        }
                        break;
                        case ImGuizmo::OPERATION::ROTATE: {

                            glm::vec3 originalRotationEuler = tc.Rotation;

                            // Map original rotation to range [-180, 180] which is what ImGuizmo
                            // gives us
                            originalRotationEuler.x =
                                fmodf(originalRotationEuler.x + glm::pi<float>(), glm::two_pi<float>()) -
                                glm::pi<float>();
                            originalRotationEuler.y =
                                fmodf(originalRotationEuler.y + glm::pi<float>(), glm::two_pi<float>()) -
                                glm::pi<float>();
                            originalRotationEuler.z =
                                fmodf(originalRotationEuler.z + glm::pi<float>(), glm::two_pi<float>()) -
                                glm::pi<float>();

                            glm::vec3 deltaRotationEuler = rotation - originalRotationEuler;

                            // Try to avoid drift due numeric precision
                            if (fabs(deltaRotationEuler.x) < 0.001)
                                deltaRotationEuler.x = 0.0f;
                            if (fabs(deltaRotationEuler.y) < 0.001)
                                deltaRotationEuler.y = 0.0f;
                            if (fabs(deltaRotationEuler.z) < 0.001)
                                deltaRotationEuler.z = 0.0f;

                            tc.Rotation += deltaRotationEuler;
                        }
                        break;
                        case ImGuizmo::OPERATION::SCALE: {
                            if (scale != glm::vec3(1.0f, 1.0f, 1.0f))
                                tc.Scale = scale;
                        }
                        break;
                    }
                }
            }

            ImGui::End();
        }
        else
        {
            m_ViewportFocused = false;
            m_ViewportHovered = false;
            Application::Get().GetImGuiLayer()->BlockEvents(false);
        }

        // Depth FrameBuffer
        if (ShowDepthBuffer)
        {
            ImGui::Begin("ViewPort2");
            ImVec2 viewportsize = ImGui::GetContentRegionAvail();

            if (m_ViewportSize != *(glm::vec2*)&viewportsize)
            {
                // CLIENT_INFO("ViewportSize: {0} {1}", viewportsize.x, viewportsize.y);
                m_ViewportSize = {viewportsize.x, viewportsize.y};
                // m_Framebuffer->Resize(viewportsize.x, viewportsize.y);
            }

            uint64_t depthframebufferID = (uint64_t)m_ViewportRenderer->GetOutput(2);
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

} // namespace Kans
