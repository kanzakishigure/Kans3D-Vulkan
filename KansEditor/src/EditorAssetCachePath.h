#pragma once
#include <filesystem>
#include <variant>
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    // Temporary editor override of Project's default cache location. Independent
    // of the working directory, which KansFileSystem changes during startup.
    std::variant<std::filesystem::path, AssetError> GetEditorAssetCachePath();
} // namespace Kans
