#include "Widgets.h"

namespace Kans::Widgets
{
    bool ToolButton(const char* icon, const char* tooltip, bool active, bool enabled)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.26f, 0.59f, 0.98f, 0.45f) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.40f, 0.40f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.59f, 0.98f, 0.65f));
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button(icon, {28.0f, 26.0f});
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }

    void ToolSeparator()
    {
        ImGui::SameLine(0, 8.0f);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            {at.x, at.y + 4.0f}, {at.x, at.y + 22.0f}, ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Dummy({1.0f, 26.0f});
        ImGui::SameLine(0, 8.0f);
    }

    void OverlayGroup(ImVec2 position, const std::function<void()>& contents)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(position);
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        ImGui::BeginGroup();
        contents();
        ImGui::EndGroup();
        draw->ChannelsSetCurrent(0);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        draw->AddRectFilled({min.x - 4.0f, min.y - 3.0f}, {max.x + 4.0f, max.y + 3.0f}, IM_COL32(18, 18, 18, 190), 6.0f);
        draw->ChannelsMerge();
    }
} // namespace Kans::Widgets
