#pragma once
#include <functional>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Renderer/Resource/BloomSettings.h"

namespace Kans
{
    // Bloom rows for an open PropertyGrid. `dirtPicker` draws the lens dirt texture
    // row and returns true when the texture changed; without it that row is omitted.
    // Returns true when any setting changed (the caller stores the validated result).
    bool DrawBloomSettings(BloomSettings& settings, const std::function<bool(AssetID&)>& dirtPicker = {});
} // namespace Kans
