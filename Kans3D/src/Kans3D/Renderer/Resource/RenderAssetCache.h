#pragma once
#include <unordered_map>
#include <glm/glm.hpp>
#include "Kans3D/Core/Math/Bounds.h"
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/Asset/Runtime/TextureData.h"
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"
#include "Kans3D/Renderer/Resource/GpuShader.h"
#include "Kans3D/Renderer/Resource/Texture.h"

namespace Kans
{
    struct GpuMesh
    {
        // RHI-MIGRATION: immutable VAO view for picking/outline and the material preview.
        Ref<VertexArray> VertexArray;
        RenderHardware::Handle<RenderHardware::Buffer> Vertices, Indices;
        std::vector<RenderHardware::VertexBinding>     VertexBindings {{0, 64, false}};
        std::vector<RenderHardware::VertexAttribute>   Attributes {{0, 0, 0, RenderHardware::VertexFormat::Float3},
                                                                   {1, 0, 12, RenderHardware::VertexFormat::Float3},
                                                                   {2, 0, 24, RenderHardware::VertexFormat::Float2},
                                                                   {3, 0, 32, RenderHardware::VertexFormat::Float4},
                                                                   {4, 0, 48, RenderHardware::VertexFormat::Float4}};
        uint32_t         IndexCount = 0;
        AssetID          Material;
        AssetID          AuthoredMaterial;
        Bounds           LocalBounds; // In mesh space.
    };

    struct GpuMaterial
    {
        AssetID Shader;
    };

    // Material data packed for its Shader: set-1 uniform blocks and textures.
    struct GpuMaterialBindings
    {
        mutable RenderHardware::Handle<RenderHardware::BindGroup>            Group;
        mutable std::vector<RenderHardware::Handle<RenderHardware::Texture>> Images;
        AssetID Shader; // Actual program, including Standard while a custom Shader is unavailable.
        MaterialRenderState Render;
        std::vector<std::pair<uint32_t, GpuUniformBuffer>> Blocks;
        std::vector<std::pair<uint32_t, AssetID>>          Textures;
    };

    struct ModelInstance
    {
        AssetID   Mesh;
        glm::mat4 Transform {1.0f}; // Model-space transform of the node.
    };

    // Flattened node hierarchy of a Model asset.
    struct ModelInstances
    {
        std::vector<ModelInstance> Instances;
    };

    // Render-thread cache from runtime CPU assets to GPU resources.
    //
    // Assets are requested from the AssetManager without blocking. While a newer
    // version of an asset is loading, the last uploaded resource stays in use.
    // Resources are rebuilt when the AssetManager returns a different CPU object.
    class RenderAssetCache
    {
    public:
        explicit RenderAssetCache(AssetManager& assets, std::shared_ptr<RenderHardware::Device> device = {}) :
            m_Device(std::move(device)), m_Assets(assets)
        {}
        std::shared_ptr<RenderHardware::Device>         GetDevice();
        RenderHardware::Handle<RenderHardware::Texture> GetTextureResource(AssetID id);
        // Sampling belongs to an independent RHI sampler, never mutable native image state.
        void SetTextureSampler(AssetID id, const RenderHardware::SamplerDesc& desc);
        RenderHardware::Handle<RenderHardware::BindGroup>
        GetMaterialGroup(const GpuShader& shader, const GpuMaterialBindings& bindings, bool* complete = nullptr);

        // Evicts resources not used for `unusedFrames` frames.
        void BeginFrame(uint64_t unusedFrames = 600);
        void Clear();
        // RHI-MIGRATION: compatibility retirement after legacy preview state restoration.
        void ReleaseRetiredResources();

        const ModelInstances* GetModel(AssetID id);
        const GpuMesh*        GetMesh(AssetID id);
        const GpuMaterial*    GetMaterial(AssetID id);
        Ref<Texture2D>        GetTexture(AssetID id);
        const GpuShader*      GetShader(AssetID id);
        // Model-space bounds of a model's meshes; empty until the model and its
        // meshes are uploaded.
        Bounds GetModelBounds(AssetID model);
        // Packed material parameters, using Standard for missing/pending/failed
        // custom Shaders. `standard` forces Standard for viewport debug views.
        const GpuMaterialBindings* GetMaterialBindings(AssetID material, bool standard = false);
        // Returns false if an assigned texture is not ready (fallbacks still bind).
        bool BindMaterialTextures(const GpuShader& shader, const GpuMaterialBindings& bindings);

        AssetManager& GetAssetManager() { return m_Assets; }

    private:
        template<typename T, typename Resource>
        struct Entry
        {
            Ref<const T>              Source;
            Scope<Resource>           Value;
            uint64_t                  LastUsedFrame = 0;
            std::optional<AssetError> ReportedError;
        };
        template<typename T, typename Resource, typename Build>
        Resource* Resolve(std::unordered_map<AssetID, Entry<T, Resource>>& entries, AssetID id, Build&& build);

        struct BindingsEntry
        {
            Ref<const MaterialDataAsset>  Material;
            Ref<const ShaderDataAsset>    Shader;
            Scope<GpuMaterialBindings>    Value;
            uint64_t                      LastUsedFrame = 0;
        };

        const GpuMaterialBindings* ResolveBindings(AssetID                      materialID,
                                                   Ref<const MaterialDataAsset> materialSource,
                                                   Ref<const ShaderDataAsset>   shaderSource,
                                                   bool                         standard);

        std::shared_ptr<RenderHardware::Device>                                      m_Device;
        RenderHardware::Handle<RenderHardware::Sampler>                              m_MaterialSampler;
        std::unordered_map<AssetID, RenderHardware::Handle<RenderHardware::Sampler>> m_TextureSamplers;
        std::array<Ref<Texture2D>, 3>                                      m_FallbackTextures;
        AssetManager&                                                      m_Assets;
        uint64_t                                                           m_Frame = 0;
        std::unordered_map<AssetID, Entry<ModelAsset, ModelInstances>>     m_Models;
        std::unordered_map<AssetID, Entry<MeshAsset, GpuMesh>>             m_Meshes;
        std::unordered_map<AssetID, Entry<MaterialDataAsset, GpuMaterial>> m_Materials;
        std::unordered_map<AssetID, Entry<TextureAsset, Ref<Texture2D>>>   m_Textures;
        std::unordered_map<AssetID, Entry<ShaderDataAsset, GpuShader>>     m_Shaders;
        std::unordered_map<AssetID, BindingsEntry>                         m_Bindings;
        std::unordered_map<AssetID, BindingsEntry>                         m_StandardBindings;
    };

    // Uploads CPU mesh data to RHI buffers and exposes an immutable legacy VAO view.
    [[nodiscard]] std::variant<GpuMesh, AssetError> UploadMesh(const MeshData&                         data,
                                                               std::shared_ptr<RenderHardware::Device> device = {});
    // Converts a Model hierarchy to per-mesh model-space transforms.
    [[nodiscard]] std::variant<ModelInstances, AssetError> FlattenModel(const ModelData& data);
    // Maps imported material parameters and texture semantics to the engine PBR material.
    GpuMaterial MakeGpuMaterial(const MaterialData& data);
    // Packs a material into RHI uniform buffers; group 1 is resolved lazily with its textures.
    [[nodiscard]] std::variant<GpuMaterialBindings, AssetError>
    MakeGpuMaterialBindings(const MaterialData&                     material,
                            const ShaderData&                       shader,
                            std::shared_ptr<RenderHardware::Device> device = {});
} // namespace Kans
