#include "kspch.h"
#include "RenderScene.h"

#include "Kans3D/Renderer/RenderWorld.h"
#include "Kans3D/Renderer/Renderer.h"
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
        // Preserve the editor reload/play compatibility policy.
        const auto resource    = GetRenderResource();
        const auto environment = GetEnvironmentPath();
        m_LogicScene = scene;
        SetRenderResource(resource);
        PrepareEnvironment(environment);
    }

    void RenderScene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth  = width;
        m_ViewportHeight = height;
    }

    void RenderScene::OnRenderEditor(Ref<SceneRenderer> renderer, TimeStep ts, const EditorCamera& camera)
    {
        SceneInfo info              = {};
        info.sceneCamera.Position   = camera.GetPosition();
        info.sceneCamera.viewMatrix = camera.GetViewMatrix();
        info.sceneCamera.camera     = camera;
        Render(*renderer, info);
    }

    void RenderScene::OnRenderRuntime(Ref<SceneRenderer> renderer, TimeStep ts)
    {
        if (auto view = MakeRuntimeView(*m_LogicScene, {m_ViewportWidth, m_ViewportHeight}))
            Render(*renderer, view->Scene);
    }

    void RenderScene::Render(SceneRenderer& renderer, const SceneInfo& info)
    {
        renderer.SetScene(this);
        renderer.BeginScene(info);
        renderer.m_View.ExtractWorld    = true;
        renderer.m_View.DrawEnvironment = true;
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
