#pragma once
#include <filesystem>
#include <functional>
#include <imgui.h>
#include "Kans3D/Editor/EditorCamera.h"
#include "Kans3D/Renderer/Resource/RenderScene.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "HierarchyPanel.h"

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
        using FileDrop = std::function<void(const std::filesystem::path&)>;

        explicit ViewportPanel(HierarchyPanel& hierarchy) : m_Hierarchy(hierarchy) {}

        void SetContext(Ref<Scene> scene, Ref<RenderScene> renderScene, Ref<SceneRenderer> renderer);
        void SetFileDropCallback(FileDrop callback) { m_FileDrop = std::move(callback); }
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
        void Render();
        void HandleNavigation();
        void HandleShortcuts();
        void DrawToolbar();
        void DrawGizmo();
        void AcceptDrop();

        HierarchyPanel&    m_Hierarchy;
        Ref<Scene>         m_Scene;
        Ref<RenderScene>   m_RenderScene;
        Ref<SceneRenderer> m_Renderer;
        FileDrop           m_FileDrop;
        EditorCamera       m_Camera;

        ImVec2     m_Min {}, m_Max {};
        glm::uvec2 m_Size {0, 0};
        bool       m_Hovered = false, m_Focused = false, m_ImageHovered = false;
        Navigation m_Navigation = Navigation::None;
        bool       m_Playing = false, m_UseEditorCameraInPlay = false;

        // Gizmo: ImGuizmo operation (-1 = none) and mode (0 = local, 1 = world).
        int   m_GizmoType  = 7;
        int   m_GizmoMode  = 0;
        bool  m_GizmoSnap  = false;
        float m_SnapMove   = 0.5f;
        float m_SnapRotate = 15.0f;
        float m_SnapScale  = 0.1f;
    };
} // namespace Kans
