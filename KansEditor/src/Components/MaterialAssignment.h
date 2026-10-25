#pragma once
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Scene/Entity.h"

namespace Kans
{
    class Scene;

    // Puts `material` in `entity`'s material slot, as a per-instance override: a prefab
    // part records a prefab override, a mesh or editable mesh its own slot. An invalid
    // material restores the imported one. False when the entity has no such mesh.
    bool AssignMaterial(Scene& scene, Entity entity, uint32_t slot, AssetID material);
} // namespace Kans
