#include "kspch.h"
#include "RenderScene.h"

#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/SceneRenderer.h"

// temp
#include "Kans3D/Scene/Entity.h"
#define EnableRender2D true
#define EnableSceneRender true
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

    void RenderScene::OnRenderEditor(Ref<SceneRenderer> renderer, TimeStep ts, const EditorCamera& camera)
    {

        // SceneInfo
        SceneInfo sceneinfo = {};

        // Renderer preparation
        {
            // viePosition;

            sceneinfo.sceneCamera.Position   = camera.getPosition();
            sceneinfo.sceneCamera.viewMatrix = camera.getViewMatrix();
            sceneinfo.sceneCamera.camera     = camera;
            // dirLight
            {
                auto view = m_LogicScene->m_Registry.view<DirLightComponent>();
                for (auto entity : view)
                {
                    auto [dirlightCMP]                    = view.get(entity);
                    sceneinfo.dirLight.Dirction           = dirlightCMP.Direction;
                    sceneinfo.dirLight.Ambient_Intensity  = dirlightCMP.Ambient_Intensity;
                    sceneinfo.dirLight.Diffuse_Intensity  = dirlightCMP.Diffuse_Intensity;
                    sceneinfo.dirLight.Specular_Intensity = dirlightCMP.Specular_Intensity;
                }
            }
            // spotLight
            {
                auto view = m_LogicScene->m_Registry.view<PointLightComponent, TransformComponent>();
                for (auto entity : view)
                {
                    auto [pointlightCMP, transformCMP]      = view.get(entity);
                    sceneinfo.pointLight.Position           = transformCMP.Position;
                    sceneinfo.pointLight.Ambient_Intensity  = pointlightCMP.Ambient_Intensity;
                    sceneinfo.pointLight.Diffuse_Intensity  = pointlightCMP.Diffuse_Intensity;
                    sceneinfo.pointLight.Specular_Intensity = pointlightCMP.Specular_Intensity;
                }
            }
        }

        renderer->SetScene(this);
        renderer->BeginScene(sceneinfo);
        SubmitModels(*renderer);
        // Renderer2D
#if EnableRender2D
        {

            if (!m_LogicScene->m_Registry.view<SpriteRendererComponent>().empty())
            {
                Renderer2D::ResetStats();

                Renderer2D::BeginScene(sceneinfo.sceneCamera.camera.GetProjectionMatrix(), camera.getViewMatrix());
                auto group = m_LogicScene->m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
                for (auto entity : group)
                {
                    auto [transformCMP, spriteRendererCMP] = group.get(entity);
                    Renderer2D::DrawQuad(
                        transformCMP.GetTransform(), spriteRendererCMP.Texture, spriteRendererCMP.Color);
                }
                Renderer2D::EndScene();
            }
        }
#endif
#if EnableSceneRender
        // StaticMesh renderer
        {

            auto group = m_LogicScene->m_Registry.view<TransformComponent, StaticMeshComponent>();
            for (auto entity : group)
            {
                Entity entt = {entity, m_LogicScene.get()};
                OpenGLRenderCommand::EnableSetStencil(true);
                OpenGLRenderCommand::StencilOp(StencilOption::KEEP, StencilOption::KEEP, StencilOption::REPLACE);
                OpenGLRenderCommand::SetStencilFunc(StencilFunction::ALWAYS, 1, 0xff);
                OpenGLRenderCommand::SetStencilMask(0xff);
                auto [transformCMP, meshCMP] = group.get(entity);
                // renderer->SubmitStaticMesh(meshCMP.StaticMesh, meshCMP.MaterialTable, transformCMP.GetTransform());
                // ToneShader

                if (m_RenderResource.Piplinestate.EnableToneShader)
                {
                    OpenGLRenderCommand::EnableCullFace(false);
                    Entity e = {entity, m_LogicScene.get()};

                    renderer->SubmitToneCharactorShader(meshCMP.StaticMesh, transformCMP.GetTransform());
                    OpenGLRenderCommand::EnableCullFace(true);
                }

                // Stencil
                if (m_RenderResource.Piplinestate.EnableStencil)
                {
                    OpenGLRenderCommand::SetStencilFunc(StencilFunction::NOTEQUAL, 1, 0xff);
                    OpenGLRenderCommand::SetStencilMask(0x00);
                    TransformComponent ts = transformCMP;
                    ts.Scale *= 1.007f;
                    renderer->SubmitStaticMeshStencil(meshCMP.StaticMesh, ts.GetTransform());
                    OpenGLRenderCommand::SetStencilMask(0xff);
                    OpenGLRenderCommand::EnableSetStencil(false);
                }

                // Stroke
                if (m_RenderResource.Piplinestate.EnableOutline)
                {
                    OpenGLRenderCommand::EnableCullFace(true);
                    OpenGLRenderCommand::CullFace(CullFaceOption::FRONT);
                    renderer->SubmitStaticMeshOutLine(meshCMP.StaticMesh, transformCMP.GetTransform());
                    OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
                    OpenGLRenderCommand::EnableCullFace(false);
                }
                // DebugNormalShader
                if (m_RenderResource.Piplinestate.EnableDebugNormal)
                {
                    renderer->SubmitStaticMeshDebugNormal(meshCMP.StaticMesh, transformCMP.GetTransform());
                }

                // StaticMesh
                if (m_RenderResource.Piplinestate.EnableDefaultShader)
                {
                    if (entt.HasComponent<MaterialComponent>())
                    {
                        Entity e           = {entity, m_LogicScene.get()};
                        auto&  materialCMP = e.GetComponent<MaterialComponent>();

                        OpenGLRenderCommand::EnableCullFace(false);
                        OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
                        renderer->SubmitPBRStaticMesh(
                            meshCMP.StaticMesh, materialCMP.MaterialTable, transformCMP.GetTransform());
                        OpenGLRenderCommand::CullFace(CullFaceOption::FRONT);
                        OpenGLRenderCommand::EnableCullFace(false);
                    }
                }
                // For MousePick
                {
                    if (entt.HasComponent<MaterialComponent>())
                    {
                        Entity e           = {entity, m_LogicScene.get()};
                        auto&  materialCMP = e.GetComponent<MaterialComponent>();

                        OpenGLRenderCommand::EnableCullFace(false);
                        OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
                        renderer->SubmitMeshEntity(meshCMP.StaticMesh, e.GetUUID(), transformCMP.GetTransform());
                        OpenGLRenderCommand::CullFace(CullFaceOption::FRONT);
                        OpenGLRenderCommand::EnableCullFace(false);
                    }
                }

                // Spot cloud
                if (false)
                {
                    Entity e           = {entity, m_LogicScene.get()};
                    auto&  materialCMP = e.GetComponent<MaterialComponent>();

                    OpenGLRenderCommand::EnableCullFace(true);
                    renderer->SubmitSpotCloud(
                        meshCMP.StaticMesh, materialCMP.MaterialTable, transformCMP.GetTransform());
                    OpenGLRenderCommand::EnableCullFace(false);
                }
                // renderer->SubmitMeshPost()
            }

            if (m_RenderResource.Piplinestate.EnableGridShader && Renderer::GetShaderLibrary() &&
                Renderer::GetShaderLibrary()->Exists("GridShader"))
            {
                OpenGLRenderCommand::EnableCullFace(false);

                glm::mat4 transform = glm::mat4(1.0);
                transform *= glm::scale(glm::mat4(1.0), glm::vec3(10, 1, 10));

                renderer->SubmitGrid(0.025, 16.025, transform);
                OpenGLRenderCommand::EnableCullFace(true);
            }

            OpenGLRenderCommand::EnableCullFace(false);
            if (renderer->GetEnvironment().IrradianceMap)
                renderer->SubmitSkybox(renderer->GetEnvironment().IrradianceMap);
            OpenGLRenderCommand::EnableCullFace(true);
        }
        renderer->EndScene();

#endif

        // Post effect
        {
        }
    }
    void RenderScene::OnRenderRuntime(Ref<SceneRenderer> renderer, TimeStep ts)
    {

        Entity cameraEntity = m_LogicScene->GetCameraEntity();
        if (!cameraEntity)
            return;
        // SceneInfo
        SceneInfo sceneinfo = {};

        CameraComponent& cameraCMP   = cameraEntity.GetComponent<CameraComponent>();
        sceneinfo.sceneCamera.camera = cameraCMP.SceneCamera;
        glm::mat4 Cameratransform    = cameraEntity.GetComponent<TransformComponent>().GetTransform();
        glm::vec3 camaerapos         = cameraEntity.GetComponent<TransformComponent>().Position;
        // Renderer preparation
        {
            // viePosition;
            sceneinfo.sceneCamera.Position   = camaerapos;
            sceneinfo.sceneCamera.viewMatrix = glm::inverse(Cameratransform);
            // dirLight
            {
                auto view = m_LogicScene->m_Registry.view<DirLightComponent>();
                for (auto entity : view)
                {
                    auto [dirlightCMP]                    = view.get(entity);
                    sceneinfo.dirLight.Dirction           = dirlightCMP.Direction;
                    sceneinfo.dirLight.Ambient_Intensity  = dirlightCMP.Ambient_Intensity;
                    sceneinfo.dirLight.Diffuse_Intensity  = dirlightCMP.Diffuse_Intensity;
                    sceneinfo.dirLight.Specular_Intensity = dirlightCMP.Specular_Intensity;
                }
            }
            // spotLight
            {
                auto view = m_LogicScene->m_Registry.view<PointLightComponent, TransformComponent>();
                for (auto entity : view)
                {
                    auto [pointlightCMP, transformCMP]      = view.get(entity);
                    sceneinfo.pointLight.Position           = transformCMP.Position;
                    sceneinfo.pointLight.Ambient_Intensity  = pointlightCMP.Ambient_Intensity;
                    sceneinfo.pointLight.Diffuse_Intensity  = pointlightCMP.Diffuse_Intensity;
                    sceneinfo.pointLight.Specular_Intensity = pointlightCMP.Specular_Intensity;
                }
            }
        }

        renderer->SetScene(this);
        renderer->BeginScene(sceneinfo);
        SubmitModels(*renderer);
        // Renderer2D
#if EnableRender2D
        {

            if (!m_LogicScene->m_Registry.view<SpriteRendererComponent>().empty())
            {
                Renderer2D::ResetStats();

                Renderer2D::BeginScene(sceneinfo.sceneCamera.camera.GetProjectionMatrix(),
                                       glm::inverse(Cameratransform));
                auto group = m_LogicScene->m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
                for (auto entity : group)
                {
                    auto [transformCMP, spriteRendererCMP] = group.get(entity);
                    Renderer2D::DrawQuad(
                        transformCMP.GetTransform(), spriteRendererCMP.Texture, spriteRendererCMP.Color);
                }
                Renderer2D::EndScene();
            }
        }
#endif
#if EnableSceneRender
        // StaticMesh renderer
        {

            auto group = m_LogicScene->m_Registry.view<TransformComponent, StaticMeshComponent>();
            for (auto entity : group)
            {
                Entity entt = {entity, m_LogicScene.get()};
                OpenGLRenderCommand::EnableSetStencil(true);
                OpenGLRenderCommand::StencilOp(StencilOption::KEEP, StencilOption::KEEP, StencilOption::REPLACE);
                OpenGLRenderCommand::SetStencilFunc(StencilFunction::ALWAYS, 1, 0xff);
                OpenGLRenderCommand::SetStencilMask(0xff);
                auto [transformCMP, meshCMP] = group.get(entity);
                // renderer->SubmitStaticMesh(meshCMP.StaticMesh, meshCMP.MaterialTable, transformCMP.GetTransform());
                // ToneShader

                if (m_RenderResource.Piplinestate.EnableToneShader)
                {
                    OpenGLRenderCommand::EnableCullFace(false);
                    Entity e = {entity, m_LogicScene.get()};

                    renderer->SubmitToneCharactorShader(meshCMP.StaticMesh, transformCMP.GetTransform());
                    OpenGLRenderCommand::EnableCullFace(true);
                }

                // Stencil
                if (m_RenderResource.Piplinestate.EnableStencil)
                {
                    OpenGLRenderCommand::SetStencilFunc(StencilFunction::NOTEQUAL, 1, 0xff);
                    OpenGLRenderCommand::SetStencilMask(0x00);
                    TransformComponent ts = transformCMP;
                    ts.Scale *= 1.007f;
                    renderer->SubmitStaticMeshStencil(meshCMP.StaticMesh, ts.GetTransform());
                    OpenGLRenderCommand::SetStencilMask(0xff);
                    OpenGLRenderCommand::EnableSetStencil(false);
                }

                // Stroke
                if (m_RenderResource.Piplinestate.EnableOutline)
                {
                    OpenGLRenderCommand::EnableCullFace(true);
                    OpenGLRenderCommand::CullFace(CullFaceOption::FRONT);
                    renderer->SubmitStaticMeshOutLine(meshCMP.StaticMesh, transformCMP.GetTransform());
                    OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
                    OpenGLRenderCommand::EnableCullFace(false);
                }
                // DebugNormalShader
                if (m_RenderResource.Piplinestate.EnableDebugNormal)
                {
                    renderer->SubmitStaticMeshDebugNormal(meshCMP.StaticMesh, transformCMP.GetTransform());
                }

                // StaticMesh
                if (m_RenderResource.Piplinestate.EnableDefaultShader)
                {
                    if (entt.HasComponent<MaterialComponent>())
                    {
                        Entity e           = {entity, m_LogicScene.get()};
                        auto&  materialCMP = e.GetComponent<MaterialComponent>();

                        OpenGLRenderCommand::EnableCullFace(false);
                        OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
                        renderer->SubmitPBRStaticMesh(
                            meshCMP.StaticMesh, materialCMP.MaterialTable, transformCMP.GetTransform());
                        OpenGLRenderCommand::CullFace(CullFaceOption::FRONT);
                        OpenGLRenderCommand::EnableCullFace(false);
                    }
                }
                // For MousePick
                {
                    if (entt.HasComponent<MaterialComponent>())
                    {
                        Entity e           = {entity, m_LogicScene.get()};
                        auto&  materialCMP = e.GetComponent<MaterialComponent>();

                        OpenGLRenderCommand::EnableCullFace(false);
                        OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
                        renderer->SubmitMeshEntity(meshCMP.StaticMesh, e.GetUUID(), transformCMP.GetTransform());
                        OpenGLRenderCommand::CullFace(CullFaceOption::FRONT);
                        OpenGLRenderCommand::EnableCullFace(false);
                    }
                }

                // Spot cloud
                if (false)
                {
                    Entity e           = {entity, m_LogicScene.get()};
                    auto&  materialCMP = e.GetComponent<MaterialComponent>();

                    OpenGLRenderCommand::EnableCullFace(true);
                    renderer->SubmitSpotCloud(
                        meshCMP.StaticMesh, materialCMP.MaterialTable, transformCMP.GetTransform());
                    OpenGLRenderCommand::EnableCullFace(false);
                }
                // renderer->SubmitMeshPost()
            }

            if (m_RenderResource.Piplinestate.EnableGridShader && Renderer::GetShaderLibrary() &&
                Renderer::GetShaderLibrary()->Exists("GridShader"))
            {
                OpenGLRenderCommand::EnableCullFace(false);

                glm::mat4 transform = glm::mat4(1.0);
                transform *= glm::scale(glm::mat4(1.0), glm::vec3(10, 1, 10));

                renderer->SubmitGrid(0.025, 16.025, transform);
                OpenGLRenderCommand::EnableCullFace(true);
            }

            OpenGLRenderCommand::EnableCullFace(false);
            if (renderer->GetEnvironment().IrradianceMap)
                renderer->SubmitSkybox(renderer->GetEnvironment().IrradianceMap);
            OpenGLRenderCommand::EnableCullFace(true);
        }
        renderer->EndScene();

#endif

        // Post effect
        {
        }
    }

    void RenderScene::SubmitModels(SceneRenderer& renderer)
    {
        const auto& state = m_RenderResource.Piplinestate;
        auto        view  = m_LogicScene->m_Registry.view<TransformComponent, ProductModelComponent>();
        for (auto entity : view)
        {
            auto [transformCMP, model] = view.get(entity);
            const glm::mat4 transform  = transformCMP.GetTransform();
            OpenGLRenderCommand::EnableCullFace(false);
            renderer.SubmitModel(model, transform);
            if (state.EnableOutline)
            {
                OpenGLRenderCommand::EnableCullFace(true);
                OpenGLRenderCommand::CullFace(CullFaceOption::FRONT);
                renderer.SubmitModelGeometry(model, transform, "OutLineShader");
                OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
                OpenGLRenderCommand::EnableCullFace(false);
            }
            if (state.EnableDebugNormal)
                renderer.SubmitModelGeometry(model, transform, "DebugNormalShader");
        }
    }

    void RenderScene::PrepareEnvironment(const std::filesystem::path& envMapPath) { m_EnvironmentPath = envMapPath; }

} // namespace Kans
