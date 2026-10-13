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
    EngineFrameUniforms MakeLightingUniforms(const SceneInfo& scene)
    {
        EngineFrameUniforms frame;
        frame.CameraPosition = glm::vec4(scene.sceneCamera.Position, 1);
        const size_t count   = scene.pointLights.empty() ?
                                   (scene.pointLight.Diffuse_Intensity != glm::vec3(0) ? 1 : 0) :
                                   std::min(scene.pointLights.size(), size_t(EnginePointLightLimit));
        for (size_t i = 0; i < count; ++i)
        {
            const auto& light       = scene.pointLights.empty() ? scene.pointLight : scene.pointLights[i];
            frame.PointPositions[i] = glm::vec4(light.Position, 1);
            frame.PointRadiance[i]  = glm::vec4(light.Diffuse_Intensity, 0);
            frame.PointSpecular[i]  = glm::vec4(light.Specular_Intensity, 0);
            if (i < EngineLightCount)
            {
                frame.LightPositions[i] = frame.PointPositions[i];
                frame.LightColors[i]    = frame.PointRadiance[i];
                frame.LightDiffuse[i]   = frame.PointRadiance[i];
            }
        }
        const auto& light           = scene.dirLight;
        const float directionLength = glm::length(light.Dirction);
        if ((scene.HasDirectionalLight || light.Diffuse_Intensity != glm::vec3(0) ||
             light.Ambient_Intensity != glm::vec3(0)) &&
            directionLength > 1e-6f)
        {
            frame.DirectionalDirection = glm::vec4(light.Dirction / directionLength, 1);
            frame.DirectionalRadiance  = glm::vec4(light.Diffuse_Intensity, 0);
            frame.DirectionalSpecular  = glm::vec4(light.Specular_Intensity, 0);
        }
        frame.DirectionalAmbient = glm::vec4(light.Ambient_Intensity, 0);
        frame.LightingInfo       = {float(count), scene.sceneCamera.camera.GetExposure(), 0, 0};
        return frame;
    }
} // namespace Kans
