#pragma once
#include <string>
#include <glm/glm.hpp>
#include <imgui.h>

// Two-column "label | value" layout for inspector-style panels. Labels sit in a
// fixed column, editors fill the stretched value column, and long values wrap
// inside it. Wrap rows between Begin() and End(); each widget adds one row.
namespace Kans::PropertyGrid
{
    bool Begin(const char* id, float labelWidth = 116.0f);
    void End();

    // Starts a row: draws the label (with an optional tooltip) and moves to the
    // value cell, where the next item takes the full width.
    void Row(const char* label, const char* tooltip = nullptr);

    // Read-only text, wrapped in the value column.
    void Text(const char* label, const std::string& value, bool disabled = false);
    bool Float(const char* label, float& value, float speed = 0.1f, float min = 0.0f, float max = 0.0f,
               const char* format = "%.3f");
    bool Checkbox(const char* label, bool& value);
    bool Color3(const char* label, glm::vec3& value);
    bool Color4(const char* label, glm::vec4& value);
    bool InputText(const char* label, std::string& value);
    bool Combo(const char* label, const char* const items[], int count, int& index);
    // X/Y/Z fields with colored reset buttons; clicking a button resets that axis.
    bool Vec3(const char* label, glm::vec3& value, float reset = 0.0f, float speed = 0.1f);
} // namespace Kans::PropertyGrid
