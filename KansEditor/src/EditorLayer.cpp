#include "EditorLayer.h"
#include <imgui.h>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ImGuizmo.h"
// TestInclude
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Renderer/Features/Editor/EditorRenderFeatures.h"
#include <Kans3D/Core/Hash.h>
#include <Kans3D/Core/UUID.h>
#include <Kans3D/ImGui/Colors.h>

namespace Kans
{
    namespace
    {
        // Relative to the project's asset folder.
        constexpr const char* StartupScene = "scenes/Showcase.kans";
        constexpr const char* DefaultEnvironment = "assets/textures/HDR/studio_country_hall_2k.hdr";
    } // namespace


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
        LoadProjectScripts();

        // Resource Init
        EditorResources::Init();

        // scene init
        {

            // Create Scene
            {
                m_ActiveScene = CreateRef<Scene>("Untitled");
                m_ActiveScene->OnViewportResize((uint32_t)Application::Get().GetWindow().GetWidth(),
                                                (uint32_t)Application::Get().GetWindow().GetHeight());
                auto pointlight = m_ActiveScene->CreateEntity("PointLight");

                auto& plightCMP              = pointlight.AddComponent<PointLightComponent>();
                plightCMP.Diffuse_Intensity  = glm::vec3(1.0);
                plightCMP.Specular_Intensity = glm::vec3(1.0);
                plightCMP.Ambient_Intensity  = glm::vec3(1.0);
                pointlight.PatchComponent<TransformComponent>([](auto& t) { t.Position = {20.0f, 20.0f, 1.0f}; });

                auto  DirLight            = m_ActiveScene->CreateEntity("DirLight");
                auto& dirCMP              = DirLight.AddComponent<DirLightComponent>();
                dirCMP.Diffuse_Intensity  = glm::vec3(1.0);
                dirCMP.Specular_Intensity = glm::vec3(1.0);
                dirCMP.Ambient_Intensity  = glm::vec3(1.0);
            }

            // Create Scene camera
            {
                m_CameraEntity        = m_ActiveScene->CreateEntity("mainCamera");
                m_CameraEntity.PatchComponent<TransformComponent>([](auto& t) { t.Position = {0.0f, 0.7f, 1.5f}; });
                auto& cmp             = m_CameraEntity.AddComponent<CameraComponent>();
                cmp.SceneCamera.SetViewportSize(1920, 1080);
            }

            // InitPanel
            {
                m_HierarchyPanel.SetScene(m_ActiveScene);
            }

            m_History.Reset(*m_ActiveScene);
        }

        // Viewport rendering
        {
            m_Viewport = CreateScope<ViewportClient>();
            m_Viewport->SetScene(m_ActiveScene);
            // Scenes without environment settings show the editor's default map; a scene's own
            // map is relative to the project directory.
            m_ActiveScene->GetRenderWorld().EnvironmentPath = DefaultEnvironment;
            m_ActiveScene->GetRenderWorld().EnvironmentRoot = std::filesystem::current_path();
            m_PropertiesPanel.SetDefaultEnvironment(DefaultEnvironment);
            m_Viewport->PrepareEnvironment();
            // Material previews share the viewport's renderer and environment; their
            // asset cache is bound when the project's assets are (RefreshProductAssets).
            m_MaterialPreview   = CreateScope<MaterialPreviewRenderer>(m_Viewport->GetRenderer(), nullptr,
                                                                     m_Viewport->GetEnvironment());
            m_ThumbnailRenderer = CreateScope<MaterialPreviewRenderer>(m_Viewport->GetRenderer(), nullptr,
                                                                       m_Viewport->GetEnvironment());
            m_MaterialEditorPanel.SetPreviewRenderer(m_MaterialPreview.get());
            m_ContentBrowserPanel.SetMaterialThumbnailProvider(
                [this](const std::filesystem::path& file, uint32_t size) { return MaterialThumbnail(file, size); });
            m_ViewportPanel.SetContext(m_ActiveScene, m_Viewport.get());
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
            // Until projects have their own settings, the project's showcase scene opens
            // when it exists; otherwise the scene built above stays.
            const auto      startup = KansFileSystem::GetAssetFolder() / StartupScene;
            std::error_code missing;
            if (std::holds_alternative<std::monostate>(configured) && std::filesystem::exists(startup, missing))
                Record(OpenScene(startup));
            QueueUnimportedShaders();
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
        m_ContentBrowserPanel.SetExtractMaterialsCallback({});
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
        if (m_Viewport)
        {
            m_ViewportPanel.SetContext({}, nullptr);
            m_MaterialPreview->SetResources({}, {});
            m_ThumbnailRenderer->SetResources({}, {});
            m_Viewport->SetAssetManager(nullptr);
            m_PropertiesPanel.SetAssetManager(nullptr);
            m_MaterialEditorPanel.SetAssetManager(nullptr);
            m_MaterialEditorPanel.SetPreviewRenderer(nullptr);
            m_ContentBrowserPanel.SetMaterialThumbnailProvider({});
            m_MaterialThumbnails.clear();
            m_MaterialPreview.reset();
            m_ThumbnailRenderer.reset();
            m_Viewport.reset();
        }
        m_Assets.reset();
        m_HierarchyPanel.SetScene({});

        PROFILE_FUCTION();
        CLIENT_INFO_TAG("Editor", "editor layer call detach");

        EditorResources::ShutDown();

        m_ActiveScene.reset();
        m_AssetImports.reset();
        m_Console.Detach();
    }

    void EditorLayer::OnUpdate(TimeStep ts)
    {
        PROFILE_FUCTION();
        PollScriptBuild();
        if (m_AssetImports)
        {
            // Consume source operations before Poll: a just-completed build may
            // publish a newer catalog than this retained immutable notification.
            auto changed = m_AssetImports->ConsumeRuntimeRegistryChange();
            if (const auto* error = std::get_if<AssetError>(&changed))
                Record(*error);
            else if (const auto& snapshot = std::get<std::optional<Ref<const AssetRegistry>>>(changed); snapshot)
            {
                Record(RefreshProductAssets(*snapshot));
                QueueUnimportedShaders(); // Sources were refreshed: new shader files are imported.
            }
            auto completed = m_AssetImports->Poll();
            if (const auto* error = std::get_if<AssetError>(&completed))
                Record(*error);
            else if (std::get<bool>(completed))
            {
                // Dependencies can be published even when the root build fails.
                Record(RefreshProductAssets());
                if (m_PendingPlacement && m_PendingPlacementWaiting)
                    StartPendingPlacementBuild();
                else
                    PlacePendingModel();
                // The first build (the open-time sweep) may have scanned new shader sources.
                QueueUnimportedShaders();
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
