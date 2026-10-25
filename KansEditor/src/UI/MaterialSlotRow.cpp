#include "MaterialSlotRow.h"
#include <imgui.h>
#include <algorithm>
#include "Kans3D/ImGui/EditorTheme.h"
#include "Kans3D/Renderer/Resource/Texture.h"
#include "EditorIcons.h"
#include "Widgets.h"

namespace Kans::Widgets
{
    bool MaterialSlotRow(ComponentUI&       ui,
                         const std::string& title,
                         const std::string& detail,
                         const SlotParts&   parts,
                         AssetID            imported,
                         AssetID            current,
                         AssetID&           chosen)
    {
        const auto&   style    = ImGui::GetStyle();
        const auto&   colors   = EditorTheme::Colors();
        const float   tool     = EditorTheme::Px(26.0f);
        const float   side     = tool + style.ItemSpacing.y + ImGui::GetFrameHeight();
        const bool    isolated = ui.IsSlotIsolated(parts), highlighted = ui.IsSlotHighlighted(parts);
        const AssetID browser = ui.BrowserMaterial();
        bool          picked  = false;
        const float   pad     = EditorTheme::Px(3.0f);
        ImDrawList*   draw    = ImGui::GetWindowDrawList();
        // The gap comes first, so the row itself is the last item (for tooltips and drops).
        ImGui::Dummy({0, pad});
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        ImGui::BeginGroup();

        const ImVec2 at = ImGui::GetCursorScreenPos(), end {at.x + side, at.y + side};
        ImGui::Dummy({side, side});
        draw->AddRectFilled(at, end, ImGui::GetColorU32(colors.Inset), style.FrameRounding);
        if (const auto texture = ui.MaterialThumbnail(current))
            draw->AddImageRounded(static_cast<ImTextureID>(uintptr_t(texture->GetImGuiTextureID())),
                                  at,
                                  end,
                                  {0, 1},
                                  {1, 0},
                                  IM_COL32_WHITE,
                                  style.FrameRounding);
        else
            CenterGlyph(ICON_FA_CIRCLE_DOT, at, end, ImGui::GetColorU32(colors.TextOff));
        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title.c_str());
        ImGui::SameLine();
        ImGui::TextColored(colors.TextDim, "%s", detail.c_str());
        // The four tools sit at the right end (SameLine offsets are group-relative here, so push them).
        ImGui::SameLine();
        const float tools = 4 * EditorTheme::Px(28.0f) + 3 * style.ItemSpacing.x;
        if (const float gap = ImGui::GetContentRegionAvail().x - tools; gap > style.ItemSpacing.x)
        {
            ImGui::Dummy({gap - style.ItemSpacing.x, 0});
            ImGui::SameLine();
        }
        if (ToolButton(ICON_FA_EYE, "Highlight these parts in the viewport", highlighted))
            ui.ToggleSlotHighlight(parts);
        ImGui::SameLine();
        if (ToolButton(ICON_FA_CROSSHAIRS, isolated ? "Show the whole scene" : "Show only these parts", isolated))
            ui.ToggleSlotIsolate(parts);
        ImGui::SameLine();
        if (ToolButton(ICON_FA_ARROW_LEFT,
                       browser.IsValid() ? ("Use " + ui.MaterialName(browser)).c_str() :
                                           "Use the material selected in the Content Browser",
                       false,
                       browser.IsValid() && browser != current))
        {
            chosen = browser;
            picked = true;
        }
        ImGui::SameLine();
        if (ToolButton(
                ICON_FA_ROTATE_LEFT, ("Restore " + ui.MaterialName(imported)).c_str(), false, current != imported))
        {
            chosen = {};
            picked = true;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        AssetID combo;
        if (ui.MaterialCombo(imported, current, combo))
        {
            chosen = combo;
            picked = true;
        }
        ImGui::EndGroup();
        ImGui::EndGroup();

        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (hovered)
            ui.HoverSlot(parts);
        if (const AssetID dropped = ui.AcceptMaterialDrop(); dropped.IsValid())
        {
            chosen = dropped;
            picked = true;
        }
        draw->ChannelsSetCurrent(0);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        // Hover fills the row; a pinned or isolated row carries a strip in its viewport colour,
        // which keeps the tools' own on state readable.
        if (hovered)
            draw->AddRectFilled({min.x - pad, min.y - pad},
                                {max.x + pad, max.y + pad},
                                ImGui::GetColorU32(colors.Hover),
                                style.FrameRounding);
        if (highlighted || isolated)
        {
            const glm::vec4 strip = highlighted ? SlotPinnedColor : SlotIsolatedColor;
            draw->AddRectFilled({min.x - pad, min.y - pad},
                                {min.x - pad + EditorTheme::Px(3.0f), max.y + pad},
                                ImGui::GetColorU32(ImVec4(strip.r, strip.g, strip.b, 1.0f)),
                                style.FrameRounding);
        }
        draw->ChannelsMerge();
        return picked;
    }
} // namespace Kans::Widgets
