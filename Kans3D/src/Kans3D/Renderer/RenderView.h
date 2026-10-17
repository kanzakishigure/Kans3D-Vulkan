#pragma once
#include <functional>
#include <optional>
#include <glm/glm.hpp>
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Scene/Components.h"
#include "ImmediatePassState.h"
#include "RenderSettings.h"
#include "Resource/Camera.h"

namespace Kans
{
    class Scene;
    class RenderFeatureSet;
    class RenderWorld;
    struct ScenePickHit
    {
        UUID                     EntityID = 0, InstanceRoot = 0;
        std::optional<glm::vec3> Position;
    };
    struct SceneRenderStats
    {
        uint32_t DrawCalls = 0;
        uint64_t Triangles = 0;
    };
    struct SpriteStatistics
    {
        uint32_t DrawCalls = 0, QuadCount = 0;
        uint32_t GetTotalVertexCount() const { return QuadCount * 4; }
        uint32_t GetTotalIndexCount() const { return QuadCount * 6; }
    };
    struct RenderCamera
    {
        Camera    camera;
        glm::mat4 viewMatrix;
        glm::vec3 Position;
    };
    using SceneRendererCamera = RenderCamera; // Editor facade compatibility.
    struct SceneInfo
    {
        DirLight            dirLight;
        PointLight          pointLight;
        RenderCamera            sceneCamera;
        std::vector<PointLight> pointLights;
        bool                    HasDirectionalLight = false;
    };
    enum class ModelStencilMode
    {
        Disabled,
        WriteReference
    };
    enum class ModelGeometryMode
    {
        Default,
        StencilOutline,
        BackFaces
    };
    enum class SceneViewMode
    {
        Lit,
        BaseColor,
        Normals,
        Wireframe
    };
    struct PrefabPreview
    {
        AssetID   Prefab;
        glm::mat4 Transform {1};
    };
    struct ViewMeshSubmission
    {
        MeshComponent    Mesh;
        glm::mat4        Transform {1};
        UUID             EntityID = 0, InstanceRoot = 0;
        ModelStencilMode Stencil = ModelStencilMode::Disabled;
    };
    struct ViewShaderDraw
    {
        AssetID          Mesh;
        glm::mat4        Transform;
        AssetID          Material;
        SceneViewMode    ViewMode;
        ModelStencilMode Stencil;
        // Prepared asset metadata only: extraction never resolves or uploads resources.
        Bounds              LocalBounds;
        MaterialRenderState Render;
        bool                Ready = false;
        uint32_t            Section = 0;
        UUID                EntityID = 0, InstanceRoot = 0;
        Bounds              WorldBounds;
        bool                HasWorldBounds = false;
    };
    struct ViewGeometryDraw
    {
        glm::mat4             Transform;
        std::string           ShaderName;
        ModelGeometryMode     Mode;
        AssetID                 Mesh;
        std::optional<uint32_t> Section;
        UUID                    EntityID = 0;
    };
    struct ViewReadyDraw
    {
        size_t              DrawIndex;
        MaterialRenderState Render;
        float               DistanceSquared;
    };
    struct ViewSprite
    {
        glm::mat4      Transform;
        Ref<Texture2D> Texture;
        glm::vec4      Color;
    };
    struct ViewGrid
    {
        float              Size, Scale;
        glm::mat4          Transform;
        ImmediatePassState State;
    };
    struct ViewSkybox
    {
        RenderHardware::Handle<RenderHardware::Texture> Texture;
        ImmediatePassState                              State;
    };
    struct ViewSpriteBatch
    {
        RenderHardware::Handle<RenderHardware::CommandList> Commands;
        RenderHardware::Handle<RenderHardware::Texture>     Texture;
    };
    enum class ViewImmediateKind
    {
        Sprites,
        RecordedSprites,
        Grid,
        Skybox
    };
    struct ViewImmediate
    {
        ViewImmediateKind Kind;
        size_t            Index;
        size_t            Count = SIZE_MAX;
    };
    struct ViewEnvironment
    {
        RenderHardware::Handle<RenderHardware::Texture> IrradianceMap, RadianceMap;
    };
    // One view/frame. Owns intent, CPU sort metadata and resource handles, never borrowed meshes.
    // Matrices remain in the engine's coordinate convention until recording.
    struct ViewMaterialResources
    {
        std::shared_ptr<const GpuShader>           Shader;
        std::shared_ptr<const GpuMaterialBindings> Bindings;
    };
    struct RenderView
    {
        SceneInfo                          Scene {};
        RenderFeatureSet* Features = nullptr; // Borrowed for this call; owned by the viewport/configuration.
        std::shared_ptr<RenderAssetCache>                 Assets;
        std::map<AssetID, std::shared_ptr<const GpuMesh>> MeshResources;
        std::map<AssetID, ViewMaterialResources>          MaterialResources;
        std::optional<EngineFrameUniforms>                LightingUniforms;
        std::string                                       Pipeline = "Forward";
        std::optional<float>                              Exposure;
        float                                             Time         = 0;
        bool                                              ExtractWorld = true;
        std::optional<std::filesystem::path> EnvironmentPath; // nullopt inherits world; empty disables IBL.
        bool                                 DrawEnvironment     = true;
        bool                                 UseWorldEnvironment = true;
        glm::uvec2                         Viewport {0};
        SceneViewMode                      DebugMode = SceneViewMode::Lit;
        std::vector<ViewMeshSubmission>    Meshes;
        std::vector<UUID>                  HighlightEntities;
        std::vector<ViewShaderDraw>        Draws;
        std::vector<ViewReadyDraw>         MaterialOrder;
        std::vector<size_t>                Opaque, Mask, Blend; // Draw indices, in their filtered execution order.
        std::vector<ViewGeometryDraw>      Geometry;
        std::vector<ViewSprite>            Sprites;
        std::vector<ViewGrid>              Grids;
        std::vector<ViewSkybox>            Skyboxes;
        std::vector<ViewSpriteBatch>       SpriteBatches; // Pre-recorded immediate sprite batches.
        std::vector<ViewImmediate>         ImmediateOrder;
        ViewEnvironment                    EnvironmentMaps;
        RenderHardware::Handle<RenderHardware::Texture> BrdfLUT;
        std::optional<PrefabPreview>                    Preview;
    };
    // Scene-camera game view: no editor features, highlights or previews.
    std::optional<RenderView> MakeRuntimeView(Scene& scene, glm::uvec2 viewport);
    // CPU-only extraction of an available mesh; absent data emits no draws.
    void
    ExtractMeshDraws(RenderView& view, const ViewMeshSubmission& submission, SceneViewMode mode, const GpuMesh* mesh);
    // Planes extracted from a device view-projection matrix (Vulkan depth 0..1).
    class Frustum
    {
    public:
        explicit Frustum(const glm::mat4& deviceViewProjection);
        bool Intersects(const Bounds& worldBounds) const;

    private:
        std::array<glm::vec4, 6> m_Planes;
    };
    void ExtractDrawLists(RenderView& view, const Frustum* frustum = nullptr);
    // CPU-only packing shared by rendering and analytic contract tests.
    EngineFrameUniforms MakeLightingUniforms(const SceneInfo& scene);
} // namespace Kans
