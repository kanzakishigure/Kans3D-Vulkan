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
        Ref<VertexArray> VertexArray;
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
        explicit RenderAssetCache(AssetManager& assets) : m_Assets(assets) {}

        // Evicts resources not used for `unusedFrames` frames.
        void BeginFrame(uint64_t unusedFrames = 600);
        void Clear();
        // Call after restoring caller GL state. Bound retired objects stay alive until
        // a later release or BeginFrame; unbound replacements are freed immediately.
        // Requires the render thread and its current GL context. Does not advance the frame.
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
        // Keep replaced programs, textures and UBOs alive through GL state restoration.
        // Preview completion releases unbound objects; BeginFrame/Clear releases all.
        std::vector<Scope<GpuShader>>           m_RetiredShaders;
        std::vector<Scope<Ref<Texture2D>>>      m_RetiredTextures;
        std::vector<Scope<GpuMaterialBindings>> m_RetiredBindings;
    };

    // Uploads CPU mesh data to an indexed vertex array (requires a current GL context).
    [[nodiscard]] std::variant<GpuMesh, AssetError> UploadMesh(const MeshData& data);
    // Converts a Model hierarchy to per-mesh model-space transforms.
    [[nodiscard]] std::variant<ModelInstances, AssetError> FlattenModel(const ModelData& data);
    // Maps imported material parameters and texture semantics to the engine PBR material.
    GpuMaterial MakeGpuMaterial(const MaterialData& data);
    // Packs a Shader material's uniform blocks into GL buffers (requires a current GL context).
    [[nodiscard]] std::variant<GpuMaterialBindings, AssetError> MakeGpuMaterialBindings(const MaterialData& material,
                                                                                        const ShaderData&   shader);
} // namespace Kans
