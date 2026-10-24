#include "PropertyGrid.h"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

namespace Kans::PropertyGrid
{
    bool Begin(const char* id, float labelWidth)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {4.0f, 2.0f});
        const bool open = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX);
        if (!open)
        {
            ImGui::PopStyleVar();
            return false;
        }
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void End()
    {
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }

    void Row(const char* label, const char* tooltip)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        // Labels that do not fit are clipped by the cell; the tooltip shows them in full.
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::TextDisabled("%s", label);
        if (ImGui::IsItemHovered() && (tooltip || ImGui::CalcTextSize(label).x > width))
            ImGui::SetTooltip("%s", tooltip ? tooltip : label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void Text(const char* label, const std::string& value, bool disabled)
    {
        Row(label);
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(0.0f);
        if (disabled)
            ImGui::TextDisabled("%s", value.c_str());
        else
            ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
    }

    bool Int(const char* label, int& value, int min, int max)
    {
        Row(label);
        ImGui::PushID(label);
        const bool changed = ImGui::DragInt("##value", &value, 0.2f, min, max);
        ImGui::PopID();
        return changed;
    }

    bool Float(const char* label, float& value, float speed, float min, float max, const char* format)
    {
        Row(label);
        ImGui::PushID(label);
        const bool changed = ImGui::DragFloat("##value", &value, speed, min, max, format);
        ImGui::PopID();
        return changed;
    }

    bool Checkbox(const char* label, bool& value)
    {
        Row(label);
        ImGui::PushID(label);
        const bool changed = ImGui::Checkbox("##value", &value);
        ImGui::PopID();
        return changed;
    }

    bool Color3(const char* label, glm::vec3& value)
    {
        Row(label);
        ImGui::PushID(label);
        const bool changed = ImGui::ColorEdit3("##value", glm::value_ptr(value), ImGuiColorEditFlags_Float);
        ImGui::PopID();
        return changed;
    }

    bool Color4(const char* label, glm::vec4& value)
    {
        Row(label);
        ImGui::PushID(label);
        const bool changed = ImGui::ColorEdit4("##value", glm::value_ptr(value), ImGuiColorEditFlags_Float);
        ImGui::PopID();
        return changed;
    }

    bool InputText(const char* label, std::string& value)
    {
        Row(label);
        char buffer[512] = {};
        value.copy(buffer, sizeof(buffer) - 1);
        ImGui::PushID(label);
        const bool changed = ImGui::InputText("##value", buffer, sizeof(buffer));
        ImGui::PopID();
        if (changed)
            value = buffer;
        return changed;
    }

    bool Combo(const char* label, const char* const items[], int count, int& index)
    {
        Row(label);
        ImGui::PushID(label);
        bool changed = false;
        if (ImGui::BeginCombo("##value", index >= 0 && index < count ? items[index] : ""))
        {
            for (int i = 0; i < count; ++i)
                if (ImGui::Selectable(items[i], i == index) && i != index)
                {
                    index   = i;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    bool Vec3(const char* label, glm::vec3& value, float reset, float speed)
    {
        Row(label);
        ImGui::PushID(label);
        const ImVec4 colors[3][2] = {
            {{0.80f, 0.25f, 0.25f, 1.0f}, {0.90f, 0.35f, 0.35f, 1.0f}},
            {{0.30f, 0.65f, 0.30f, 1.0f}, {0.40f, 0.75f, 0.40f, 1.0f}},
            {{0.25f, 0.45f, 0.85f, 1.0f}, {0.35f, 0.55f, 0.95f, 1.0f}},
        };
        const char* axes[3]   = {"X", "Y", "Z"};
        const float height    = ImGui::GetFrameHeight();
        const float spacing   = 4.0f;
        const float fieldWidth =
            std::max(24.0f, (ImGui::GetContentRegionAvail().x - 3.0f * height - 2.0f * spacing) / 3.0f);
        bool changed = false;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
        for (int axis = 0; axis < 3; ++axis)
        {
            if (axis)
                ImGui::SameLine(0.0f, spacing);
            ImGui::PushID(axis);
            ImGui::PushStyleColor(ImGuiCol_Button, colors[axis][0]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[axis][1]);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[axis][0]);
            if (ImGui::Button(axes[axis], {height, height}))
            {
                changed |= value[axis] != reset;
                value[axis] = reset;
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset %s to %g", axes[axis], reset);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fieldWidth);
            changed |= ImGui::DragFloat("##axis", &value[axis], speed, 0.0f, 0.0f, "%.3f");
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
        return changed;
    }
} // namespace Kans::PropertyGrid
