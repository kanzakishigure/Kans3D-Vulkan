#pragma once
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <imgui.h>
#include <glm/glm.hpp>
#include "Kans3D/Geometry/MeshOperations.h"
#include "Kans3D/Scene/Entity.h"

namespace Kans
{
    class EditorCamera;
    class Scene;

    // Vertex, edge and face editing of an entity's EditableMeshComponent in the
    // scene view (like ProBuilder's element modes). The viewport forwards its
    // clicks, gizmo and overlay drawing here while an element mode is active.
    //
    // Click selects the element under the cursor (Shift adds or removes), a left
    // drag selects with a rectangle, the move/rotate/scale gizmo transforms the
    // selection around its centre and Shift while starting a move extrudes the
    // selected faces or boundary edges. 1-4 switch Object/Vertex/Edge/Face.
    class MeshEditTool
    {
    public:
        enum class Mode
        {
            Object,
            Vertex,
            Edge,
            Face
        };
        // The scene view this frame: screen rectangle and the editor camera.
        struct View
        {
            ImVec2              Min, Max;
            const EditorCamera* Camera = nullptr;
            Scene*              Scene  = nullptr;
        };
        enum class Release
        {
            None,    // Still pressed, or nothing was pressed.
            Handled, // Selected elements.
            Missed,  // Clicked no element: the viewport may select another entity.
        };

        Mode GetMode() const { return m_Mode; }
        void SetMode(Mode mode, Entity entity);
        // Leaves element modes (e.g. when the selection is not an editable mesh).
        void Reset();
        // Whether `entity` is being edited (an element mode and an editable mesh).
        bool IsEditing(Entity entity) const;

        // Elements, the selection and the selection rectangle.
        void DrawOverlay(Entity entity, const View& view);
        // A left press on the scene image; the drag and release are followed in Update.
        void Press(ImVec2 mouse) { m_Press = mouse; }
        Release Update(Entity entity, const View& view);
        // The gizmo on the selection; `operation`/`mode` are ImGuizmo values.
        void DrawGizmo(Entity entity, const View& view, int operation, int mode, const float* snap);
        // Mode buttons for the viewport toolbar.
        void DrawModeButtons(Entity entity);
        // The operations panel shown in element modes.
        void DrawPanel(Entity entity, ImVec2 position);
        // Element-mode keys (1-4, Delete, Ctrl+A); call when the viewport has focus.
        void HandleShortcuts(Entity entity);
        // Whether the gizmo is being dragged on the selection this frame.
        bool IsManipulating() const { return m_Manipulating; }

    private:
        using Operation = std::function<MeshOperationResult(EditableMesh&)>;
        // Runs an operation on a copy and stores the result; the selection becomes
        // what the operation returns when `reselect` is set.
        bool Apply(Entity entity, const char* name, const Operation& operation, bool reselect = false);
        void Validate(Entity entity);
        void SetSelection(MeshSelection selection);
        bool Selected(uint32_t index) const;
        bool Selected(MeshEdge edge) const;
        void Toggle(const MeshSelection& picked, bool add);
        glm::mat4 ModelViewProjection(Entity entity, const View& view) const;
        // Element hit by a click at `pixel` (viewport pixels, top-left origin).
        MeshSelection PickAt(Entity entity, const View& view, glm::vec2 pixel) const;
        void      Fail(const std::string& message);

        Mode          m_Mode = Mode::Object;
        UUID          m_Entity = 0;
        MeshSelection m_Selection;
        std::optional<ImVec2> m_Press;
        bool                  m_Dragging = false;

        // Gizmo drag: the mesh and pivot when it started.
        bool         m_Manipulating = false;
        EditableMesh m_StartMesh;
        glm::mat4    m_Pivot {1}, m_StartPivot {1};

        // Operation settings.
        float m_ExtrudeDistance = 0.5f, m_InsetAmount = 0.1f, m_WeldDistance = 0.01f;
        bool  m_ExtrudeIndividual = false;
        int   m_MaterialSlot = 0;
        FaceUV m_UV;

        std::string                           m_Error;
        std::chrono::steady_clock::time_point m_ErrorTime;
    };
} // namespace Kans
