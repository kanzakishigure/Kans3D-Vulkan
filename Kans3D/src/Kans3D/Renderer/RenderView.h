#pragma once
#include <functional>
#include <optional>
#include <glm/glm.hpp>
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Renderer/Resource/RenderScene.h"
#include "Kans3D/Scene/Components.h"
#include "ImmediatePassState.h"

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
    };
    struct ViewEnvironment
    {
        RenderHardware::Handle<RenderHardware::Texture> IrradianceMap, RadianceMap;
    };
    // One view/frame. Owns intent, CPU sort metadata and resource handles, never borrowed meshes.
    // Matrices remain in the engine's coordinate convention until recording.
    struct RenderView
    {
        SceneInfo                          Scene {};
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
        std::vector<ViewSpriteBatch>       SpriteBatches; // Compatibility with direct Renderer2D callers.
        std::vector<ViewImmediate>         ImmediateOrder;
        ViewEnvironment                    EnvironmentMaps;
        RenderHardware::Handle<RenderHardware::Texture> BrdfLUT;
        std::optional<PrefabPreview>                    Preview;
    };
    // CPU-only extraction of an available mesh; absent data emits no draws.
    void
    ExtractMeshDraws(RenderView& view, const ViewMeshSubmission& submission, SceneViewMode mode, const GpuMesh* mesh);
    void ExtractDrawLists(RenderView& view);
    // CPU-only packing shared by rendering and analytic contract tests.
    EngineFrameUniforms MakeLightingUniforms(const SceneInfo& scene);
} // namespace Kans
