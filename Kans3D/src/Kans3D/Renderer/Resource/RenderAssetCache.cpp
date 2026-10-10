#include "kspch.h"
#include "RenderAssetCache.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include "Kans3D/Platform/OpenGL/OpenGLBuffer.h"
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "Kans3D/Platform/OpenGL/OpenGLVertexArray.h"
#include "Kans3D/Renderer/RHI/RendererAPI.h"

namespace Kans
{
    std::variant<GpuMesh, AssetError> UploadMesh(const MeshData& data)
    {
        auto valid = ValidateMeshData(data);
        if (auto* error = std::get_if<AssetError>(&valid))
            return *error;
        if (RendererAPI::Current() != RendererAPIType::OPENGL)
            return AssetError {AssetErrorCode::NotImplemented, "Mesh upload is unavailable for this backend"};
        if (!glGetString || !glGetString(GL_VERSION))
            return AssetError {AssetErrorCode::InvalidArgument, "Mesh upload requires a current OpenGL context"};

        std::vector<float> packed;
        packed.reserve(data.Vertices.size() * 16);
        for (const auto& vertex : data.Vertices)
        {
            packed.insert(packed.end(), vertex.Position.begin(), vertex.Position.end());
            packed.insert(packed.end(), vertex.Normal.begin(), vertex.Normal.end());
            packed.insert(packed.end(), vertex.UV.begin(), vertex.UV.end());
            packed.insert(packed.end(), vertex.Color.begin(), vertex.Color.end());
            packed.insert(packed.end(), vertex.Tangent.begin(), vertex.Tangent.end());
        }
        auto array = CreateRef<OpenGLVertexArray>();
        array->Bind();
        auto vertices =
            CreateRef<OpenGLVertexBuffer>(packed.data(), static_cast<uint32_t>(packed.size() * sizeof(float)));
        vertices->SetBufferLayout({{ShaderDataType::Float3, "a_Position"},
                                   {ShaderDataType::Float3, "a_Normal"},
                                   {ShaderDataType::Float2, "a_UV"},
                                   {ShaderDataType::Float4, "a_Color"},
                                   {ShaderDataType::Float4, "a_Tangent"}});
        array->AddVertexBuffer(vertices);
        array->SetIndexBuffer(
            CreateRef<OpenGLIndexBuffer>(data.Indices.data(), static_cast<uint32_t>(data.Indices.size())));
        array->Unbind();

        GpuMesh mesh;
        for (const auto& vertex : data.Vertices)
            mesh.LocalBounds.Expand(glm::make_vec3(vertex.Position.data()));
        mesh.VertexArray = std::move(array);
        mesh.IndexCount  = static_cast<uint32_t>(data.Indices.size());
        mesh.Material    = data.Material;
        return mesh;
    }

    std::variant<ModelInstances, AssetError> FlattenModel(const ModelData& data)
    {
        const size_t           count = data.Nodes.size();
        std::vector<glm::mat4> world(count);
        // 0 = unvisited, 1 = in progress, 2 = done.
        std::vector<uint8_t> state(count, 0);
        std::vector<size_t>  chain;
        for (size_t start = 0; start < count; ++start)
        {
            // Walk up to the first resolved ancestor, then resolve back down.
            for (int64_t node = static_cast<int64_t>(start); node >= 0; node = data.Nodes[node].Parent)
            {
                if (static_cast<size_t>(node) >= count || state[node] == 1)
                    return AssetError {AssetErrorCode::InvalidData, "Model node hierarchy is not a tree"};
                if (state[node] == 2)
                    break;
                state[node] = 1;
                chain.push_back(static_cast<size_t>(node));
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                const auto& node = data.Nodes[*it];
                // LocalTransform is row-major; glm matrices are column-major.
                const glm::mat4 local = glm::transpose(glm::make_mat4(node.LocalTransform.data()));
                world[*it]            = node.Parent < 0 ? local : world[node.Parent] * local;
                state[*it]            = 2;
            }
            chain.clear();
        }
        ModelInstances result;
        for (size_t i = 0; i < count; ++i)
            for (AssetID mesh : data.Nodes[i].Meshes)
                result.Instances.push_back({mesh, world[i]});
        return result;
    }

    GpuMaterial MakeGpuMaterial(const MaterialData& data)
    {
        GpuMaterial material;
        material.BaseColor = glm::make_vec4(data.BaseColor.data());
        material.Metallic  = data.Metallic;
        material.Roughness = data.Roughness;
        if (data.Bindings)
            material.Shader = data.Shader;
        // The first texture for each slot wins; preferred semantics are listed first.
        using S   = MaterialTextureSemantic;
        auto pick = [&](AssetID& slot, std::initializer_list<S> semantics) {
            for (S semantic : semantics)
                for (const auto& binding : data.Textures)
                    if (binding.Semantic == static_cast<uint32_t>(semantic) && binding.UVChannel == 0 &&
                        binding.Texture.IsValid())
                    {
                        slot = binding.Texture;
                        return;
                    }
        };
        pick(material.AlbedoTexture, {S::BaseColor, S::Diffuse});
        // OBJ files commonly store normal maps as bump (Height) maps.
        pick(material.NormalTexture, {S::Normal, S::NormalCamera, S::Height});
        // Assimp 5 reports the glTF metallic-roughness texture as Unknown.
        pick(material.MetallicRoughnessTexture, {S::MetallicRoughness, S::Unknown, S::DiffuseRoughness});
        pick(material.OcclusionTexture, {S::AmbientOcclusion, S::Lightmap});
        pick(material.EmissiveTexture, {S::EmissionColor, S::Emissive});
        return material;
    }

    std::variant<GpuMaterialBindings, AssetError> MakeGpuMaterialBindings(const MaterialData& material,
                                                                          const ShaderData&   shader)
    {
        if (!material.Bindings)
            return AssetError {AssetErrorCode::InvalidArgument, "Material has no Shader bindings"};
        auto packed = PackMaterialUniforms(material, shader.Code, *material.Bindings);
        if (auto* error = std::get_if<AssetError>(&packed))
            return std::move(*error);
        GpuMaterialBindings bindings;
        for (const auto& block : std::get<std::vector<MaterialUniformBlock>>(packed))
            bindings.Blocks.emplace_back(block.Binding, GpuUniformBuffer(block.Bytes.size(), block.Bytes.data()));
        for (const auto& texture : material.Bindings->Textures)
            bindings.Textures.emplace_back(texture.Binding, material.Textures[texture.TextureIndex].Texture);
        return bindings;
    }

    template<typename T, typename Resource, typename Build>
    Resource*
    RenderAssetCache::Resolve(std::unordered_map<AssetID, Entry<T, Resource>>& entries, AssetID id, Build&& build)
    {
        if (!id.IsValid())
            return nullptr;
        auto& entry         = entries[id];
        entry.LastUsedFrame = m_Frame;
        auto status         = m_Assets.Request<T>(id);
        if (status.State == AssetLoadState::Ready && status.Asset != entry.Source)
        {
            entry.Source = status.Asset;
            auto built   = build(*status.Asset);
            if (auto* error = std::get_if<AssetError>(&built))
                status.Error = std::move(*error);
            else
            {
                entry.Value = CreateScope<Resource>(std::get<Resource>(std::move(built)));
                entry.ReportedError.reset();
            }
        }
        if (status.Error && (!entry.ReportedError || entry.ReportedError->Message != status.Error->Message))
        {
            CORE_ERROR("Asset {}: {}", id.ToString(), status.Error->Message);
            entry.ReportedError = status.Error;
        }
        return entry.Value.get();
    }

    const ModelInstances* RenderAssetCache::GetModel(AssetID id)
    {
        return Resolve(m_Models, id, [](const ModelAsset& asset) { return FlattenModel(asset.GetData()); });
    }

    const GpuMesh* RenderAssetCache::GetMesh(AssetID id)
    {
        return Resolve(m_Meshes, id, [](const MeshAsset& asset) { return UploadMesh(asset.GetData()); });
    }

    const GpuMaterial* RenderAssetCache::GetMaterial(AssetID id)
    {
        return Resolve(m_Materials, id, [](const MaterialDataAsset& asset) {
            return std::variant<GpuMaterial, AssetError>(MakeGpuMaterial(asset.GetData()));
        });
    }

    Ref<Texture2D> RenderAssetCache::GetTexture(AssetID id)
    {
        auto* texture = Resolve(m_Textures, id, [](const TextureAsset& asset) {
            auto uploaded = Texture2D::CreateFromProduct(asset.GetData());
            if (auto* error = std::get_if<AssetError>(&uploaded))
                return std::variant<Ref<Texture2D>, AssetError>(std::move(*error));
            return std::variant<Ref<Texture2D>, AssetError>(std::get<Ref<Texture2D>>(std::move(uploaded)));
        });
        return texture ? *texture : nullptr;
    }

    const GpuShader* RenderAssetCache::GetShader(AssetID id)
    {
        return Resolve(m_Shaders, id, [](const ShaderDataAsset& asset) { return GpuShader::Create(asset.GetData()); });
    }

    Bounds RenderAssetCache::GetModelBounds(AssetID model)
    {
        Bounds bounds;
        if (const auto* instances = GetModel(model))
            for (const auto& instance : instances->Instances)
                if (const auto* mesh = GetMesh(instance.Mesh))
                    bounds.Expand(mesh->LocalBounds.Transformed(instance.Transform));
        return bounds;
    }

    const GpuMaterialBindings* RenderAssetCache::GetMaterialBindings(AssetID materialID)
    {
        const GpuMaterial* material = GetMaterial(materialID);
        if (!material || !material->Shader.IsValid() || !GetShader(material->Shader))
            return nullptr;
        const auto& materialSource = m_Materials.at(materialID).Source;
        const auto& shaderSource   = m_Shaders.at(material->Shader).Source;
        auto&       entry          = m_Bindings[materialID];
        entry.LastUsedFrame        = m_Frame;
        if (entry.Material != materialSource || entry.Shader != shaderSource)
        {
            entry.Material = materialSource;
            entry.Shader   = shaderSource;
            entry.Value.reset();
            auto built = MakeGpuMaterialBindings(materialSource->GetData(), shaderSource->GetData());
            if (auto* error = std::get_if<AssetError>(&built))
                CORE_ERROR("Material {}: {}", materialID.ToString(), error->Message);
            else
                entry.Value = CreateScope<GpuMaterialBindings>(std::get<GpuMaterialBindings>(std::move(built)));
        }
        return entry.Value.get();
    }

    void RenderAssetCache::BeginFrame(uint64_t unusedFrames)
    {
        ++m_Frame;
        auto evict = [&](auto& entries) {
            for (auto it = entries.begin(); it != entries.end();)
                it = m_Frame - it->second.LastUsedFrame > unusedFrames ? entries.erase(it) : std::next(it);
        };
        evict(m_Models);
        evict(m_Meshes);
        evict(m_Materials);
        evict(m_Textures);
        evict(m_Shaders);
        evict(m_Bindings);
    }

    void RenderAssetCache::Clear()
    {
        m_Models.clear();
        m_Meshes.clear();
        m_Materials.clear();
        m_Textures.clear();
        m_Shaders.clear();
        m_Bindings.clear();
    }
} // namespace Kans
