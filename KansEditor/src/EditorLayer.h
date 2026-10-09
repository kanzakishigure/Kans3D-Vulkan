#pragma once
#include <array>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Project/Project.h"
#include <Kans3D.h>

#include "Kans3D/Editor/EditorCamera.h"
#include "Kans3D/Editor/Panel/SceneHierachyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/MaterialEditorPanel.h"
#include "Panels/ProductImportPanel.h"
namespace Kans
{
    class EditorLayer : public Kans::Layer
    {

    public:
        EditorLayer();
        virtual ~EditorLayer() = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;

        void         OnUpdate(Kans::TimeStep ts) override;
        void         OnEvent(Kans::Event& e) override;
        virtual void OnImGuiRender() override;

        bool OnKeyPressedEvent(KeyPressedEvent& e);

    private:
        void                                     OpenProject();
        std::variant<std::monostate, AssetError> RefreshProductAssets(Ref<const AssetRegistry> snapshot = {});
        std::vector<EditorAssetChoice>           CollectAssetChoices(const AssetRegistry& registry) const;
        // Adds a dropped model to the scene, importing it first if needed.
        void                                     PlaceAsset(const std::filesystem::path& file);
        void                                     PlacePendingModel();
        AssetID                                  FindPublishedModel(SourceAssetID source) const;
        std::optional<SourceAssetID>             m_PendingPlacement;
        std::variant<std::monostate, AssetError> ActivateScene(Ref<Scene> scene, std::filesystem::path path);
        std::variant<std::monostate, AssetError> NewScene();
        std::variant<std::monostate, AssetError> OpenScene(const std::filesystem::path& path);
        std::variant<std::monostate, AssetError> SaveScene(const std::filesystem::path& path);
        void                                     DrawSceneFileMenu();
        std::filesystem::path                    m_ScenePath;
        std::array<char, 1024>                   m_ScenePathInput {};
        std::string                              m_SceneFileError;
        Scope<EditorAssetImportService>          m_AssetImports;
        Scope<AssetManager>                      m_Assets;

        glm::vec2 m_ViewportSize = glm::vec2(0.0f, 0.0f);

        bool m_ViewportFocused  = false;
        bool m_ViewportHovered  = false;
        bool m_CameraNavigating = false;
        bool Switchcamera       = false;

        Ref<Scene>         m_ActiveScene;
        Ref<RenderScene>   m_ActiveRenderScene;
        Ref<SceneRenderer> m_ViewportRenderer;

        Entity m_CameraEntity;
        Entity m_SecondCameraEntity;

        // Gizmo
        int m_GizmoType = 7; // -1 = no gizmo
        int m_GizmoMode = 0; // 0 = local

        EditorCamera m_EditorCamera;
        // We Should Have PanelManger or such things
        // HierachyPanel
        SceneHierachyPanel m_SceneHierachyPanel;
        // ContentBrowserPanel
        ContentBrowserPanel m_ContentBrowserPanel;
        ProductImportPanel  m_ProductImportPanel;
        MaterialEditorPanel m_MaterialEditorPanel;
    };

} // namespace Kans
