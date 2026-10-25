#include "kspch.h"
#include "RenderAssetCache.h"
#include <glad/glad.h>
#include <type_traits>
#include <unordered_set>
#include <glm/gtc/type_ptr.hpp>
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/Platform/OpenGL/OpenGLBuffer.h"
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
        mesh.AuthoredMaterial = data.AuthoredMaterial;
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
        material.Shader = IsStandardShader(data.Shader) || !data.Bindings ? StandardShaderID() : data.Shader;
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
        bindings.Shader = material.Shader;
        bindings.Render = material.Render;
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
                if constexpr (std::is_same_v<Resource, GpuShader>)
                {
                    if (entry.Value)
                        m_RetiredShaders.emplace_back(std::move(entry.Value));
                }
                else if constexpr (std::is_same_v<Resource, Ref<Texture2D>>)
                {
                    if (entry.Value)
                        m_RetiredTextures.emplace_back(std::move(entry.Value));
                }
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
        if (IsStandardShader(id))
        {
            auto& entry         = m_Shaders[StandardShaderID()];
            entry.LastUsedFrame = m_Frame;
            if (!entry.Source)
                entry.Source = GetStandardShaderAsset();
            if (!entry.Value && !entry.ReportedError)
            {
                auto built = GpuShader::Create(entry.Source->GetData());
                if (auto* error = std::get_if<AssetError>(&built))
                {
                    entry.ReportedError = *error;
                    CORE_ERROR("Standard Shader: {}", error->Message);
                }
                else
                    entry.Value = CreateScope<GpuShader>(std::get<GpuShader>(std::move(built)));
            }
            return entry.Value.get();
        }
        const auto  status = m_Assets.Request<ShaderDataAsset>(id);
        const auto* shader =
            Resolve(m_Shaders, id, [](const ShaderDataAsset& asset) { return GpuShader::Create(asset.GetData()); });
        // Shader failures must not expose an older program with a newer CPU layout.
        return status.IsReady() && !m_Shaders.at(id).ReportedError ? shader : nullptr;
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

    const GpuMaterialBindings* RenderAssetCache::GetMaterialBindings(AssetID materialID, bool standard)
    {
        const auto*                  material = GetMaterial(materialID);
        Ref<const MaterialDataAsset> materialSource;
        if (material)
            materialSource = m_Materials.at(materialID).Source;
        if (material && !standard && !IsStandardShader(material->Shader) && GetShader(material->Shader))
            if (const auto* bindings =
                    ResolveBindings(materialID, materialSource, m_Shaders.at(material->Shader).Source, false))
                return bindings;
        if (!GetShader(StandardShaderID()))
            return nullptr;
        return ResolveBindings(materialID, materialSource, GetStandardShaderAsset(), true);
    }

    const GpuMaterialBindings* RenderAssetCache::ResolveBindings(AssetID                      materialID,
                                                                 Ref<const MaterialDataAsset> materialSource,
                                                                 Ref<const ShaderDataAsset>   shaderSource,
                                                                 bool                         standard)
    {
        auto& entry         = (standard ? m_StandardBindings : m_Bindings)[materialID];
        entry.LastUsedFrame = m_Frame;
        // A null Value with unchanged sources is a cached packing failure. The
        // separate Standard entry cannot erase that failure or its source identity.
        if (entry.Material != materialSource || entry.Shader != shaderSource)
        {
            entry.Material = materialSource;
            entry.Shader   = shaderSource;
            if (entry.Value)
                m_RetiredBindings.emplace_back(std::move(entry.Value));
            MaterialData data = materialSource ? materialSource->GetData() : MaterialData {};
            if (standard)
            {
                // A custom recipe addresses another layout. Use the default Standard
                // material in debug views and while that Shader is unavailable.
                if (!IsStandardShader(data.Shader) && data.Bindings)
                {
                    const auto render = data.Render;
                    data = MaterialData {};
                    data.Render       = render;
                }
                auto resolved = ResolveStandardMaterial(data);
                if (auto* error = std::get_if<AssetError>(&resolved))
                {
                    CORE_ERROR("Material {}: {}", materialID.ToString(), error->Message);
                    return nullptr;
                }
                data = std::get<MaterialData>(std::move(resolved));
            }
            auto built = MakeGpuMaterialBindings(data, shaderSource->GetData());
            if (auto* error = std::get_if<AssetError>(&built))
            {
                CORE_ERROR("Material {}: {}", materialID.ToString(), error->Message);
            }
            else
                entry.Value = CreateScope<GpuMaterialBindings>(std::get<GpuMaterialBindings>(std::move(built)));
        }
        return entry.Value.get();
    }

    bool RenderAssetCache::BindMaterialTextures(const GpuShader& shader, const GpuMaterialBindings& bindings)
    {
        bool complete = true;
        for (const auto& [binding, unit] : shader.MaterialSamplerUnits())
        {
            Ref<Texture2D> texture;
            for (const auto& [textureBinding, id] : bindings.Textures)
                if (textureBinding == binding && id.IsValid())
                {
                    const auto status = m_Assets.Request<TextureAsset>(id);
                    texture = GetTexture(id);
                    const auto& entry = m_Textures.at(id);
                    // A failed replacement upload may retain the previous GPU texture.
                    complete &=
                        status.IsReady() && bool(texture) && !entry.ReportedError && entry.Source == status.Asset;
                }
            if (!texture)
            {
                const auto role     = shader.MaterialSamplerFallbacks().at(binding);
                auto&      fallback = m_FallbackTextures[size_t(role)];
                if (!fallback)
                {
                    // Float data represents the flat normal exactly (0.5, 0.5, 1).
                    const std::array<float, 4> pixel =
                        role == ShaderTextureFallback::Normal ? std::array<float, 4> {0.5f, 0.5f, 1, 1} :
                        role == ShaderTextureFallback::Black  ? std::array<float, 4> {0, 0, 0, 1} :
                                                                std::array<float, 4> {1, 1, 1, 1};
                    TextureData image;
                    image.Width = image.Height = 1;
                    image.Format               = TexturePixelFormat::RGBA32Float;
                    image.Usage                = TextureUsage::Data;
                    image.Mips.push_back({std::vector<std::byte>(sizeof(pixel))});
                    std::memcpy(image.Mips[0].Pixels.data(), pixel.data(), sizeof(pixel));
                    auto uploaded = Texture2D::CreateFromProduct(image);
                    if (auto* error = std::get_if<AssetError>(&uploaded))
                    {
                        CORE_ERROR("Material fallback texture: {}", error->Message);
                        complete = false;
                        continue;
                    }
                    fallback = std::get<Ref<Texture2D>>(std::move(uploaded));
                }
                texture = fallback;
            }
            texture->Bind(unit);
        }
        return complete;
    }

    void RenderAssetCache::ReleaseRetiredResources()
    {
        // Deleting restored textures/buffers would unbind them from the caller.
        // Retain only objects still in use, so hidden viewports cannot accumulate edits.
        if (!m_RetiredShaders.empty())
        {
            GLint program;
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            for (auto it = m_RetiredShaders.begin(); it != m_RetiredShaders.end();)
                it = (*it)->GetRendererID() == uint32_t(program) ? std::next(it) : m_RetiredShaders.erase(it);
        }
        if (!m_RetiredTextures.empty())
        {
            std::unordered_set<uint32_t> bound;
            GLint                        active, count, texture;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &count);
            for (GLint unit = 0; unit < count; ++unit)
            {
                glActiveTexture(GL_TEXTURE0 + unit);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
                bound.insert(uint32_t(texture));
            }
            glActiveTexture(active);
            for (auto it = m_RetiredTextures.begin(); it != m_RetiredTextures.end();)
                it = bound.count((**it)->GetRenererID()) ? std::next(it) : m_RetiredTextures.erase(it);
        }
        if (!m_RetiredBindings.empty())
        {
            std::unordered_set<uint32_t> bound;
            GLint                        buffer, count;
            // A caller can bind a cache UBO to other buffer targets too.
            for (auto target : {GL_ARRAY_BUFFER_BINDING,
                                GL_ELEMENT_ARRAY_BUFFER_BINDING,
                                GL_UNIFORM_BUFFER_BINDING,
                                GL_PIXEL_PACK_BUFFER_BINDING,
                                GL_PIXEL_UNPACK_BUFFER_BINDING,
                                GL_COPY_READ_BUFFER_BINDING,
                                GL_COPY_WRITE_BUFFER_BINDING,
                                GL_DRAW_INDIRECT_BUFFER_BINDING,
                                GL_DISPATCH_INDIRECT_BUFFER_BINDING,
                                GL_SHADER_STORAGE_BUFFER_BINDING,
                                GL_ATOMIC_COUNTER_BUFFER_BINDING,
                                GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,
                                GL_TEXTURE_BUFFER_BINDING})
            {
                glGetIntegerv(target, &buffer);
                bound.insert(uint32_t(buffer));
            }
            for (const auto& [target, limit] :
                 {std::pair {GL_UNIFORM_BUFFER_BINDING, GL_MAX_UNIFORM_BUFFER_BINDINGS},
                  std::pair {GL_SHADER_STORAGE_BUFFER_BINDING, GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS},
                  std::pair {GL_ATOMIC_COUNTER_BUFFER_BINDING, GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS},
                  std::pair {GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, GL_MAX_TRANSFORM_FEEDBACK_BUFFERS}})
            {
                glGetIntegerv(limit, &count);
                for (GLint point = 0; point < count; ++point)
                {
                    glGetIntegeri_v(target, point, &buffer);
                    bound.insert(uint32_t(buffer));
                }
            }
            for (auto it = m_RetiredBindings.begin(); it != m_RetiredBindings.end();)
            {
                const bool used = std::any_of((*it)->Blocks.begin(), (*it)->Blocks.end(), [&](const auto& block) {
                    return bound.count(block.second.GetRendererID()) != 0;
                });
                it              = used ? std::next(it) : m_RetiredBindings.erase(it);
            }
        }
    }

    void RenderAssetCache::BeginFrame(uint64_t unusedFrames)
    {
        m_RetiredShaders.clear();
        m_RetiredTextures.clear();
        m_RetiredBindings.clear();
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
        evict(m_StandardBindings);
    }

    void RenderAssetCache::Clear()
    {
        m_RetiredShaders.clear();
        m_RetiredTextures.clear();
        m_RetiredBindings.clear();
        m_Models.clear();
        m_Meshes.clear();
        m_Materials.clear();
        m_Textures.clear();
        m_Shaders.clear();
        m_Bindings.clear();
        m_StandardBindings.clear();
        m_FallbackTextures = {};
    }
} // namespace Kans
