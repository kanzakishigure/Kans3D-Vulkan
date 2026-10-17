#include "kspch.h"
#include "RenderView.h"
#include <algorithm>
#include "Kans3D/Scene/Entity.h"

namespace Kans
{
    void
    ExtractMeshDraws(RenderView& view, const ViewMeshSubmission& submission, SceneViewMode mode, const GpuMesh* mesh)
    {
        if (!mesh)
            return;
        const auto& component = submission.Mesh;
        for (size_t i = 0; i < mesh->Sections.size(); ++i)
        {
            if (component.Section && *component.Section != i)
                continue;
            const auto& section  = mesh->Sections[i];
            const auto  slot     = section.MaterialSlot;
            const auto  material = slot < component.Materials.size() && component.Materials[slot].IsValid() ?
                                       component.Materials[slot] :
                                       mesh->MaterialSlots[slot].DefaultMaterial;
            view.Draws.push_back({component.Mesh,
                                  submission.Transform,
                                  material,
                                  mode,
                                  submission.Stencil,
                                  section.LocalBounds,
                                  {},
                                  false,
                                  uint32_t(i),
                                  submission.EntityID,
                                  submission.InstanceRoot});
        }
    }
    Frustum::Frustum(const glm::mat4& matrix)
    {
        const auto rows = glm::transpose(matrix);
        m_Planes        = {
            rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1], rows[3] - rows[1], rows[2], rows[3] - rows[2]};
    }
    bool Frustum::Intersects(const Bounds& bounds) const
    {
        if (!bounds.IsValid())
            return true; // Unavailable bounds must never hide geometry.
        for (const auto& plane : m_Planes)
        {
            const glm::vec3 support(plane.x >= 0 ? bounds.Max.x : bounds.Min.x,
                                    plane.y >= 0 ? bounds.Max.y : bounds.Min.y,
                                    plane.z >= 0 ? bounds.Max.z : bounds.Min.z);
            const float     distance = glm::dot(glm::vec3(plane), support) + plane.w;
            const float     tolerance =
                1e-5f * (glm::dot(glm::abs(glm::vec3(plane)), glm::abs(support)) + std::abs(plane.w) + 1);
            if (std::isfinite(distance) && distance < -tolerance)
                return false;
        }
        return true;
    }
    void ExtractDrawLists(RenderView& view, const Frustum* frustum)
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
            if (frustum && !frustum->Intersects(draw.HasWorldBounds ? draw.WorldBounds :
                                                                      draw.LocalBounds.Transformed(draw.Transform)))
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

namespace Kans
{
    std::optional<RenderView> MakeRuntimeView(Scene& scene, glm::uvec2 viewport)
    {
        const auto entity = scene.GetCameraEntity();
        if (!entity)
            return std::nullopt;
        const auto transform = scene.GetWorldTransform(entity);
        RenderView view;
        view.Viewport                     = viewport;
        view.Scene.sceneCamera.camera     = entity.GetComponent<CameraComponent>().SceneCamera;
        view.Scene.sceneCamera.Position   = glm::vec3(transform[3]);
        view.Scene.sceneCamera.viewMatrix = glm::inverse(transform);
        return view;
    }
} // namespace Kans
