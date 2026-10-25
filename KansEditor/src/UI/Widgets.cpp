#include "Widgets.h"
#include "Kans3D/ImGui/EditorTheme.h"
#include <imgui_internal.h>
#include <string>
#include "EditorIcons.h"

namespace Kans::Widgets
{
    bool ToolButton(const char* icon, const char* tooltip, bool active, bool enabled)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? EditorTheme::Colors().Selection : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::Colors().Hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button(icon, {EditorTheme::Px(28.0f), EditorTheme::Px(26.0f)});
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

    float Pill(ImVec2 position, const std::function<void()>& contents)
    {
        const float pad  = EditorTheme::Px(3.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos({position.x + pad, position.y});
        // In a menu bar the line's text baseline would push the buttons down; `position` is exact.
        ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.0f;
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {EditorTheme::Px(2.0f), 0.0f});
        contents();
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        draw->ChannelsSetCurrent(0);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        draw->AddRectFilled({min.x - pad, min.y - pad},
                            {max.x + pad, max.y + pad},
                            ImGui::GetColorU32(EditorTheme::Colors().Raised),
                            EditorTheme::Px(6.0f));
        draw->ChannelsMerge();
        return max.x + pad;
    }

    bool TextButton(const char* icon, const char* text, const char* tooltip, bool active, bool dropdown)
    {
        std::string label = std::string(icon) + "  " + text;
        if (dropdown)
            label += "  " ICON_FA_CHEVRON_DOWN;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? EditorTheme::Colors().Selection : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::Colors().Hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {EditorTheme::Px(8.0f), ImGui::GetStyle().FramePadding.y});
        const bool clicked = ImGui::Button(label.c_str(), {0.0f, EditorTheme::Px(26.0f)});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }

    float OverlayGroup(ImVec2 position, const std::function<void()>& contents)
    {
        const float pad  = EditorTheme::Px(4.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos({position.x + pad, position.y + pad});
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {EditorTheme::Px(2.0f), EditorTheme::Px(4.0f)});
        contents();
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        draw->ChannelsSetCurrent(0);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        // Panel tone, mostly opaque so the scene shows through without hurting legibility.
        auto background = EditorTheme::Colors().Panel;
        background.w    = 0.82f;
        draw->AddRectFilled({min.x - pad, min.y - pad}, {max.x + pad, max.y + pad}, ImGui::GetColorU32(background),
                            EditorTheme::Px(8.0f));
        draw->ChannelsMerge();
        return max.x + pad;
    }
} // namespace Kans::Widgets
