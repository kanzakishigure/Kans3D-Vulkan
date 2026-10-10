#pragma once
#include <array>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Project/Project.h"
#include <Kans3D.h>

#include "Kans3D/Editor/EditorCamera.h"
#include "Kans3D/Editor/EditorResources.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/MaterialEditorPanel.h"
#include "Panels/ProductImportPanel.h"
#include "Panels/PropertiesPanel.h"
#include "Panels/ViewportPanel.h"
#include "SceneHistory.h"
#include "Kans3D/Scene/ScenePlaySession.h"
#include "UI/ConsolePanel.h"
#include "UI/PanelManager.h"
namespace Kans
{
    // Identities of the editor panels (see PanelManager).
    namespace PanelID
    {
        inline constexpr const char* Viewport       = "Viewport";
        inline constexpr const char* Hierarchy      = "Hierarchy";
        inline constexpr const char* Properties     = "Properties";
        inline constexpr const char* ContentBrowser = "ContentBrowser";
        inline constexpr const char* Console        = "Console";
        inline constexpr const char* Stats          = "Stats";
        inline constexpr const char* AssetImporter  = "AssetImporter";
        inline constexpr const char* MaterialEditor = "MaterialEditor";
    } // namespace PanelID

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

    private:
        // ---- Assets and scenes (EditorLayerAssets.cpp) ----
        void                                     OpenProject();
        std::variant<std::monostate, AssetError> RefreshProductAssets(Ref<const AssetRegistry> snapshot = {});
        std::vector<EditorAssetChoice>           CollectAssetChoices(const AssetRegistry& registry) const;
        // Adds a dropped model to the scene, importing it first if needed.
        // `parent` (may be null) receives the placed model entity.
        void                                     PlaceAsset(const std::filesystem::path& file, Entity parent = {});
        void                                     PlacePendingModel();
        // Builds the source of `file` (content browser "Import" / "Reimport").
        void                                     ImportSource(const std::filesystem::path& file);
        AssetID                                  FindPublishedModel(SourceAssetID source) const;
        std::optional<SourceAssetID>             m_PendingPlacement;
        UUID                                     m_PendingPlacementParent = 0;
        std::variant<std::monostate, AssetError> ActivateScene(Ref<Scene> scene, std::filesystem::path path);
        std::variant<std::monostate, AssetError> NewScene();
        std::variant<std::monostate, AssetError> OpenScene(const std::filesystem::path& path);
        std::variant<std::monostate, AssetError> SaveScene(const std::filesystem::path& path);
        // Logs an error and shows it in the main toolbar.
        void Record(const std::variant<std::monostate, AssetError>& result);
        void SaveCurrentScene();
        void DrawSceneFileDialogs();

        // ---- UI (EditorLayerUI.cpp) ----
        void RegisterPanels();
        void DrawMenuBar();
        void DrawMainToolbar();
        void DrawStats();
        void HandleShortcuts();
        // Records a history step once an interaction that may have edited the scene ends.
        void TrackSceneEdits();
        void UndoScene(int steps = 1);
        // Play mode: the scene runs from a snapshot that Stop restores.
        void StartPlay();
        void StopPlay();
        void TogglePause();
        void DrawPlayControls();
        void RedoScene(int steps = 1);
        void OpenImporter(const std::filesystem::path& file);
        void OpenMaterial(const std::filesystem::path& file);
        void CreateMaterial(const std::filesystem::path& directory);

        enum class SceneDialog
        {
            None,
            Open,
            Save
        };
        SceneDialog            m_SceneDialog = SceneDialog::None;
        std::filesystem::path  m_ScenePath;
        std::array<char, 1024> m_ScenePathInput {};
        std::string            m_SceneDialogError;
        // Last editor error, shown in the main toolbar until dismissed.
        std::string m_SceneFileError;

        SceneHistory     m_History;
        ScenePlaySession m_Play;
        bool         m_EditPending = false;

        Scope<EditorAssetImportService> m_AssetImports;
        Scope<AssetManager>             m_Assets;
        // Name of the source being imported, for the toolbar status.
        SourceAssetID m_StatusSource;
        std::string   m_StatusName;


        Ref<Scene>         m_ActiveScene;
        Ref<RenderScene>   m_ActiveRenderScene;
        Ref<SceneRenderer> m_ViewportRenderer;

        Entity m_CameraEntity;
        Entity m_SecondCameraEntity;

        PanelManager        m_Panels;
        HierarchyPanel      m_HierarchyPanel;
        PropertiesPanel     m_PropertiesPanel {m_HierarchyPanel};
        ViewportPanel       m_ViewportPanel {m_HierarchyPanel};
        ContentBrowserPanel m_ContentBrowserPanel;
        ProductImportPanel  m_ProductImportPanel;
        MaterialEditorPanel m_MaterialEditorPanel;
        ConsolePanel        m_Console;
    };

} // namespace Kans
