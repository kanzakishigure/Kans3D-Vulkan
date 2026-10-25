#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "Kans3D/Asset/AssetError.h"
#include "Entity.h"

namespace Kans::SceneClipboard
{
    // Copies each selected subtree once, retaining the given order of roots.
    // Generated prefab parts selected without their instance become plain entities.
    std::string Copy(Scene& scene, const std::vector<Entity>& entities);
    // Cheap check for the supported clipboard header; Paste validates the payload.
    bool IsClipboardText(std::string_view text);
    // Adds a fresh forest, appended under parent (null means scene roots).
    // Invalid input and unsupported versions return AssetError without adding entities.
    std::variant<std::vector<Entity>, AssetError> Paste(Scene& scene, std::string_view text, Entity parent = {});
} // namespace Kans::SceneClipboard
