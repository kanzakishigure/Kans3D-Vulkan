#pragma once
#include <chrono>
#include <glm/glm.hpp>
#include "Kans3D/Renderer/RHI/FrameBuffer.h"

#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Renderer/Resource/RenderScene.h"
#include "Kans3D/Renderer/Resource/SelectionTools.h"
#include "Kans3D/Renderer/Resource/SceneCamera.h"
#include "Kans3D/Scene/ProductModelComponent.h"

namespace Kans
{
    struct SceneRendererCamera
    {
        Camera    camera;
        glm::mat4 viewMatrix;
        glm::vec3 Position;
    };
    struct SceneInfo
    {
        DirLight            dirLight;
        PointLight          pointLight;
        SceneRendererCamera sceneCamera;
    };
    class ShaderLibrary;

    // Explicit deferred draw intent; submission does not inspect GL state.
    enum class ModelStencilMode
    {
        Disabled,
        WriteReference, // Write 1 where a material fragment passes depth.
    };
    enum class ModelGeometryMode
    {
        Default,
        StencilOutline, // Only outside reference 1, with no stencil writes.
        BackFaces,      // Inverted hull outline: cull front faces.
    };

    // Default sizes preserve the editor's environment precomputation. Small
    // offscreen clients can select lower resolutions without changing shaders.
    struct EnvironmentPrecomputeSizes
    {
        uint32_t Cubemap = 512, Irradiance = 512, Prefilter = 128, BrdfLUT = 512;
    };

    struct SceneRendererSpecification
    {
        bool SwapChainTarget = false;
        // Optional library for auxiliary geometry; defaults to the renderer library.
        Ref<ShaderLibrary> GeometryShaders;
        // Optional environment/grid/skybox shaders for offscreen clients.
        // Keeping this separate preserves the GeometryShaders override's scope.
        Ref<ShaderLibrary> AuxiliaryShaders;
    };

    class Scene;

    // How the editor viewport shades models.
    enum class SceneViewMode
    {
        Lit,
        BaseColor,
        Normals,
        Wireframe,
    };

    struct SceneRenderStats
    {
        uint32_t DrawCalls = 0;
        uint64_t Triangles = 0;
    };

    // Deferred model draw intent. GPU resources are resolved when the draw is consumed.
    struct ModelDraw
    {
        ProductModelComponent Model;
        glm::mat4             Transform {1.0f};
    };

    class SceneRenderer : public RefCounter
    {
    public:
        SceneRenderer(RenderScene* renderScene, SceneRendererSpecification spec = SceneRendererSpecification());
        ~SceneRenderer();

        // The manager must outlive its use here; pass nullptr before destroying it.
        void              SetAssetManager(AssetManager* assets);
        RenderAssetCache* GetAssetCache() { return m_AssetCache.get(); }

        // Queues each part until EndScene, across all submitted models. Empty, pending or failed
        // Shaders use engine Standard; samplers use Shader-declared fallbacks.
        void SubmitModel(const ProductModelComponent& model,
                         const glm::mat4&             transform,
                         ModelStencilMode             stencil = ModelStencilMode::Disabled);
        // Queues model geometry after the material pass with a shader from the library that takes
        // U_ViewProjection, U_Transform and U_ViewPos (outline, stencil, debug).
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

        void SubmitGrid(float size, float scale, glm::mat4 transform);
        void SubmitSkybox(Ref<TextureCube> skybox);

        uintptr_t              GetOutput(uint32_t index) const;
        const Environment&     GetEnvironment() const { return m_Environment; }
        const Ref<Texture2D>&  GetBrdfLUT() const { return m_BrdfLUT; }
        float                  GetElapsedTime() const
        {
            return std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
        }
        const Ref<TextureCube> GetSkybox() { return m_SkyBox; }

    private:
        void DrawGeometry(const Ref<VertexArray>& geometry);
        // Draws a mesh and counts it in the stats.
        void DrawMesh(const GpuMesh& mesh);
        struct ShaderDraw
        {
            AssetID          Mesh;
            glm::mat4        Transform;
            AssetID          Material;
            SceneViewMode    ViewMode;
            ModelStencilMode Stencil;
        };
        struct GeometryDraw
        {
            ProductModelComponent Model;
            glm::mat4             Transform;
            std::string           ShaderName;
            ModelGeometryMode     Mode;
        };
        void                      DrawShaderMaterials(const std::vector<ShaderDraw>& draws);
        void                      DrawModelGeometry(const GeometryDraw& draw);
        std::vector<ShaderDraw>   m_ModelDraws;
        std::vector<GeometryDraw> m_GeometryDraws;

        // SceneRenderer did't handle the life of Scnene;
        RenderScene*               m_RenderScene;
        SceneInfo                  m_SceneInfo;
        SceneRendererSpecification m_Specification;

        uint32_t         m_ViewportWidth = 0, m_ViewportHeight = 0;
        std::shared_ptr<RenderHardware::Device>                      m_Device;
        std::vector<RenderHardware::Handle<RenderHardware::Texture>> m_Colors;
        RenderHardware::Handle<RenderHardware::Texture>              m_Depth;
        RenderHardware::Handle<RenderHardware::Sampler>              m_IBLSampler;
        // RHI-MIGRATION: attachments shared with legacy grid/skybox/geometry/outline passes.
        uint32_t                      m_InteropFramebuffer = 0;
        void                          ResizeTarget();
        RenderHardware::RenderingDesc SceneTarget(RenderHardware::LoadOp load) const;

        Environment             m_Environment;
        Ref<TextureCube>        m_SkyBox;
        Ref<Texture2D>          m_BrdfLUT;
        bool                    m_Active = false;
        Scope<RenderAssetCache> m_AssetCache;
        EntityPicker            m_Picker;
        SelectionOutline        m_Outline;
        std::vector<ModelDraw>   m_Highlight;
        std::optional<ModelDraw> m_Preview;
        SceneViewMode          m_ViewMode = SceneViewMode::Lit;
        SceneRenderStats       m_Stats;
        std::chrono::steady_clock::time_point m_StartTime = std::chrono::steady_clock::now();
    };
} // namespace Kans
