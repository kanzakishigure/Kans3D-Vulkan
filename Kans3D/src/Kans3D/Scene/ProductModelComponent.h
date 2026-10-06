#pragma once
#include <variant>
#include <vector>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    // Replaces every use of the authored Material in the model with another Material.
    struct SceneMaterialOverride
    {
        AssetID Authored, Replacement;
    };

    // Persistent logical references to a Model Product. GPU resources live in the renderer.
    struct ProductModelComponent
    {
        AssetID                            Model;
        std::vector<SceneMaterialOverride> Overrides;

        // Returns the Material to draw in place of `authored`.
        AssetID ResolveMaterial(AssetID authored) const
        {
            for (const auto& item : Overrides)
                if (item.Authored.IsValid() && item.Authored == authored && item.Replacement.IsValid())
                    return item.Replacement;
            return authored;
        }
    };
    std::variant<std::monostate, AssetError> ValidateProductModelComponent(const ProductModelComponent& component);
} // namespace Kans
