#include "kspch.h"
#include "RenderScene.h"

#include "Kans3D/Renderer/RenderWorld.h"
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
        // Until stage 5, replacing the logical scene retains this wrapper's
        // visual settings, as the editor's reload/play path has always done.
        const auto resource    = GetRenderResource();
        const auto environment = GetEnvironmentPath();
        m_2DRenderable.clear();
        m_3DRenderable.clear();
        m_LogicScene = scene;
        SetRenderResource(resource);
        PrepareEnvironment(environment);
    }

    void RenderScene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth  = width;
        m_ViewportHeight = height;
    }

    void RenderScene::CollectLights(SceneInfo& info) const
    {
        m_LogicScene->GetRenderWorld().Synchronize();
        m_LogicScene->GetRenderWorld().CollectLights(info);
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

        if (renderer.GetEnvironment().IrradianceMap)
            renderer.SubmitSkybox(renderer.GetEnvironment().IrradianceMap);
        renderer.EndScene();
    }

    void RenderScene::Extract(RenderView& output) const
    {
        auto& world = m_LogicScene->GetRenderWorld();
        world.Synchronize();
        world.Extract(output, world.Settings);
    }

    const RenderResource& RenderScene::GetRenderResource() { return m_LogicScene->GetRenderWorld().Settings; }
    void                  RenderScene::SetRenderResource(const RenderResource& resource)
    {
        m_LogicScene->GetRenderWorld().Settings = resource;
    }
    const std::filesystem::path& RenderScene::GetEnvironmentPath() const
    {
        return m_LogicScene->GetRenderWorld().EnvironmentPath;
    }
    void RenderScene::PrepareEnvironment(const std::filesystem::path& envMapPath)
    {
        m_LogicScene->GetRenderWorld().EnvironmentPath = envMapPath;
    }

} // namespace Kans
