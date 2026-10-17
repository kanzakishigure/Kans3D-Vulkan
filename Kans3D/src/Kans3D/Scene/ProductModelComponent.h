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

    // Decoded legacy scene data, held only in PrefabInstanceComponent::LegacyPending.
    // Never attached to an entity or passed to the renderer.
    struct ProductModelComponent
    {
        AssetID                            Model;
        std::vector<SceneMaterialOverride> Overrides;

        // A section's legacy part override precedes a mesh-wide override, then
        // its authored slot, bound material override and bound material.
        AssetID
        ResolveMaterial(AssetID mesh, AssetID material, AssetID authoredMaterial = {}, AssetID legacyPart = {}) const
        {
            if (const auto* part = FindOverride(legacyPart))
                return part->Replacement;
            if (const auto* part = FindOverride(mesh))
                return part->Replacement;
            if (const auto* slot = FindOverride(authoredMaterial))
                return slot->Replacement;
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
