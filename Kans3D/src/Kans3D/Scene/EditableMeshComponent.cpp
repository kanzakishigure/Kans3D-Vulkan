#include "kspch.h"
#include "Components.h"

namespace Kans
{
    bool operator==(const EditableMeshComponent& a, const EditableMeshComponent& b)
    {
        return a.Mesh == b.Mesh && a.Shape == b.Shape && a.Materials == b.Materials &&
               a.RenderCustomDepth == b.RenderCustomDepth && a.CustomStencil == b.CustomStencil &&
               a.CastShadows == b.CastShadows && a.ReceiveShadows == b.ReceiveShadows;
    }
    void RegenerateShape(EditableMeshComponent& component)
    {
        if (!component.Shape)
            return;
        Sanitize(*component.Shape);
        component.Mesh = GenerateShape(*component.Shape);
    }
    size_t MaterialSlotCount(const EditableMeshComponent& component)
    {
        uint32_t used = 0;
        for (const auto& face : component.Mesh.Faces)
            used = std::max(used, face.Material + 1);
        return std::max<size_t>({used, component.Materials.size(), 1});
    }
    void RemoveMaterialSlot(EditableMeshComponent& component, uint32_t slot)
    {
        if (slot >= MaterialSlotCount(component) || MaterialSlotCount(component) <= 1)
            return;
        for (auto& face : component.Mesh.Faces)
            if (face.Material > slot || (face.Material == slot && slot > 0))
                --face.Material;
        if (slot < component.Materials.size())
            component.Materials.erase(component.Materials.begin() + slot);
    }
} // namespace Kans
