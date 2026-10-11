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
        auto directional = m_LogicScene->m_Registry.view<DirLightComponent>();
        for (auto entity : directional)
        {
            auto [light]                     = directional.get(entity);
            info.dirLight.Dirction           = light.Direction;
            info.dirLight.Ambient_Intensity  = light.Ambient_Intensity;
            info.dirLight.Diffuse_Intensity  = light.Diffuse_Intensity;
            info.dirLight.Specular_Intensity = light.Specular_Intensity;
        }
        auto points = m_LogicScene->m_Registry.view<PointLightComponent, TransformComponent>();
        for (auto entity : points)
        {
            auto [light, transform]            = points.get(entity);
            info.pointLight.Position           = glm::vec3(m_LogicScene->GetWorldTransform({entity, m_LogicScene.get()})[3]);
            info.pointLight.Ambient_Intensity  = light.Ambient_Intensity;
            info.pointLight.Diffuse_Intensity  = light.Diffuse_Intensity;
            info.pointLight.Specular_Intensity = light.Specular_Intensity;
        }
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
        SubmitModels(renderer);

        if (!m_LogicScene->m_Registry.view<SpriteRendererComponent>().empty())
        {
            Renderer2D::ResetStats();
            Renderer2D::BeginScene(info.sceneCamera.camera.GetProjectionMatrix(), info.sceneCamera.viewMatrix);
            auto group = m_LogicScene->m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
            for (auto entity : group)
            {
                auto [transform, sprite] = group.get(entity);
                Renderer2D::DrawQuad(
                    m_LogicScene->GetWorldTransform({entity, m_LogicScene.get()}), sprite.Texture, sprite.Color);
            }
            Renderer2D::EndScene();
        }

        // Preserve the editor's legacy immediate order and opaque grid depth writes.
        if (m_RenderResource.Piplinestate.EnableGridShader)
            renderer.SubmitGrid(0.025f, 16.025f, glm::scale(glm::mat4(1.0f), glm::vec3(10, 1, 10)));

        if (renderer.GetEnvironment().IrradianceMap)
            renderer.SubmitSkybox(renderer.GetEnvironment().IrradianceMap);

        renderer.EndScene();
    }

    void RenderScene::SubmitModels(SceneRenderer& renderer)
    {
        const auto& state = m_RenderResource.Piplinestate;
        auto        view  = m_LogicScene->m_Registry.view<TransformComponent, ProductModelComponent>();
        for (auto entity : view)
        {
            auto [transformCMP, model] = view.get(entity);
            const glm::mat4 transform  = m_LogicScene->GetWorldTransform({entity, m_LogicScene.get()});
            renderer.SubmitModel(
                model, transform, state.EnableStencil ? ModelStencilMode::WriteReference : ModelStencilMode::Disabled);
            if (state.EnableStencil)
                renderer.SubmitModelGeometry(model,
                                             transform * glm::scale(glm::mat4(1.0f), glm::vec3(1.007f)),
                                             "StencilShader",
                                             ModelGeometryMode::StencilOutline);
            if (state.EnableOutline)
                renderer.SubmitModelGeometry(model, transform, "OutLineShader", ModelGeometryMode::BackFaces);
            if (state.EnableDebugNormal)
                renderer.SubmitModelGeometry(model, transform, "DebugNormalShader");
        }
    }

    void RenderScene::PrepareEnvironment(const std::filesystem::path& envMapPath) { m_EnvironmentPath = envMapPath; }

} // namespace Kans
