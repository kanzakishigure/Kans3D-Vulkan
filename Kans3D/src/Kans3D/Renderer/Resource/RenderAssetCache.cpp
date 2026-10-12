#include "kspch.h"
#include "RenderAssetCache.h"
#include <type_traits>
#include <unordered_set>
#include <glm/gtc/type_ptr.hpp>
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/RHI/Backend.h"

namespace Kans
{
    std::variant<GpuMesh, AssetError> UploadMesh(const MeshData& data, std::shared_ptr<RenderHardware::Device> device)
    {
        auto valid = ValidateMeshData(data);
        if (auto* error = std::get_if<AssetError>(&valid))
            return *error;
        if (!device)
        {
            if (RenderHardware::GetBackend() != RenderHardware::Backend::OpenGL)
                return AssetError {AssetErrorCode::NotImplemented, "Mesh upload is unavailable for this backend"};
            try
            {
                device = RenderHardware::GetSharedDevice();
            }
            catch (const std::exception& e)
            {
                return AssetError {AssetErrorCode::InvalidArgument, e.what()};
            }
        }
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
        GpuMesh mesh;
        mesh.Device = device;
        for (const auto& vertex : data.Vertices)
            mesh.LocalBounds.Expand(glm::make_vec3(vertex.Position.data()));
        mesh.Vertices = device->CreateBuffer(
            {packed.size() * sizeof(float), RenderHardware::BufferUsage::Vertex, RenderHardware::MemoryUsage::Upload});
        mesh.Vertices->Write(0, packed.data(), packed.size() * sizeof(float));
        mesh.Indices = device->CreateBuffer({data.Indices.size() * sizeof(uint32_t),
                                             RenderHardware::BufferUsage::Index,
                                             RenderHardware::MemoryUsage::Upload});
        mesh.Indices->Write(0, data.Indices.data(), data.Indices.size() * sizeof(uint32_t));
        // Two endpoints per indexed vertex preserve the legacy per-triangle normal lines.
        std::vector<float> lines;
        lines.reserve(data.Indices.size() * 14);
        for (uint32_t index : data.Indices)
            for (float end : {0.0f, 1.0f})
            {
                const auto& v = data.Vertices[index];
                lines.insert(lines.end(), v.Position.begin(), v.Position.end());
                lines.insert(lines.end(), v.Normal.begin(), v.Normal.end());
                lines.push_back(end);
            }
        mesh.NormalLines = device->CreateBuffer(
            {lines.size() * sizeof(float), RenderHardware::BufferUsage::Vertex, RenderHardware::MemoryUsage::Upload});
        mesh.NormalLines->Write(0, lines.data(), lines.size() * sizeof(float));
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

    std::variant<GpuMaterialBindings, AssetError>
    MakeGpuMaterialBindings(const MaterialData&                     material,
                            const ShaderData&                       shader,
                            std::shared_ptr<RenderHardware::Device> device)
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
            bindings.Blocks.emplace_back(block.Binding,
                                         GpuUniformBuffer(block.Bytes.size(), block.Bytes.data(), device));
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
        return Resolve(m_Meshes, id, [&](const MeshAsset& asset) { return UploadMesh(asset.GetData(), GetDevice()); });
    }

    const GpuMaterial* RenderAssetCache::GetMaterial(AssetID id)
    {
        return Resolve(m_Materials, id, [](const MaterialDataAsset& asset) {
            return std::variant<GpuMaterial, AssetError>(MakeGpuMaterial(asset.GetData()));
        });
    }

    std::shared_ptr<RenderHardware::Device> RenderAssetCache::GetDevice()
    {
        if (!m_Device)
            m_Device = RenderHardware::GetSharedDevice();
        return m_Device;
    }
    void RenderAssetCache::SetTextureSampler(AssetID id, const RenderHardware::SamplerDesc& desc)
    {
        m_TextureSamplers[id] = GetDevice()->CreateSampler(desc);
        for (auto* entries : {&m_Bindings, &m_StandardBindings})
            for (auto& [key, entry] : *entries)
                if (entry.Value)
                    entry.Value->Group.reset();
    }
    Ref<Texture2D> RenderAssetCache::GetTexture(AssetID id)
    {
        auto* texture = Resolve(m_Textures, id, [&](const TextureAsset& asset) {
            auto uploaded = Texture2D::CreateFromProduct(asset.GetData(), GetDevice());
            if (std::holds_alternative<Ref<Texture2D>>(uploaded))
            {
                // Drop obsolete cached descriptors. Recorded commands still retain their snapshots.
                for (auto* entries : {&m_Bindings, &m_StandardBindings})
                    for (auto& [key, entry] : *entries)
                        if (entry.Value)
                        {
                            entry.Value->Group.reset();
                            entry.Value->Images.clear();
                        }
            }
            return uploaded;
        });
        return texture ? *texture : nullptr;
    }
    RenderHardware::Handle<RenderHardware::Texture> RenderAssetCache::GetTextureResource(AssetID id)
    {
        auto view = GetTexture(id);
        return view ? view->GetResource() : nullptr;
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
                auto built = GpuShader::Create(entry.Source->GetData(), GetDevice());
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
        const auto* shader = Resolve(m_Shaders, id, [&](const ShaderDataAsset& asset) {
            return GpuShader::Create(asset.GetData(), GetDevice());
        });
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
            entry.Value.reset();
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
            auto built = MakeGpuMaterialBindings(data, shaderSource->GetData(), GetDevice());
            if (auto* error = std::get_if<AssetError>(&built))
            {
                CORE_ERROR("Material {}: {}", materialID.ToString(), error->Message);
            }
            else
                entry.Value = CreateScope<GpuMaterialBindings>(std::get<GpuMaterialBindings>(std::move(built)));
        }
        return entry.Value.get();
    }

    RenderHardware::Handle<RenderHardware::BindGroup>
    RenderAssetCache::GetMaterialGroup(const GpuShader& shader, const GpuMaterialBindings& bindings, bool* result)
    {
        bool complete = true;
        RenderHardware::BindGroupDesc group;
        group.Layout = shader.MaterialLayout;
        std::vector<RenderHardware::Handle<RenderHardware::Texture>> images;
        if (!m_MaterialSampler)
            m_MaterialSampler = GetDevice()->CreateSampler();
        for (const auto& [binding, buffer] : bindings.Blocks)
            group.Entries.push_back({binding, buffer.Resource()});
        for (const auto& [binding, unit] : shader.MaterialSamplerFallbacks())
        {
            Ref<Texture2D> texture;
            auto           sampler = m_MaterialSampler;
            for (const auto& [textureBinding, id] : bindings.Textures)
                if (textureBinding == binding && id.IsValid())
                {
                    const auto status = m_Assets.Request<TextureAsset>(id);
                    texture = GetTexture(id);
                    if (auto found = m_TextureSamplers.find(id); found != m_TextureSamplers.end())
                        sampler = found->second;
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
                    auto uploaded = Texture2D::CreateFromProduct(image, GetDevice());
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
            auto image = texture->GetResource();
            images.push_back(image);
            RenderHardware::BindGroupEntry entry;
            entry.Slot            = binding;
            entry.TextureResource = image;
            entry.SamplerResource = sampler;
            group.Entries.push_back(std::move(entry));
        }
        if (result)
            *result = complete;
        if (group.Entries.size() != group.Layout->GetDesc().Entries.size())
        {
            bindings.Images = std::move(images);
            return nullptr; // Compatibility texture-only binding has no uniform blocks.
        }
        if (!bindings.Group || bindings.Images != images)
        {
            bindings.Group  = GetDevice()->CreateBindGroup(group);
            bindings.Images = std::move(images);
        }
        return bindings.Group;
    }

    void RenderAssetCache::BeginFrame(uint64_t unusedFrames)
    {

        if (m_Device)
            m_Device->WaitIdle();
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

        m_Models.clear();
        m_Meshes.clear();
        m_Materials.clear();
        m_Textures.clear();
        m_Shaders.clear();
        m_Bindings.clear();
        m_StandardBindings.clear();
        m_FallbackTextures = {};
        m_MaterialSampler.reset();
        m_TextureSamplers.clear();
    }
} // namespace Kans
