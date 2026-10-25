#pragma once
#include <functional>
#include <imgui.h>

// Small widgets shared by editor toolbars and overlays.
namespace Kans::Widgets
{
    // Icon button with a tooltip; `active` highlights toggles that are on.
    bool ToolButton(const char* icon, const char* tooltip, bool active = false, bool enabled = true);
    // Thin vertical line between toolbar groups (call between items on one line).
    void ToolSeparator();
    // Draws `contents` (items on one line) on a raised pill starting at `position`;
    // returns the pill's right edge.
    float Pill(ImVec2 position, const std::function<void()>& contents);
    // Draws `contents` at `position` over a translucent rounded background.
    void OverlayGroup(ImVec2 position, const std::function<void()>& contents);
} // namespace Kans::Widgets
