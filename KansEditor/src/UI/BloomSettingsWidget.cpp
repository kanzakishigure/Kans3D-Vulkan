#include "BloomSettingsWidget.h"
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
    bool DrawPostProcessSettings(PostProcessSettings& settings, const std::function<bool(AssetID&)>& dirtPicker)
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
