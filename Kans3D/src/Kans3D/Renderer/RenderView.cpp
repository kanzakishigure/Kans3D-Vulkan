#include "kspch.h"
#include "RenderView.h"
#include <algorithm>

namespace Kans
{
    void ExtractModelPart(RenderView&                  view,
                          const ProductModelComponent& model,
                          const glm::mat4&             transform,
                          ModelStencilMode             stencil,
                          SceneViewMode                mode,
                          const ModelInstance&         instance,
                          const ViewMeshMetadata&      metadata)
    {
        view.Draws.push_back({instance.Mesh,
                              transform * instance.Transform,
                              model.ResolveMaterial(instance.Mesh, metadata.Material, metadata.AuthoredMaterial),
                              mode,
                              stencil,
                              metadata.LocalBounds,
                              {},
                              false});
    }
    void ExtractModelDraws(RenderView&                                                    view,
                           const ProductModelComponent&                                   model,
                           const glm::mat4&                                               transform,
                           ModelStencilMode                                               stencil,
                           SceneViewMode                                                  mode,
                           const ModelInstances*                                          instances,
                           const std::function<std::optional<ViewMeshMetadata>(AssetID)>& mesh)
    {
        if (!instances)
            return;
        for (const auto& instance : instances->Instances)
            if (auto metadata = mesh(instance.Mesh))
                ExtractModelPart(view, model, transform, stencil, mode, instance, *metadata);
    }
    void ExtractDrawLists(RenderView& view)
    {
        view.MaterialOrder.clear();
        view.Opaque.clear();
        view.Mask.clear();
        view.Blend.clear();
        for (size_t i = 0; i < view.Draws.size(); ++i)
        {
            const auto& draw = view.Draws[i];
            if (!draw.Ready)
                continue;
            const auto center = glm::vec3(draw.Transform * glm::vec4(draw.LocalBounds.Center(), 1));
            const auto offset = center - view.Scene.sceneCamera.Position;
            view.MaterialOrder.push_back({i, draw.Render, glm::dot(offset, offset)});
        }
        std::stable_sort(view.MaterialOrder.begin(), view.MaterialOrder.end(), [](const auto& a, const auto& b) {
            bool aBlend = a.Render.AlphaMode == MaterialAlphaMode::Blend;
            bool bBlend = b.Render.AlphaMode == MaterialAlphaMode::Blend;
            if (aBlend != bBlend)
                return !aBlend;
            return aBlend && a.DistanceSquared > b.DistanceSquared;
        });
        for (const auto& draw : view.MaterialOrder)
        {
            auto& list = draw.Render.AlphaMode == MaterialAlphaMode::Blend ? view.Blend :
                         draw.Render.AlphaMode == MaterialAlphaMode::Mask  ? view.Mask :
                                                                             view.Opaque;
            list.push_back(draw.DrawIndex);
        }
    }
} // namespace Kans
