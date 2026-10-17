#include "kspch.h"
#include "Kans3D/Scene/Entity.h"

#include <string>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Renderer/Features/MaterialPassFeature.h"
#include "Kans3D/Renderer/RenderPipeline.h"
#include "Kans3D/Renderer/RenderWorld.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/SceneRenderer.h"

namespace Kans
{
    SceneRenderer::SceneRenderer(RenderScene* scene, SceneRendererSpecification spec) :
        m_RenderScene(scene), m_Specification(spec)
    {

        m_Features.Register("MaterialOutline",
                            std::make_unique<MaterialPassFeature>("Outline", RenderExtensionPoint::AfterTransparent));
        Init();
    }

    SceneRenderer::~SceneRenderer()
    {
        Renderer2D::ClearTarget();
        m_Result = {};
        m_View   = {};
        m_Target.reset();
        m_Renderer->GetDevice()->WaitIdle();
    }

    void SceneRenderer::SetAssetManager(AssetManager* assets)
    {
        m_View.Assets.reset();
        m_Result.View.Assets.reset();
        m_AssetCache = assets ? m_Renderer->AcquireAssetCache(*assets) : nullptr;
        m_RenderScene->GetLogicScene()->SetPrefabAssetManager(assets);
    }

    std::array<RenderHardware::Handle<RenderHardware::Texture>, 3> SceneRenderer::GetMaterialEnvironment()
    {
        RefreshEnvironment();
        return m_Renderer->GetMaterialEnvironment(*m_EnvironmentLighting);
    }

    void SceneRenderer::Init()
    {
        m_ViewportWidth  = m_RenderScene->m_ViewportWidth;
        m_ViewportHeight = m_RenderScene->m_ViewportHeight;
        m_Renderer       = Renderer::Acquire();
        m_Target = std::make_unique<ViewTarget>(m_Renderer->GetDevice());
        m_Target->Resize({m_ViewportWidth, m_ViewportHeight});
    }

    RenderHardware::RenderingDesc SceneRenderer::SceneTarget(RenderHardware::LoadOp load, bool hdr) const
    {
        return m_Target->Target(load, hdr);
    }

    uintptr_t SceneRenderer::GetOutput(uint32_t index) const { return m_Target->GetOutput(index); }

    void      SceneRenderer::PrepareEnvironment(EnvironmentPrecomputeSizes sizes)
    {
        auto source         = m_RenderScene->GetEnvironmentPath();
        auto lighting       = m_Renderer->PrepareEnvironment(source, sizes);
        m_EnvironmentSizes  = sizes;
        m_EnvironmentSource = std::move(source);
        if (lighting)
            m_EnvironmentLighting = std::move(lighting);
    }

    void SceneRenderer::RefreshEnvironment() const
    {
        if (!m_EnvironmentSource.empty() && m_EnvironmentSource != m_RenderScene->GetEnvironmentPath())
        {
            m_EnvironmentSource = m_RenderScene->GetEnvironmentPath();
            auto lighting       = m_Renderer->PrepareEnvironment(m_EnvironmentSource, m_EnvironmentSizes);
            if (lighting)
                m_EnvironmentLighting = std::move(lighting);
        }
    }

    void SceneRenderer::UpdateEnvironment(std::string path)
    {
        m_RenderScene->PrepareEnvironment(path);
        m_EnvironmentSource              = m_RenderScene->GetEnvironmentPath();
        auto lighting                    = m_Renderer->UpdateEnvironment(m_EnvironmentSource, m_EnvironmentSizes);
        if (lighting)
            m_EnvironmentLighting = std::move(lighting);
    }

    void SceneRenderer::SetPipeline(const std::string& name)
    {
        // Validate immediately, so an unknown pipeline cannot strand an active frame.
        RenderPipelineRegistry::Create(name);
        m_PipelineName = name;
    }

    void SceneRenderer::SubmitSprites(std::vector<ViewSprite> sprites)
    {
        m_View.Sprites = std::move(sprites);
        if (!m_View.Sprites.empty())
            m_View.ImmediateOrder.push_back({ViewImmediateKind::Sprites, 0});
    }

    void SceneRenderer::SetPreview(AssetID prefab, const glm::mat4& transform)
    {
        m_Preview = PrefabPreview {prefab, transform};
    }

    void SceneRenderer::SetScene(RenderScene* scene)
    {
        CORE_ASSERT(scene, "Handel a nullptr scene");
        m_RenderScene = scene;
    }

    void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth  = width;
        m_ViewportHeight = height;
        m_Target->Resize({width, height});
    }

    void SceneRenderer::SubmitGrid(float size, float scale, glm::mat4 transform, ImmediatePassState state)
    {
        m_View.ImmediateOrder.push_back({ViewImmediateKind::Grid, m_View.Grids.size()});
        m_View.Grids.push_back({size, scale, transform, state});
    }

    void SceneRenderer::SubmitSkybox(RenderHardware::Handle<RenderHardware::Texture> skybox, ImmediatePassState state)
    {
        if (!skybox)
            return;
        m_View.ImmediateOrder.push_back({ViewImmediateKind::Skybox, m_View.Skyboxes.size()});
        m_View.Skyboxes.push_back({std::move(skybox), state});
    }

    void SceneRenderer::SubmitMeshGeometry(const ViewGeometryDraw& draw) { m_View.Geometry.push_back(draw); }

    void SceneRenderer::BeginScene(SceneInfo info)
    {
        m_View                     = {};
        m_View.Scene               = info;
        m_View.Viewport            = {m_ViewportWidth, m_ViewportHeight};
        m_View.DebugMode           = m_ViewMode;
        m_View.ExtractWorld        = false;
        m_View.DrawEnvironment     = false;
        m_View.UseWorldEnvironment = false;
        m_View.Assets              = m_AssetCache;
        m_View.Features            = &m_Features;
        m_View.Pipeline            = m_PipelineName;
        m_View.Time                = GetElapsedTime();
        RefreshEnvironment();
        m_View.EnvironmentMaps = {m_EnvironmentLighting->Maps.IrradianceMap, m_EnvironmentLighting->Maps.RadianceMap};
        m_View.BrdfLUT         = m_EnvironmentLighting->BrdfLUT;
        Renderer2D::SetTarget(GetDevice(), GetRenderTarget());
        Renderer2D::SetSubmissionSink([this](auto commands, auto texture) {
            m_View.ImmediateOrder.push_back({ViewImmediateKind::RecordedSprites, m_View.SpriteBatches.size()});
            m_View.SpriteBatches.push_back({std::move(commands), std::move(texture)});
        });
    }
    void SceneRenderer::EndScene()
    {
        m_View.HighlightEntities = std::move(m_EntityHighlight);
        m_View.Preview           = std::move(m_Preview);
        m_Result = m_Renderer->Render(m_RenderScene->GetLogicScene()->GetRenderWorld(), m_View, *m_Target);
        m_View   = m_Result.View;
        m_Preview.reset();
        Renderer2D::ClearTarget();
        if (m_Result.SpriteStats.QuadCount)
            Renderer2D::GetStats() = m_Result.SpriteStats;
    }
    void SceneRenderer::SubmitMesh(const ViewMeshSubmission& submission)
    {
        ExtractMeshDraws(
            m_View, submission, m_ViewMode, m_AssetCache ? m_AssetCache->GetMesh(submission.Mesh.Mesh) : nullptr);
    }
    void SceneRenderer::AppendEntityDraws(Scene& scene, UUID entity, uint32_t id, std::vector<MeshDraw>& draws)
    {
        m_Renderer->AppendEntityDraws(scene.GetRenderWorld(), m_View, entity, id, draws);
    }
    ScenePickHit SceneRenderer::PickSceneSurface(Scene& scene, const glm::mat4& viewProjection, glm::uvec2 pixel)
    {
        RenderView view;
        view.Assets                       = m_AssetCache;
        view.Viewport                     = {m_ViewportWidth, m_ViewportHeight};
        view.Scene.sceneCamera.camera     = Camera(viewProjection);
        view.Scene.sceneCamera.viewMatrix = glm::mat4(1);
        return m_Renderer->PickSceneSurface(scene.GetRenderWorld(), view, pixel);
    }
} // namespace Kans
