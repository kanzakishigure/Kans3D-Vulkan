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
#define NativeScript false

namespace Kans
{

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}
    void EditorLayer::OnAttach()
    {
        PROFILE_FUCTION();
        // Before anything logs, so the console shows the whole session.
        m_Console.Attach();
        RegisterPanels();
        m_Panels.InstallSettingsHandler();
        m_ContentBrowserPanel.InstallSettingsHandler();

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
                m_HierarchyPanel.SetScene(m_ActiveScene);
            }

            m_History.Reset(*m_ActiveScene);
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
            m_ViewportPanel.SetContext(m_ActiveScene, m_ActiveRenderScene, m_ViewportRenderer);
            m_ViewportPanel.SetFileDropCallback([this](const std::filesystem::path& file, std::optional<glm::vec3> position) {
                if (file.extension() == ".kans")
                    Record(OpenScene(file));
                else if (file.has_extension())
                    PlaceAsset(file, {}, position);
            });
            m_ViewportPanel.SetModelResolver([this](const std::filesystem::path& file) {
                const auto source = file.has_extension() ? FindModelSource(file) : std::nullopt;
                return source ? FindPublishedModel(*source) : AssetID {};
            });
            auto configured = RefreshProductAssets();
            Record(configured);
        }
    }

    void EditorLayer::OnDetach()
    {
        m_Panels.UninstallSettingsHandler();
        m_ContentBrowserPanel.UninstallSettingsHandler();
        m_ContentBrowserPanel.Bind(nullptr);
        m_ContentBrowserPanel.SetOpenFileCallback({});
        m_ContentBrowserPanel.SetCreateMaterialCallback({});
        m_ContentBrowserPanel.SetImportCallback({});
        m_ContentBrowserPanel.SetImportSettingsCallback({});
        m_ContentBrowserPanel.SetErrorCallback({});
        m_ProductImportPanel.Bind(nullptr);
        m_MaterialEditorPanel.Bind(nullptr);
        if (m_AssetImports)
        {
            auto stopped = m_AssetImports->Shutdown();
            if (const auto* error = std::get_if<AssetError>(&stopped))
                CORE_ERROR("Project import shutdown: {}", error->Message);
        }
        if (m_ActiveScene)
            m_Play.Discard(*m_ActiveScene);
        if (m_ViewportRenderer)
        {
            m_ViewportPanel.SetContext({}, {}, {});
            m_ViewportRenderer->SetAssetManager(nullptr);
            m_PropertiesPanel.SetAssetManager(nullptr);
            m_MaterialEditorPanel.SetAssetManager(nullptr);
            m_ViewportRenderer.reset();
        }
        m_Assets.reset();
        m_HierarchyPanel.SetScene({});

        PROFILE_FUCTION();
        CLIENT_INFO_TAG("Editor", "editor layer call detach");

        Renderer2D::Shutdown();
        EditorResources::ShutDown();

        m_ActiveRenderScene.reset();
        m_ActiveScene.reset();
        m_AssetImports.reset();
        m_Console.Detach();
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
                Record(*error);
            else if (const auto& snapshot = std::get<std::optional<Ref<const AssetRegistry>>>(changed); snapshot)
                Record(RefreshProductAssets(*snapshot));
            auto completed = m_AssetImports->Poll();
            if (const auto* error = std::get_if<AssetError>(&completed))
                Record(*error);
            else if (std::get<bool>(completed))
            {
                // Dependencies can be published even when the root build fails.
                Record(RefreshProductAssets());
                PlacePendingModel();
                if (!m_Play.IsActive())
                    m_History.Record(*m_ActiveScene);
            }
        }

        // The scene only runs in play mode.
        m_Play.Update(*m_ActiveScene, ts);
        // The viewport panel renders the scene during the UI frame.
    }

    void EditorLayer::OnEvent(Event& e)
    {
        // Editor shortcuts are handled in the ImGui frame (HandleShortcuts).
        (void)e;
    }

} // namespace Kans
