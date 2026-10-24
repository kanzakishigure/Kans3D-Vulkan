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
} // namespace Kans
