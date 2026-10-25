#include "MaterialAssignment.h"
#include <algorithm>
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Scene/Scene.h"

namespace Kans
{
    bool AssignMaterial(Scene& scene, Entity entity, uint32_t slot, AssetID material)
    {
        if (!entity)
            return false;
        if (entity.HasComponent<MeshComponent>())
        {
            const Entity root = scene.GetPrefabInstanceRoot(entity);
            if (root && entity.HasComponent<PrefabOwnedComponent>())
            {
                scene.SetPrefabMaterialOverride(
                    root, entity.GetComponent<PrefabOwnedComponent>().NodeKey, slot, material);
                return true;
            }
            entity.PatchComponent<MeshComponent>([&](MeshComponent& mesh) {
                mesh.Materials.resize(std::max<size_t>(mesh.Materials.size(), slot + 1));
                mesh.Materials[slot] = material;
            });
            return true;
        }
        if (entity.HasComponent<EditableMeshComponent>())
        {
            entity.PatchComponent<EditableMeshComponent>([&](EditableMeshComponent& mesh) {
                mesh.Materials.resize(std::max<size_t>(mesh.Materials.size(), slot + 1));
                mesh.Materials[slot] = material;
            });
            return true;
        }
        return false;
    }
} // namespace Kans
