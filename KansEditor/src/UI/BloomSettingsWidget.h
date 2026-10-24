#pragma once
#include <functional>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Renderer/Resource/AntiAliasing.h"
#include "Kans3D/Renderer/Resource/BloomSettings.h"
#include "Kans3D/Renderer/Resource/PostProcessSettings.h"

namespace Kans
{
    // Bloom rows for an open PropertyGrid. `dirtPicker` draws the lens dirt texture
    // row and returns true when the texture changed; without it that row is omitted.
    // Returns true when any setting changed (the caller stores the validated result).
    bool DrawBloomSettings(BloomSettings& settings, const std::function<bool(AssetID&)>& dirtPicker = {});
    // Picker row for one asset; true when the selection changed.
    using AssetRowPicker = std::function<bool(const char* label, AssetID&)>;
    // Exposure, tone mapping and bloom rows for an open PropertyGrid; true when any changed.
    // `materialPicker` adds the ordered post-process material list (omitted without it).
    bool DrawPostProcessSettings(PostProcessSettings&                   settings,
                                 const std::function<bool(AssetID&)>& dirtPicker     = {},
                                 const AssetRowPicker&                 materialPicker = {});
    // Anti-aliasing row for an open PropertyGrid; true when the mode changed.
    bool DrawAntiAliasing(AntiAliasing& mode);
} // namespace Kans
