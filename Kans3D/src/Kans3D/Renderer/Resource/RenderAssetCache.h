#pragma once
#include <unordered_map>
#include <glm/glm.hpp>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/Asset/Runtime/TextureData.h"
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"
#include "Kans3D/Renderer/Resource/Texture.h"

namespace Kans
{
    struct GpuMesh
    {
        Ref<VertexArray> VertexArray;
        uint32_t         IndexCount = 0;
        AssetID          Material;
    };

    // Engine PBR material parameters. Texture IDs are resolved when drawing so a
    // material is usable before (or without) its textures.
    struct GpuMaterial
    {
        glm::vec4 BaseColor {1.0f};
        float     Metallic  = 0.0f;
        float     Roughness = 1.0f;
        AssetID   AlbedoTexture;
        AssetID   NormalTexture;
        AssetID   MetallicRoughnessTexture;
        AssetID   OcclusionTexture;
        AssetID   EmissiveTexture;
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

        const ModelInstances* GetModel(AssetID id);
        const GpuMesh*        GetMesh(AssetID id);
        const GpuMaterial*    GetMaterial(AssetID id);
        Ref<Texture2D>        GetTexture(AssetID id);

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

        AssetManager&                                                      m_Assets;
        uint64_t                                                           m_Frame = 0;
        std::unordered_map<AssetID, Entry<ModelAsset, ModelInstances>>     m_Models;
        std::unordered_map<AssetID, Entry<MeshAsset, GpuMesh>>             m_Meshes;
        std::unordered_map<AssetID, Entry<MaterialDataAsset, GpuMaterial>> m_Materials;
        std::unordered_map<AssetID, Entry<TextureAsset, Ref<Texture2D>>>   m_Textures;
    };

    // Uploads CPU mesh data to an indexed vertex array (requires a current GL context).
    [[nodiscard]] std::variant<GpuMesh, AssetError> UploadMesh(const MeshData& data);
    // Converts a Model hierarchy to per-mesh model-space transforms.
    [[nodiscard]] std::variant<ModelInstances, AssetError> FlattenModel(const ModelData& data);
    // Maps imported material parameters and texture semantics to the engine PBR material.
    GpuMaterial MakeGpuMaterial(const MaterialData& data);
} // namespace Kans
