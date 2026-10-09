#pragma once
#include <variant>
#include <vector>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    // Draws a different Material. `Authored` is either one of the model's Mesh
    // Products (that part only) or a Material it uses (every part using it).
    struct SceneMaterialOverride
    {
        AssetID Authored, Replacement;
    };

    // Persistent logical references to a Model Product. GPU resources live in the renderer.
    struct ProductModelComponent
    {
        AssetID                            Model;
        std::vector<SceneMaterialOverride> Overrides;

        // The Material to draw for a part: its own override, else its material's, else its own.
        AssetID ResolveMaterial(AssetID mesh, AssetID material) const
        {
            if (const auto* part = FindOverride(mesh))
                return part->Replacement;
            if (const auto* slot = FindOverride(material))
                return slot->Replacement;
            return material;
        }
        // The complete override for `authored`, or nullptr.
        const SceneMaterialOverride* FindOverride(AssetID authored) const
        {
            for (const auto& item : Overrides)
                if (authored.IsValid() && item.Authored == authored && item.Replacement.IsValid())
                    return &item;
            return nullptr;
        }
        // Sets or, with an invalid replacement, removes the override for `authored`.
        void SetOverride(AssetID authored, AssetID replacement)
        {
            for (auto it = Overrides.begin(); it != Overrides.end(); ++it)
                if (it->Authored == authored)
                {
                    if (replacement.IsValid())
                        it->Replacement = replacement;
                    else
                        Overrides.erase(it);
                    return;
                }
            if (authored.IsValid() && replacement.IsValid())
                Overrides.push_back({authored, replacement});
        }
    };
    std::variant<std::monostate, AssetError> ValidateProductModelComponent(const ProductModelComponent& component);
} // namespace Kans
