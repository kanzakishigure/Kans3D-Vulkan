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
        if (renderer.GetAssetCache())
            m_LogicScene->UpdatePrefabs(renderer.GetAssetCache()->GetAssetManager());
        else
            m_LogicScene->UpdatePrefabs();
        renderer.BeginScene(info);
        Extract(renderer.m_View);
        for (const auto& mesh : renderer.m_View.Meshes)
            renderer.SubmitMesh(mesh);
        if (renderer.GetEnvironment().IrradianceMap)
            renderer.SubmitSkybox(renderer.GetEnvironment().IrradianceMap);
        renderer.EndScene();
    }

    void RenderScene::Extract(RenderView& output) const
    {
        m_LogicScene->UpdatePrefabs();
        CollectLights(output.Scene);
        output.Meshes.clear();
        output.Geometry.clear();
        output.Sprites.clear();
        output.Grids.clear();
        output.ImmediateOrder.clear();
        const auto& state = m_RenderResource.Piplinestate;
        for (Entity entity : m_LogicScene->GetMeshEntities())
        {
            const auto& mesh  = entity.GetComponent<MeshComponent>();
            const auto  world = m_LogicScene->GetWorldTransform(entity);
            const auto  root  = m_LogicScene->GetPrefabInstanceRoot(entity);
            output.Meshes.push_back(
                {mesh,
                 world,
                 entity.GetUUID(),
                 root ? root.GetUUID() : UUID(0),
                 state.EnableStencil ? ModelStencilMode::WriteReference : ModelStencilMode::Disabled});
            if (state.EnableStencil)
            {
                auto outline = world * glm::scale(glm::mat4(1), glm::vec3(1.007f));
                if (root && entity.HasComponent<PrefabOwnedComponent>())
                {
                    std::function<glm::mat4(Entity)> relative = [&](Entity part) {
                        auto parent = m_LogicScene->GetParent(part);
                        auto local  = part.GetComponent<TransformComponent>().GetTransform();
                        return parent && parent != root ? relative(parent) * local : local;
                    };
                    outline = m_LogicScene->GetWorldTransform(root) * glm::scale(glm::mat4(1), glm::vec3(1.007f)) *
                              relative(entity);
                }
                output.Geometry.push_back({outline,
                                           "StencilShader",
                                           ModelGeometryMode::StencilOutline,
                                           mesh.Mesh,
                                           mesh.Section,
                                           entity.GetUUID()});
            }
            if (state.EnableOutline)
                output.Geometry.push_back(
                    {world, "OutLineShader", ModelGeometryMode::BackFaces, mesh.Mesh, mesh.Section, entity.GetUUID()});
            if (state.EnableDebugNormal)
                output.Geometry.push_back({world,
                                           "DebugNormalShader",
                                           ModelGeometryMode::Default,
                                           mesh.Mesh,
                                           mesh.Section,
                                           entity.GetUUID()});
        }
        std::stable_sort(
            output.Geometry.begin(), output.Geometry.end(), [&](const ViewGeometryDraw& a, const ViewGeometryDraw& b) {
                const auto ae = m_LogicScene->GetEntityByUUID(a.EntityID),
                           be = m_LogicScene->GetEntityByUUID(b.EntityID);
                const auto ar = m_LogicScene->GetPrefabInstanceRoot(ae), br = m_LogicScene->GetPrefabInstanceRoot(be);
                const auto ao = ar ? ar.GetComponent<PrefabInstanceComponent>().DrawOrder : uint64_t(uint32_t(ae));
                const auto bo = br ? br.GetComponent<PrefabInstanceComponent>().DrawOrder : uint64_t(uint32_t(be));
                if (ao != bo)
                    return ao > bo;
                auto rank = [](const auto& draw) {
                    return draw.Mode == ModelGeometryMode::StencilOutline ? 0 :
                           draw.Mode == ModelGeometryMode::BackFaces      ? 1 :
                                                                            2;
                };
                return rank(a) < rank(b);
            });
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
