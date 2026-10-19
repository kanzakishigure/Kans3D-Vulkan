#pragma once
#include <map>
#include <string>
#include <imgui.h>
#include <vector>
#include "HierarchyPanel.h"
#include "Kans3D/Editor/EditorAssetChoice.h"

namespace Kans
{
    struct AnimationControllerData;

    // The selected model's animator as a node graph: one tab per layer, its states as
    // nodes and transitions as arrows, the playing state highlighted. Right-click adds a
    // state, dragging from a node's port to another node adds a transition, and the side
    // panel edits the selected state (including its blend tree) or transition. Also lists
    // the controller's expression presets.
    class AnimatorGraphPanel
    {
    public:
        explicit AnimatorGraphPanel(HierarchyPanel& hierarchy) : m_Hierarchy(hierarchy) {}
        void Draw();
        // Clips offered for states and blend tree children.
        void SetAssetChoices(std::vector<EditorAssetChoice> choices) { m_Choices = std::move(choices); }

    private:
        bool DrawGraph(AnimationControllerData& controller, size_t layer);
        bool DrawSelection(AnimationControllerData& controller, size_t layer);
        bool DrawExpressions(AnimationControllerData& controller);
        ImVec2& NodePosition(size_t layer, size_t state);

        // A clip from the project (true when `clip` changed).
        bool ClipCombo(const char* id, AssetID& clip);

        HierarchyPanel&                m_Hierarchy;
        std::vector<EditorAssetChoice> m_Choices;
        UUID            m_Entity = 0;
        size_t          m_Layer = 0;
        int             m_SelectedState = -1, m_SelectedTransition = -1;
        int             m_LinkFrom = -2; // State a new transition starts from (-1 = Any State), -2 = none.
        ImVec2          m_Pan {240, 60}; // Leaves room for Any State, left of the first state.
        // Node positions per (layer, state) of the current entity; laid out on first sight.
        std::map<std::pair<size_t, size_t>, ImVec2> m_Positions;
    };
} // namespace Kans
