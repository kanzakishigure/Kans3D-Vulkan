#pragma once
#include <chrono>
#include <glm/glm.hpp>
#include "Graph/RenderGraph.h"
#include "RenderFeature.h"
#include "RenderView.h"
#include "Renderer.h"
#include "ViewTarget.h"

#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Renderer/Resource/RenderScene.h"
#include "Kans3D/Renderer/Resource/SelectionTools.h"
#include "Kans3D/Renderer/Resource/SceneCamera.h"
#include "Kans3D/Scene/ProductModelComponent.h"

namespace Kans
{
    struct SceneRendererSpecification
    {
        bool SwapChainTarget = false;
    };

    class Scene;

    struct SceneRenderStats
    {
        uint32_t DrawCalls = 0;
        uint64_t Triangles = 0;
    };

    class SceneRenderer : public RefCounter
    {
    public:
        SceneRenderer(RenderScene* renderScene, SceneRendererSpecification spec = SceneRendererSpecification());
        ~SceneRenderer();

        // The manager must outlive its use here; pass nullptr before destroying it.
        void              SetAssetManager(AssetManager* assets);
        RenderAssetCache* GetAssetCache() { return m_AssetCache.get(); }

        const std::shared_ptr<Renderer>& GetRenderer() const { return m_Renderer; }
        ViewTarget&                      GetViewTarget() { return *m_Target; }
        const ViewTarget&                GetViewTarget() const { return *m_Target; }

        // Queues each part until EndScene, across all submitted models. Empty, pending or failed
        // Shaders use engine Standard; samplers use Shader-declared fallbacks.
        void SubmitModel(const ProductModelComponent& model,
                         const glm::mat4&             transform,
                         ModelStencilMode             stencil = ModelStencilMode::Disabled);
        // Queues built-in StencilShader, OutLineShader or DebugNormalShader geometry after materials.
        void SubmitModelGeometry(const ProductModelComponent& model,
                                 const glm::mat4&             transform,
                                 const std::string&           shaderName,
                                 ModelGeometryMode            mode = ModelGeometryMode::Default);

        // Editor helpers: meshes of a model for immediate picking (skips parts still loading).
        // Returned MeshDraws borrow GPU resources and are only valid until the asset cache
        // is next updated (including BeginScene or subsequent cache requests).
        void     AppendModelDraws(const ProductModelComponent& model,
                                  const glm::mat4&             transform,
                                  uint32_t                     id,
                                  std::vector<MeshDraw>&       draws);
        uint32_t Pick(const std::vector<MeshDraw>& draws, const glm::mat4& viewProjection, glm::uvec2 pixel);
        PickHit  PickSurface(const std::vector<MeshDraw>& draws, const glm::mat4& viewProjection, glm::uvec2 pixel);
        // Model descriptions to outline, consumed by the next EndScene. Safe to queue before BeginScene.
        void SetHighlight(std::vector<ModelDraw> models) { m_Highlight = std::move(models); }
        // Lit model and blue outline, consumed by the next EndScene. Not pickable.
        void     SetPreview(const ProductModelComponent& model, const glm::mat4& transform);
        void     ClearPreview() { m_Preview.reset(); }
        void     SetViewMode(SceneViewMode mode) { m_ViewMode = mode; }
        // Counts of the last rendered scene.
        const SceneRenderStats& GetStats() const { return m_Stats; }

        void                  Init();
        void                  PrepareEnvironment(EnvironmentPrecomputeSizes sizes = {});
        void                  UpdateEnvironment(std::string path);
        void                  BeginScene(SceneInfo info);
        void                  EndScene();
        void                  SetScene(RenderScene* scene);
        void                  SetViewportSize(uint32_t width, uint32_t height);
        std::vector<uint32_t> GetViewportSize() { return {m_ViewportWidth, m_ViewportHeight}; }

        void               SubmitSprites(std::vector<ViewSprite> sprites);
        const RenderView&  GetRenderView() const { return m_View; }
        const RenderGraph& GetRenderGraph() const { return m_Graph; }
        void               SetPipeline(const std::string& name);
        const std::string& GetPipeline() const { return m_PipelineName; }
        RenderFeatureSet&       GetFeatures() { return m_Features; }
        const RenderFeatureSet& GetFeatures() const { return m_Features; }
        void SubmitGrid(float size, float scale, glm::mat4 transform, ImmediatePassState state = {});
        void SubmitSkybox(RenderHardware::Handle<RenderHardware::Texture> skybox, ImmediatePassState state = {});

        RenderHardware::RenderingDesc GetRenderTarget() const { return SceneTarget(RenderHardware::LoadOp::Load); }
        const RenderHardware::Handle<RenderHardware::Texture>& GetHDRTarget() const { return m_Target->GetHDR(); }
        uintptr_t              GetOutput(uint32_t index) const;
        std::shared_ptr<RenderHardware::Device>                GetDevice() const { return m_Renderer->GetDevice(); }
        std::array<RenderHardware::Handle<RenderHardware::Texture>, 3> GetMaterialEnvironment();
        const RenderHardware::Handle<RenderHardware::Sampler>&         GetMaterialEnvironmentSampler() const
        {
            return m_Renderer->GetMaterialEnvironmentSampler();
        }
        const Environment& GetEnvironment() const
        {
            RefreshEnvironment();
            return m_EnvironmentLighting->Maps;
        }
        const RenderHardware::Handle<RenderHardware::Texture>& GetBrdfLUT() const
        {
            RefreshEnvironment();
            return m_EnvironmentLighting->BrdfLUT;
        }
        float                  GetElapsedTime() const
        {
            return std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
        }
        const RenderHardware::Handle<RenderHardware::Texture> GetSkybox()
        {
            RefreshEnvironment();
            return m_EnvironmentLighting->Skybox;
        }

    private:
        using ShaderDraw   = ViewShaderDraw;
        using GeometryDraw = ViewGeometryDraw;
        bool HasMaterialPass(const RenderView& view, const std::string& tag);
        void DrawShaderMaterials(
            RenderHardware::CommandList&                                                       commands,
            const std::function<void(const RenderHardware::Handle<RenderHardware::Texture>&)>& sampled   = {},
            const std::string&                                                                 tag       = {},
            const RenderView*                                                                  view      = nullptr,
            RenderHardware::Handle<RenderHardware::CommandList>*                               finalPost = nullptr);
        void                 DrawModelGeometry(const GeometryDraw& draw, RenderHardware::CommandList& commands);
        void                 DrawGrid(const ViewGrid& grid, RenderHardware::CommandList& commands);
        void                 DrawSkybox(const ViewSkybox& sky, RenderHardware::CommandList& commands);
        void                 PrepareDrawLists();
        RenderView           m_View;
        RenderGraph          m_Graph;
        std::string          m_PipelineName = "Forward";
        RenderFeatureSet     m_Features;
        friend class ForwardPipeline;
        friend class MaterialPassFeature;
        friend class EditorGridFeature;
        friend class DebugGeometryFeature;
        friend class EditorOutlineFeature;
        friend class EditorPreviewFeature;
        friend class RenderScene;

        // SceneRenderer did't handle the life of Scnene;
        RenderScene*               m_RenderScene;
        SceneInfo                  m_SceneInfo;
        SceneRendererSpecification m_Specification;

        uint32_t         m_ViewportWidth = 0, m_ViewportHeight = 0;
        std::shared_ptr<Renderer>                    m_Renderer;
        std::unique_ptr<ViewTarget>                  m_Target;
        mutable std::shared_ptr<EnvironmentLighting> m_EnvironmentLighting = std::make_shared<EnvironmentLighting>();
        EnvironmentPrecomputeSizes                   m_EnvironmentSizes;
        mutable std::filesystem::path                m_EnvironmentSource;
        void                                         RefreshEnvironment() const;
        RenderHardware::RenderingDesc SceneTarget(RenderHardware::LoadOp load, bool hdr = false) const;

        bool                    m_Active = false;
        std::shared_ptr<RenderAssetCache>     m_AssetCache;
        SelectionOutline        m_Outline;
        std::vector<ModelDraw>   m_Highlight;
        std::optional<ModelDraw> m_Preview;
        SceneViewMode          m_ViewMode = SceneViewMode::Lit;
        SceneRenderStats       m_Stats;
        std::chrono::steady_clock::time_point m_StartTime = std::chrono::steady_clock::now();
    };
} // namespace Kans
