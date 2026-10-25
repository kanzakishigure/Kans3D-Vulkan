#include "BloomSettingsWidget.h"
#include <string>
#include <imgui.h>
#include "UI/EditorIcons.h"
#include "UI/PropertyGrid.h"

namespace Kans
{
    bool DrawBloomSettings(BloomSettings& settings, const std::function<bool(AssetID&)>& dirtPicker)
    {
        bool changed = PropertyGrid::Checkbox("Bloom", settings.Enabled);
        if (!settings.Enabled)
            return changed;
        const char* modes[] = {"Additive", "Energy conserving"};
        int         mode    = int(settings.Mode);
        if (PropertyGrid::Combo("Mode", modes, 2, mode))
        {
            settings.Mode = BloomSettings::Composite(mode);
            changed       = true;
        }
        const float maxIntensity = settings.Mode == BloomSettings::EnergyConserving ? 1.0f : 128.0f;
        changed |= PropertyGrid::Float("Intensity", settings.Intensity, 0.01f, 0.0f, maxIntensity);
        changed |= PropertyGrid::Float("Threshold", settings.Threshold, 0.01f, 0.0f, 65472.0f);
        changed |= PropertyGrid::Float("Knee", settings.Knee, 0.01f, 0.0f, 1.0f);
        changed |= PropertyGrid::Float("Scatter", settings.Scatter, 0.01f, 0.0f, 1.0f);
        changed |= PropertyGrid::Float("Clamp", settings.Clamp, 10.0f, 0.0f, 65472.0f, "%.0f");
        changed |= PropertyGrid::Color3("Tint", settings.Tint);
        changed |= PropertyGrid::Checkbox("High quality", settings.HighQualityFiltering);
        if (dirtPicker)
        {
            changed |= dirtPicker(settings.DirtTexture);
            if (settings.DirtTexture.IsValid())
                changed |= PropertyGrid::Float("Dirt intensity", settings.DirtIntensity, 0.01f, 0.0f, 128.0f);
        }
        return changed;
    }
    namespace
    {
        // Entries run in list order within the stage their shader declares
        // (before or after tone mapping); surface materials are ignored by the renderer.
        bool DrawPostMaterials(std::vector<PostProcessMaterial>& materials, const AssetRowPicker& picker)
        {
            bool   changed = false;
            size_t erase = SIZE_MAX, up = SIZE_MAX;
            for (size_t i = 0; i < materials.size(); ++i)
            {
                ImGui::PushID(int(i));
                auto&             entry = materials[i];
                const std::string label = "Post material " + std::to_string(i + 1);
                changed |= picker(label.c_str(), entry.Material);
                PropertyGrid::Row("");
                changed |= ImGui::Checkbox("Enabled", &entry.Enabled);
                ImGui::SameLine();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::SmallButton(ICON_FA_ARROW_UP))
                    up = i;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(i + 1 == materials.size());
                if (ImGui::SmallButton(ICON_FA_ARROW_DOWN))
                    up = i + 1;
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH))
                    erase = i;
                ImGui::PopID();
            }
            if (up != SIZE_MAX)
            {
                std::swap(materials[up - 1], materials[up]);
                changed = true;
            }
            if (erase != SIZE_MAX)
            {
                materials.erase(materials.begin() + erase);
                changed = true;
            }
            AssetID added;
            if (picker("Add post material", added) && added.IsValid())
            {
                materials.push_back({added, true});
                changed = true;
            }
            return changed;
        }
    } // namespace
    bool DrawPostProcessSettings(PostProcessSettings&                   settings,
                                 const std::function<bool(AssetID&)>& dirtPicker,
                                 const AssetRowPicker&                 materialPicker)
    {
        bool changed = PropertyGrid::Float("Exposure", settings.Exposure, 0.01f, 0.001f, 128.0f);
        // Standard matches Blender's "Standard" view transform: exposure, then sRGB.
        const char* toneMappings[] = {"ACES", "Standard (none)"};
        int         toneMapping    = int(settings.ToneMapping);
        if (PropertyGrid::Combo("Tone mapping", toneMappings, 2, toneMapping))
        {
            settings.ToneMapping = ToneMappingMode(toneMapping);
            changed              = true;
        }
        // Blackman-Harris reconstruction radius (Blender Film > Filter Size); 0 keeps the sharp resolve.
        changed |= PropertyGrid::Float("Pixel filter", settings.FilterSize, 0.05f, 0.0f, 4.0f, "%.2f px");
        changed |= DrawBloomSettings(settings.Bloom, dirtPicker);
        if (materialPicker)
            changed |= DrawPostMaterials(settings.Materials, materialPicker);
        return changed;
    }
    bool DrawAntiAliasing(AntiAliasing& mode)
    {
        const char* names[] = {"Off", "FXAA (fast)", "MSAA 2x", "MSAA 4x", "MSAA 8x"};
        int         index   = int(mode);
        if (!PropertyGrid::Combo("Anti-aliasing", names, 5, index))
            return false;
        mode = AntiAliasing(index);
        return true;
    }
} // namespace Kans
