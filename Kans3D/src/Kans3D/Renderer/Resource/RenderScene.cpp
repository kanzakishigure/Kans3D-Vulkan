#include "kspch.h"
#include "RenderScene.h"

#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/SceneRenderer.h"

#include "Kans3D/Scene/Entity.h"
namespace Kans
{

    RenderScene::RenderScene(const Ref<Scene>& logicScene) : m_LogicScene(logicScene)
    {

        m_ViewportWidth  = m_LogicScene->m_ViewportWidth;
        m_ViewportHeight = m_LogicScene->m_ViewportHeight;
    }

    RenderScene::~RenderScene() = default;

    void RenderScene::SetLogicScene(const Ref<Scene>& scene)
    {
        CORE_ASSERT(scene, "RenderScene requires a logical scene");
        m_2DRenderable.clear();
        m_3DRenderable.clear();
        m_LogicScene = scene;
    }

    void RenderScene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth  = width;
        m_ViewportHeight = height;
    }

    void RenderScene::CollectLights(SceneInfo& info) const
    {
        info.dirLight   = {};
        info.pointLight = {};
        info.pointLights.clear();
        info.HasDirectionalLight = false;
        uint64_t directionalID   = std::numeric_limits<uint64_t>::max();
        auto     directional     = m_LogicScene->m_Registry.view<DirLightComponent, IDComponent>();
        for (auto entity : directional)
        {
            const auto id = uint64_t(directional.get<IDComponent>(entity).ID);
            if (info.HasDirectionalLight && id >= directionalID)
                continue;
            const auto& light = directional.get<DirLightComponent>(entity);
            info.dirLight     = {
                light.Direction, light.Diffuse_Intensity, light.Specular_Intensity, light.Ambient_Intensity};
            info.HasDirectionalLight = true;
            directionalID            = id;
        }
        struct Candidate
        {
            PointLight Light;
            float      Distance;
            uint64_t   ID;
        };
        std::vector<Candidate> candidates;
        auto points = m_LogicScene->m_Registry.view<PointLightComponent, TransformComponent, IDComponent>();
        for (auto entity : points)
        {
            const auto& light    = points.get<PointLightComponent>(entity);
            const auto  position = glm::vec3(m_LogicScene->GetWorldTransform({entity, m_LogicScene.get()})[3]);
            const auto  delta    = position - info.sceneCamera.Position;
            const float distance = glm::dot(delta, delta);
            candidates.push_back(
                {{position, light.Diffuse_Intensity, light.Specular_Intensity, light.Ambient_Intensity},
                 std::isfinite(distance) ? distance : std::numeric_limits<float>::max(),
                 uint64_t(points.get<IDComponent>(entity).ID)});
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.Distance != b.Distance ? a.Distance < b.Distance : a.ID < b.ID;
        });
        for (size_t i = 0; i < std::min(candidates.size(), size_t(EnginePointLightLimit)); ++i)
            info.pointLights.push_back(candidates[i].Light);
        if (!info.pointLights.empty())
            info.pointLight = info.pointLights.front(); // Source-compatible singular snapshot.
    }

    void RenderScene::OnRenderEditor(Ref<SceneRenderer> renderer, TimeStep ts, const EditorCamera& camera)
    {
        SceneInfo info              = {};
        info.sceneCamera.Position   = camera.GetPosition();
        info.sceneCamera.viewMatrix = camera.GetViewMatrix();
        info.sceneCamera.camera     = camera;
        CollectLights(info);
        Render(*renderer, info);
    }

    void RenderScene::OnRenderRuntime(Ref<SceneRenderer> renderer, TimeStep ts)
    {
        Entity cameraEntity = m_LogicScene->GetCameraEntity();
        if (!cameraEntity)
            return;
        const glm::mat4 world       = m_LogicScene->GetWorldTransform(cameraEntity);
        SceneInfo       info        = {};
        info.sceneCamera.camera     = cameraEntity.GetComponent<CameraComponent>().SceneCamera;
        info.sceneCamera.Position   = glm::vec3(world[3]);
        info.sceneCamera.viewMatrix = glm::inverse(world);
        CollectLights(info);
        Render(*renderer, info);
    }

    void RenderScene::Render(SceneRenderer& renderer, const SceneInfo& info)
    {
        renderer.SetScene(this);
        renderer.BeginScene(info);
        Extract(renderer.m_View);
        // Reuse one override vector while preparing parts; no allocation for each model.
        ProductModelComponent preparation;
        size_t                maxOverrides = 0;
        for (const auto& model : renderer.m_View.Models)
            maxOverrides = std::max(maxOverrides, model.OverrideCount);
        preparation.Overrides.reserve(maxOverrides);
        for (const auto& model : renderer.m_View.Models)
        {
            preparation.Model = model.Model;
            const auto begin  = renderer.m_View.MaterialOverrides.begin() + model.OverrideBegin;
            preparation.Overrides.assign(begin, begin + model.OverrideCount);
            renderer.SubmitModel(preparation, model.Transform, model.Stencil);
        }
        if (renderer.GetEnvironment().IrradianceMap)
            renderer.SubmitSkybox(renderer.GetEnvironment().IrradianceMap);
        renderer.EndScene();
    }

    void RenderScene::Extract(RenderView& output) const
    {
        CollectLights(output.Scene);
        output.Models.clear();
        output.MaterialOverrides.clear();
        output.Geometry.clear();
        output.Sprites.clear();
        output.Grids.clear();
        output.ImmediateOrder.clear();
        const auto& state = m_RenderResource.Piplinestate;
        auto        models = m_LogicScene->m_Registry.view<TransformComponent, ProductModelComponent>();
        for (auto entity : models)
        {
            const auto&     model     = models.get<ProductModelComponent>(entity);
            const glm::mat4 transform = m_LogicScene->GetWorldTransform({entity, m_LogicScene.get()});
            output.Models.push_back(
                {model.Model,
                 transform,
                 state.EnableStencil ? ModelStencilMode::WriteReference : ModelStencilMode::Disabled,
                 output.MaterialOverrides.size(),
                 model.Overrides.size()});
            output.MaterialOverrides.insert(
                output.MaterialOverrides.end(), model.Overrides.begin(), model.Overrides.end());
            if (state.EnableStencil)
                output.Geometry.push_back({model,
                                           transform * glm::scale(glm::mat4(1), glm::vec3(1.007f)),
                                           "StencilShader",
                                           ModelGeometryMode::StencilOutline});
            if (state.EnableOutline)
                output.Geometry.push_back({model, transform, "OutLineShader", ModelGeometryMode::BackFaces});
            if (state.EnableDebugNormal)
                output.Geometry.push_back({model, transform, "DebugNormalShader", ModelGeometryMode::Default});
        }
        if (!m_LogicScene->m_Registry.view<SpriteRendererComponent>().empty())
        {
            auto group = m_LogicScene->m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
            for (auto entity : group)
            {
                const auto& sprite = group.get<SpriteRendererComponent>(entity);
                // Legacy runtime ignores TilingFactor and uses DrawQuad's default 1.
                output.Sprites.push_back(
                    {m_LogicScene->GetWorldTransform({entity, m_LogicScene.get()}), sprite.Texture, sprite.Color});
            }
            output.ImmediateOrder.push_back({ViewImmediateKind::Sprites, 0});
        }
        if (state.EnableGridShader)
        {
            output.Grids.push_back({.025f, 16.025f, glm::scale(glm::mat4(1), glm::vec3(10, 1, 10)), {}});
            output.ImmediateOrder.push_back({ViewImmediateKind::Grid, 0});
        }
    }

    void RenderScene::PrepareEnvironment(const std::filesystem::path& envMapPath) { m_EnvironmentPath = envMapPath; }

} // namespace Kans
