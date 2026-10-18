#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <imgui.h>
#include "Kans3D/Editor/EditorCamera.h"
#include "ViewportClient.h"
#include "HierarchyPanel.h"
#include "MeshEditTool.h"

namespace Kans
{
    // The scene view: renders the scene from the editor camera (or the scene's
    // primary camera while playing), handles camera navigation, the transform
    // gizmo, the overlay toolbars and files dropped from the content browser.
    //
    // Navigation: right drag looks around (WASD/QE fly, Shift faster, wheel
    // changes the speed), Alt+left drag orbits, middle drag pans, the wheel
    // dollies, F frames the selection. Q/W/E/R pick the gizmo tool.
    class ViewportPanel
    {
    public:
        // A dropped file and, for files dropped on the scene, where it should go.
        using FileDrop = std::function<void(const std::filesystem::path&, std::optional<glm::vec3>)>;
        // The published model of a dragged file (invalid when it is not one, or not imported yet).
        using ModelResolver = std::function<AssetID(const std::filesystem::path&)>;

        explicit ViewportPanel(HierarchyPanel& hierarchy) : m_Hierarchy(hierarchy) {}

        // The viewport renders `scene` through `client` (both may be null).
        void SetContext(Ref<Scene> scene, ViewportClient* client);
        void SetFileDropCallback(FileDrop callback) { m_FileDrop = std::move(callback); }
        void SetModelResolver(ModelResolver resolver) { m_ResolveModel = std::move(resolver); }
        // While playing, the scene renders through its primary camera unless the
        // editor camera is chosen in the toolbar.
        void SetPlaying(bool playing) { m_Playing = playing; }

        void Draw();

        bool          IsHovered() const { return m_Hovered; }
        bool          IsFocused() const { return m_Focused; }
        bool          IsNavigating() const { return m_Navigation != Navigation::None; }
        EditorCamera& GetCamera() { return m_Camera; }
        void          FocusSelection();

    private:
        enum class Navigation
        {
            None,
            Look,
            Orbit,
            Pan
        };

        void Resize(ImVec2 size);
        // The camera the scene is shown through (the primary camera while playing).
        bool      UsesSceneCamera() const;
        glm::mat4 ViewProjection() const;
        // Draws of every model entity, with IDs indexing `entities` (ID - 1).
        void                  PickAt(ImVec2 mouse);
        // The entity a click at `mouse` would select (icons first, then meshes).
        Entity EntityAt(ImVec2 mouse);
        MeshEditTool::View EditView();
        void Render();
        // Icons for lights and cameras, camera frustums and light directions.
        void DrawSceneOverlays();
        void DrawViewCube();
        void DrawStatsOverlay();
        // Projects a world point to the screen; false when behind the camera.
        bool ToScreen(const glm::vec3& point, ImVec2& screen) const;
        void HandleNavigation();
        void HandleShortcuts();
        void DrawToolbar();
        void DrawGizmo();
        void AcceptDrop();
        // Where a model dragged to `mouse` lands (resting on the surface under it).
        glm::vec3 DropPosition(ImVec2 mouse, AssetID model);

        HierarchyPanel&    m_Hierarchy;
        // Vertex/edge/face editing of editable meshes.
        MeshEditTool       m_MeshTool;
        Ref<Scene>         m_Scene;
        ViewportClient*    m_Client = nullptr;
        FileDrop           m_FileDrop;
        ModelResolver      m_ResolveModel;
        // The file being dragged over the viewport and its model, resolved once per drag.
        std::filesystem::path m_DragFile;
        AssetID               m_DragModel;
        EditorCamera       m_Camera;

        ImVec2     m_Min {}, m_Max {};
        glm::uvec2 m_Size {0, 0};
        bool       m_Hovered = false, m_Focused = false, m_ImageHovered = false;
        Navigation m_Navigation = Navigation::None;
        bool       m_Playing = false, m_UseEditorCameraInPlay = false;

        SceneViewMode m_ViewMode  = SceneViewMode::Lit;
        bool          m_ShowIcons = true, m_ShowStats = false;
        // Clickable overlay icons of this frame, and the view cube's area.
        std::vector<std::pair<ImVec2, UUID>> m_Icons;
        ImVec2                               m_CubeMin {}, m_CubeMax {};

        // Gizmo: ImGuizmo operation (-1 = none) and mode (0 = local, 1 = world).
        int   m_GizmoType  = 7;
        int   m_GizmoMode  = 0;
        bool  m_GizmoSnap  = false;
        float m_SnapMove   = 0.5f;
        float m_SnapRotate = 15.0f;
        float m_SnapScale  = 0.1f;
    };
} // namespace Kans
