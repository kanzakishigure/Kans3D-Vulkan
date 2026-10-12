#include "kspch.h"

#include <iostream>
#include <string>
#include <glm/gtc/packing.hpp>
#include "Kans3D/Asset/Importer/TextureSource.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/Resource/InternalShader.h"
#include "Kans3D/Renderer/Resource/MaterialPass.h"
#include "Kans3D/Renderer/Resource/MeshFactory.h"
#include "Kans3D/Renderer/Resource/SceneShaders.h"
#include "Kans3D/Renderer/SceneRenderer.h"

namespace Kans
{
    namespace
    {
        using namespace RenderHardware;
        struct SceneDrawData
        {
            glm::mat4 ViewProjection {1}, Transform {1}, DeviceClip {1};
            glm::vec4 ViewPos {0}, Settings {0};
        };
        struct CaptureData
        {
            glm::mat4 Projection {1}, View {1};
            glm::vec4 Parameters {0};
        };
        Handle<RenderHardware::Shader> Build(Device& device, const char* name, const char* vertex, const char* fragment)
        {
            auto result = CreateInternalShader(device, name, vertex, fragment);
            if (auto* error = std::get_if<AssetError>(&result))
            {
                if (Log::GetCoreLogger())
                    CORE_ERROR("{}", error->Message);
                else
                    std::cerr << error->Message << std::endl;
                return {};
            }
            return std::get<Handle<RenderHardware::Shader>>(std::move(result));
        }
        template<class T>
        Handle<BindGroup> DrawGroup(Device&                         device,
                                    const Handle<BindGroupLayout>&  layout,
                                    const T&                        data,
                                    Handle<RenderHardware::Texture> texture = {},
                                    Handle<Sampler>                 sampler = {})
        {
            auto buffer = device.CreateBuffer({sizeof(T), BufferUsage::Uniform, MemoryUsage::Upload});
            buffer->Write(0, &data, sizeof(T));
            BindGroupDesc group;
            group.Layout = layout;
            group.Entries.push_back({0, buffer, 0, sizeof(T), {}, {}});
            if (texture)
                group.Entries.push_back({1, {}, 0, 0, std::move(texture), std::move(sampler)});
            return device.CreateBindGroup(group);
        }
        uint32_t Mips(uint32_t size)
        {
            uint32_t levels = 1;
            while (size > 1)
            {
                size >>= 1;
                ++levels;
            }
            return levels;
        }
    } // namespace

    SceneRenderer::SceneRenderer(RenderScene* scene, SceneRendererSpecification spec) :
        m_RenderScene(scene), m_Specification(spec)
    {

        Init();
    }

    SceneRenderer::~SceneRenderer()
    {
        if (m_Active)
            m_Device->EndFrame();
        m_Device->WaitIdle();
    }

    void SceneRenderer::SetAssetManager(AssetManager* assets)
    {
        m_AssetCache = assets ? CreateScope<RenderAssetCache>(*assets, m_Device) : nullptr;
    }

    void
    SceneRenderer::SubmitModel(const ProductModelComponent& model, const glm::mat4& transform, ModelStencilMode stencil)
    {
        if (!m_AssetCache)
            return;
        const ModelInstances* instances = m_AssetCache->GetModel(model.Model);
        if (!instances)
            return;
        for (const auto& instance : instances->Instances)
            if (const auto* mesh = m_AssetCache->GetMesh(instance.Mesh))
                m_ModelDraws.push_back({instance.Mesh,
                                        transform * instance.Transform,
                                        model.ResolveMaterial(instance.Mesh, mesh->Material, mesh->AuthoredMaterial),
                                        m_ViewMode,
                                        stencil});
    }

    void SceneRenderer::DrawShaderMaterials(const std::vector<ShaderDraw>& draws)
    {
        if (!m_AssetCache || draws.empty())
            return;
        struct ReadyDraw
        {
            const ShaderDraw*   Draw;
            MaterialRenderState Render;
            float               DistanceSquared;
        };
        std::vector<ReadyDraw> ready;
        for (const auto& draw : draws)
        {
            const auto* mesh = m_AssetCache->GetMesh(draw.Mesh);
            const auto* bindings =
                m_AssetCache->GetMaterialBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            if (!mesh || !bindings)
                continue;
            const glm::vec3 center = glm::vec3(draw.Transform * glm::vec4(mesh->LocalBounds.Center(), 1));
            const glm::vec3 offset = center - m_SceneInfo.sceneCamera.Position;
            ready.push_back({&draw, bindings->Render, glm::dot(offset, offset)});
        }
        std::stable_sort(ready.begin(), ready.end(), [](const ReadyDraw& a, const ReadyDraw& b) {
            const bool aBlend = a.Render.AlphaMode == MaterialAlphaMode::Blend;
            const bool bBlend = b.Render.AlphaMode == MaterialAlphaMode::Blend;
            if (aBlend != bBlend)
                return !aBlend;
            return aBlend && a.DistanceSquared > b.DistanceSquared;
        });

        EngineFrameUniforms frame;
        frame.ViewProjection =
            m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        const auto coordinates = m_Device->GetCoordinateConventions();
        frame.ViewProjection   = DeviceProjection(frame.ViewProjection, coordinates);
        frame.CameraPosition = glm::vec4(m_SceneInfo.sceneCamera.Position, 1.0f);
        for (uint32_t i = 0; i < EngineLightCount; ++i)
        {
            frame.LightPositions[i] = glm::vec4(m_SceneInfo.pointLight.Position, 1.0f);
            frame.LightColors[i]    = glm::vec4(m_SceneInfo.pointLight.Ambient_Intensity, 1.0f);
            frame.LightDiffuse[i]   = glm::vec4(m_SceneInfo.pointLight.Diffuse_Intensity, 1.0f);
        }
        frame.Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
        using namespace RenderHardware;
        const size_t alignment   = m_Device->GetBufferBindingAlignment(BindingType::UniformBuffer);
        const auto   align       = [alignment](size_t n) { return (n + alignment - 1) / alignment * alignment; };
        const size_t frameStride = align(sizeof(frame)), objectStride = align(sizeof(EngineObjectUniforms));
        // Each draw owns disjoint immutable ranges until command/frame retirement.
        if (ready.empty())
            return;
        auto frames  = m_Device->CreateBuffer({frameStride * ready.size(), BufferUsage::Uniform, MemoryUsage::Upload});
        auto objects = m_Device->CreateBuffer({objectStride * ready.size(), BufferUsage::Uniform, MemoryUsage::Upload});
        auto commands = m_Device->CreateCommandList();
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight), 0, 1});
        commands->SetScissor({0, 0, m_ViewportWidth, m_ViewportHeight});
        const auto           environment = GetMaterialEnvironment();
        size_t               range = 0, frameRange = 0;
        std::optional<float> uploadedDebugView;
        for (const auto& item : ready)
        {
            const auto& draw = *item.Draw;
            // Requests may finish during this pass. Snapshot the state used for sorting,
            // but acquire GPU resources at the draw so a cache replacement cannot dangle a queued pointer.
            const auto* mesh = m_AssetCache->GetMesh(draw.Mesh);
            const auto* bindings =
                m_AssetCache->GetMaterialBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            const auto* shader = bindings ? m_AssetCache->GetShader(bindings->Shader) : nullptr;
            if (!mesh || !bindings || !shader)
                continue;
            const auto&    render      = item.Render;
            frame.DebugView = draw.ViewMode == SceneViewMode::BaseColor ? 1.0f :
                              draw.ViewMode == SceneViewMode::Normals   ? 2.0f :
                                                                          0.0f;
            if (!uploadedDebugView || *uploadedDebugView != frame.DebugView)
            {
                if (uploadedDebugView)
                    ++frameRange;
                frames->Write(frameRange * frameStride, &frame, sizeof(frame));
                uploadedDebugView = frame.DebugView;
            }
            EngineObjectUniforms object;
            object.Transform      = draw.Transform;
            object.NormalMatrix   = glm::transpose(glm::inverse(draw.Transform));
            object.MaterialRender = {float(render.AlphaMode), render.AlphaCutoff, render.DoubleSided ? 1.0f : 0.0f, 0};
            objects->Write(range * objectStride, &object, sizeof(object));
            MaterialPassData pass;
            pass.Frame              = frames;
            pass.Object             = objects;
            pass.FrameOffset        = frameRange * frameStride;
            pass.ObjectOffset       = range * objectStride;
            pass.Environment        = environment;
            pass.EnvironmentSampler = m_IBLSampler;
            pass.ColorFormats       = {Format::RGBA8Unorm, Format::RGBA8Unorm};
            pass.Wireframe          = draw.ViewMode == SceneViewMode::Wireframe;
            pass.WriteStencil       = draw.Stencil == ModelStencilMode::WriteReference;
            pass.Render             = render;
            RecordMaterialDraw(*m_AssetCache, *commands, *shader, *bindings, *mesh, pass);
            ++m_Stats.DrawCalls;
            m_Stats.Triangles += mesh->IndexCount / 3;
            ++range;
        }
        commands->EndRendering();
        m_Device->Submit(commands);
    }

    std::array<RenderHardware::Handle<RenderHardware::Texture>, 3> SceneRenderer::GetMaterialEnvironment()
    {
        using namespace RenderHardware;
        if (!m_IBLSampler)
        {
            SamplerDesc desc;
            desc.U = desc.V = desc.W = AddressMode::ClampToEdge;
            m_IBLSampler             = m_Device->CreateSampler(desc);
        }
        std::array<Handle<RenderHardware::Texture>, 3> environment;
        const std::array<Handle<RenderHardware::Texture>, 3> maps {
            m_Environment.IrradianceMap, m_Environment.RadianceMap, m_BrdfLUT};
        for (size_t i = 0; i < 3; ++i)
        {
            const auto dimension = i == 2 ? TextureDimension::Texture2D : TextureDimension::Cube;
            if (maps[i])
                environment[i] = maps[i];
            else
            {
                if (m_BlackEnvironment[i])
                {
                    environment[i] = m_BlackEnvironment[i];
                    continue;
                }
                TextureDesc desc;
                desc.Dimension   = dimension;
                desc.PixelFormat = Format::RGBA32Float;
                desc.Usage     = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
                environment[i] = m_BlackEnvironment[i] = m_Device->CreateTexture(desc);
                const std::array<float, 4> black {0, 0, 0, 1};
                auto upload = m_Device->CreateBuffer({sizeof(black), BufferUsage::CopySource, MemoryUsage::Upload});
                upload->Write(0, black.data(), sizeof(black));
                auto copy = m_Device->CreateCommandList();
                for (uint32_t face = 0; face < (i == 2 ? 1u : 6u); ++face)
                    copy->CopyBufferToTexture(upload, 0, environment[i], {0, face});
                m_Device->Submit(copy);
            }
        }
        return environment;
    }

    void SceneRenderer::SubmitModelGeometry(const ProductModelComponent& model,
                                            const glm::mat4&             transform,
                                            const std::string&           shaderName,
                                            ModelGeometryMode            mode)
    {
        m_GeometryDraws.push_back({model, transform, shaderName, mode});
    }

    void SceneRenderer::DrawModelGeometry(const GeometryDraw& draw)
    {
        using namespace RenderHardware;
        if (!m_AssetCache)
            return;
        const ModelInstances* instances = m_AssetCache->GetModel(draw.Model.Model);
        if (!instances)
            return;
        const bool  normals  = draw.ShaderName == "DebugNormalShader";
        const char* vertex   = normals                            ? SceneShaders::NormalVertex :
                               draw.ShaderName == "OutLineShader" ? SceneShaders::OutlineVertex :
                               draw.ShaderName == "StencilShader" ? SceneShaders::StencilVertex :
                                                                    nullptr;
        const char* fragment = normals                            ? SceneShaders::NormalFragment :
                               draw.ShaderName == "OutLineShader" ? SceneShaders::OutlineFragment :
                                                                    SceneShaders::StencilFragment;
        if (!vertex)
            return;
        PipelineDesc desc = ScenePipeline(draw.ShaderName.c_str(), vertex, fragment, false);
        if (!desc.Program)
            return;
        desc.DepthStencil.DepthCompare =
            draw.Mode == ModelGeometryMode::StencilOutline ? CompareOp::LessEqual : CompareOp::Less;
        desc.DepthStencil.StencilTest   = draw.Mode == ModelGeometryMode::StencilOutline;
        desc.DepthStencil.Front.Compare = desc.DepthStencil.Back.Compare = CompareOp::NotEqual;
        desc.DepthStencil.Front.Reference = desc.DepthStencil.Back.Reference = 1;
        desc.DepthStencil.Front.WriteMask = desc.DepthStencil.Back.WriteMask = 0;
        desc.Rasterizer.Cull = draw.Mode == ModelGeometryMode::BackFaces ? CullMode::Front : CullMode::None;
        if (normals)
        {
            desc.Primitive      = Topology::LineList;
            desc.VertexBindings = {{0, 28, false}};
            desc.Attributes     = {
                {0, 0, 0, VertexFormat::Float3}, {1, 0, 12, VertexFormat::Float3}, {2, 0, 24, VertexFormat::Float}};
        }
        else
        {
            desc.VertexBindings = {{0, 64, false}};
            desc.Attributes     = {{0, 0, 0, VertexFormat::Float3},
                                   {1, 0, 12, VertexFormat::Float3},
                                   {2, 0, 24, VertexFormat::Float2},
                                   {3, 0, 32, VertexFormat::Float4},
                                   {4, 0, 48, VertexFormat::Float4}};
        }
        auto pipeline = m_Device->CreatePipeline(desc);
        auto commands = m_Device->CreateCommandList();
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight)});
        commands->BindPipeline(pipeline);
        SceneDrawData data;
        data.DeviceClip     = DeviceProjection(glm::mat4(1), m_Device->GetCoordinateConventions());
        data.ViewProjection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        data.ViewPos        = glm::vec4(m_SceneInfo.sceneCamera.Position, 1);
        for (const auto& instance : instances->Instances)
            if (const GpuMesh* mesh = m_AssetCache->GetMesh(instance.Mesh))
            {
                data.Transform = draw.Transform * instance.Transform;
                commands->BindGroup(DrawGroup(*m_Device, desc.GroupLayouts[0], data));
                commands->BindVertexBuffers({{0, normals ? mesh->NormalLines : mesh->Vertices, 0}});
                if (normals)
                    commands->Draw(mesh->IndexCount * 2);
                else
                {
                    commands->BindIndexBuffer(mesh->Indices, IndexType::Uint32);
                    commands->DrawIndexed(mesh->IndexCount);
                }
                ++m_Stats.DrawCalls;
                m_Stats.Triangles += mesh->IndexCount / 3;
            }
        commands->EndRendering();
        m_Device->Submit(commands);
    }

    RenderHardware::PipelineDesc
    SceneRenderer::ScenePipeline(const char* name, const char* vertex, const char* fragment, bool sampled)
    {
        using namespace RenderHardware;
        PipelineDesc desc;
        // Device shaders are scene-owned; CPU SPIR-V products are cached by InternalShader.
        auto& program = m_InternalPrograms[name];
        if (!program)
            program = Build(*m_Device, name, vertex, fragment);
        desc.Program = program;
        BindGroupLayoutDesc layout {0, {{0, BindingType::UniformBuffer}}};
        if (sampled)
            layout.Entries.push_back({1, BindingType::CombinedTextureSampler});
        desc.GroupLayouts           = {m_Device->CreateBindGroupLayout(layout)};
        desc.ColorFormats           = {Format::RGBA8Unorm, Format::RGBA8Unorm};
        desc.HasDepthStencil        = true;
        desc.DepthStencilFormat     = Format::Depth24Stencil8;
        desc.DepthStencil.DepthTest = true;
        desc.Rasterizer.Cull        = CullMode::None;
        desc.Rasterizer.Front       = m_Device->GetCoordinateConventions().ClipY == ClipYDirection::Up ?
                                          FrontFace::CounterClockwise :
                                          FrontFace::Clockwise;
        BlendState blend;
        blend.WriteMask = 7;
        desc.Blends     = {blend, blend};
        return desc;
    }

    void SceneRenderer::Init()
    {
        m_ViewportWidth  = m_RenderScene->m_ViewportWidth;
        m_ViewportHeight = m_RenderScene->m_ViewportHeight;
        m_Device         = RenderHardware::GetSharedDevice();
        m_Picker.SetDevice(m_Device);
        m_Outline.SetDevice(m_Device);
        ResizeTarget();
    }

    RenderHardware::RenderingDesc SceneRenderer::SceneTarget(RenderHardware::LoadOp load) const
    {
        RenderHardware::RenderingDesc desc;
        for (const auto& color : m_Colors)
            desc.Colors.push_back({color, {}, load, RenderHardware::StoreOp::Store, {0.02f, 0.02f, 0.02f, 1}});
        desc.Depth.Target = m_Depth;
        desc.Depth.Load   = load;
        return desc;
    }
    void SceneRenderer::ResizeTarget()
    {
        using namespace RenderHardware;
        m_Colors.clear();
        m_Depth.reset();
        TextureDesc desc;
        desc.Width       = m_ViewportWidth;
        desc.Height      = m_ViewportHeight;
        desc.Usage       = RenderHardware::TextureUsage::RenderTarget | RenderHardware::TextureUsage::Sampled |
                           RenderHardware::TextureUsage::CopySource;
        m_Colors         = {m_Device->CreateTexture(desc), m_Device->CreateTexture(desc)};
        desc.PixelFormat = Format::Depth24Stencil8;
        desc.Usage       = RenderHardware::TextureUsage::DepthStencil | RenderHardware::TextureUsage::Sampled |
                           RenderHardware::TextureUsage::CopySource;
        m_Depth          = m_Device->CreateTexture(desc);
    }
    uintptr_t SceneRenderer::GetOutput(uint32_t index) const
    {
        if (index > m_Colors.size() + 1)
            return uint32_t(-1);
        return m_Device->GetImGuiTextureID(index < m_Colors.size() ? m_Colors[index] : m_Depth);
    }

    void SceneRenderer::PrepareEnvironment(EnvironmentPrecomputeSizes sizes)
    {
        using namespace RenderHardware;
        if (!sizes.Cubemap || !sizes.Irradiance || sizes.Prefilter < 16 || !sizes.BrdfLUT)
            throw std::invalid_argument("IBL sizes must be positive; prefilter needs five mips (at least 16 texels)");
        // The asset decoder owns its stb configuration. Decode bottom-first rows,
        // then match the legacy HDR image's half-float quantization before sampling.
        TextureImportSettings settings;
        settings.FlipY        = true;
        settings.GenerateMips = false;
        auto decoded = ParseTextureSource(std::filesystem::absolute(m_RenderScene->m_EnvironmentPath), settings);
        if (auto* error = std::get_if<AssetError>(&decoded))
        {
            if (Log::GetCoreLogger())
                CORE_ERROR("{}", error->Message);
            else
                std::cerr << error->Message << std::endl;
            return;
        }
        const auto& source = std::get<TextureData>(decoded);
        if (source.Format != TexturePixelFormat::RGBA32Float)
        {
            if (Log::GetCoreLogger())
                CORE_ERROR("Environment must be a floating-point HDR image");
            return;
        }
        const uint32_t        width = source.Width, height = source.Height;
        std::vector<uint16_t> half(size_t(width) * height * 4);
        for (size_t i = 0; i < half.size(); ++i)
        {
            const auto* bytes = source.Mips[0].Pixels.data() + i * 4;
            uint32_t    word  = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) |
                                (uint32_t(bytes[3]) << 24);
            float       value;
            std::memcpy(&value, &word, sizeof(value));
            half[i] = glm::packHalf1x16(value);
        }
        TextureDesc hdrDesc;
        hdrDesc.Width       = width;
        hdrDesc.Height      = height;
        hdrDesc.PixelFormat = Format::RGBA16Float;
        hdrDesc.Usage       = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
        auto hdr            = m_Device->CreateTexture(hdrDesc);
        auto upload =
            m_Device->CreateBuffer({half.size() * sizeof(uint16_t), BufferUsage::CopySource, MemoryUsage::Upload});
        upload->Write(0, half.data(), half.size() * sizeof(uint16_t));
        auto commands = m_Device->CreateCommandList();
        commands->CopyBufferToTexture(upload, 0, hdr);
        if (!m_Cube)
        {
            auto mesh = UploadMesh(MeshFactory::CreateCubeData(glm::vec3(2)), m_Device);
            if (auto* error = std::get_if<AssetError>(&mesh))
                throw std::runtime_error(error->Message);
            m_Cube = std::get<GpuMesh>(std::move(mesh));
        }
        auto texture = [&](uint32_t size, uint32_t levels, TextureDimension dimension) {
            TextureDesc desc;
            desc.Width = desc.Height = size;
            desc.MipLevels           = levels;
            desc.Dimension           = dimension;
            desc.PixelFormat         = Format::RGBA16Float;
            desc.Usage = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::RenderTarget |
                         RenderHardware::TextureUsage::CopySource | RenderHardware::TextureUsage::CopyDestination;
            return m_Device->CreateTexture(desc);
        };
        auto        sky        = texture(sizes.Cubemap, Mips(sizes.Cubemap), TextureDimension::Cube);
        auto        irradiance = texture(sizes.Irradiance, 1, TextureDimension::Cube);
        auto        prefilter  = texture(sizes.Prefilter, Mips(sizes.Prefilter), TextureDimension::Cube);
        auto        brdf       = texture(sizes.BrdfLUT, 1, TextureDimension::Texture2D);
        SamplerDesc sampling;
        sampling.U = sampling.V = sampling.W = AddressMode::ClampToEdge;
        auto sampler                         = m_Device->CreateSampler(sampling);
        auto pipeline = [&](const char* name, const char* vertex, const char* fragment, bool sampled, bool cube) {
            PipelineDesc desc;
            desc.Program = Build(*m_Device, name, vertex, fragment);
            if (!desc.Program)
                return Handle<Pipeline> {};
            BindGroupLayoutDesc layout {0, {{0, BindingType::UniformBuffer}}};
            if (sampled)
                layout.Entries.push_back({1, BindingType::CombinedTextureSampler});
            desc.GroupLayouts           = {m_Device->CreateBindGroupLayout(layout)};
            desc.ColorFormats           = {Format::RGBA16Float};
            desc.HasDepthStencil        = true;
            desc.DepthStencilFormat     = Format::Depth24Stencil8;
            desc.DepthStencil.DepthTest = desc.DepthStencil.DepthWrite = true;
            desc.DepthStencil.DepthCompare                             = CompareOp::LessEqual;
            desc.Blends.resize(1);
            desc.Rasterizer.Cull = CullMode::None;
            if (cube)
            {
                desc.VertexBindings = {{0, 64, false}};
                desc.Attributes     = {{0, 0, 0, VertexFormat::Float3}};
            }
            else
                desc.Primitive = Topology::TriangleStrip;
            return m_Device->CreatePipeline(desc);
        };
        auto hdrPipeline = pipeline("HdrToCubeMap", SceneShaders::HdrVertex, SceneShaders::HdrFragment, true, true);
        auto irradiancePipeline =
            pipeline("genIrradiance", SceneShaders::IrradianceVertex, SceneShaders::IrradianceFragment, true, true);
        auto prefilterPipeline =
            pipeline("prefilter", SceneShaders::PrefilterVertex, SceneShaders::PrefilterFragment, true, true);
        auto brdfPipeline = pipeline("brdf", SceneShaders::BrdfVertex, SceneShaders::BrdfFragment, false, false);
        if (!hdrPipeline || !irradiancePipeline || !prefilterPipeline || !brdfPipeline)
            return;
        const glm::vec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const glm::vec3 up[]         = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
        CaptureData     data;
        data.Projection = DeviceTextureProjection(glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f),
                                                  m_Device->GetCoordinateConventions());
        std::map<uint32_t, Handle<RenderHardware::Texture>> depths;
        auto                                                depth = [&](uint32_t size) {
            auto& target = depths[size];
            if (!target)
            {
                TextureDesc desc;
                desc.Width = desc.Height = size;
                desc.PixelFormat         = Format::Depth24Stencil8;
                desc.Usage               = RenderHardware::TextureUsage::DepthStencil;
                target                   = m_Device->CreateTexture(desc);
            }
            DepthAttachment attachment;
            attachment.Target = target;
            attachment.Load   = LoadOp::Clear;
            return attachment;
        };
        auto capture = [&](const Handle<Pipeline>&                pass,
                           const Handle<RenderHardware::Texture>& target,
                           const Handle<RenderHardware::Texture>& source,
                           uint32_t                               mip,
                           float                                  roughness) {
            uint32_t size     = std::max(1u, target->GetDesc().Width >> mip);
            data.Parameters.x = roughness;
            for (uint32_t face = 0; face < 6; ++face)
            {
                data.View = glm::lookAt(glm::vec3(0), directions[face], up[face]);
                RenderingDesc render;
                render.Colors.push_back({target, {mip, face}, LoadOp::Clear, StoreOp::Store, {0.02f, 0.02f, 0.02f, 1}});
                render.Depth = depth(size);
                commands->BeginRendering(render);
                commands->SetViewport({0, 0, float(size), float(size)});
                commands->BindPipeline(pass);
                commands->BindGroup(DrawGroup(*m_Device, pass->GetDesc().GroupLayouts[0], data, source, sampler));
                commands->BindVertexBuffers({{0, m_Cube->Vertices, 0}});
                commands->BindIndexBuffer(m_Cube->Indices, IndexType::Uint32);
                commands->DrawIndexed(m_Cube->IndexCount);
                commands->EndRendering();
            }
        };
        capture(hdrPipeline, sky, hdr, 0, 0);
        commands->GenerateMips(sky);
        capture(irradiancePipeline, irradiance, sky, 0, 0);
        // Legacy prefilter renders exactly five roughness levels. The remaining allocated
        // levels are zero, as in the original allocation followed by mip generation.
        const std::array<uint16_t, 4> zero {0, 0, 0, 0};
        auto                          zeros = m_Device->CreateBuffer(
            {size_t(sizes.Prefilter) * sizes.Prefilter * sizeof(zero), BufferUsage::CopySource, MemoryUsage::Upload});
        std::vector<uint16_t> zeroPixels(size_t(sizes.Prefilter) * sizes.Prefilter * 4, 0);
        zeros->Write(0, zeroPixels.data(), zeroPixels.size() * sizeof(uint16_t));
        // Initialize lower levels explicitly; avoid generating over the rendered roughness levels.
        for (uint32_t mip = 5; mip < prefilter->GetDesc().MipLevels; ++mip)
            for (uint32_t face = 0; face < 6; ++face)
                commands->CopyBufferToTexture(zeros, 0, prefilter, {mip, face});
        for (uint32_t mip = 0; mip < 5; ++mip)
            capture(prefilterPipeline, prefilter, sky, mip, float(mip) / 4.0f);
        RenderingDesc render;
        render.Colors.push_back({brdf});
        render.Depth = depth(sizes.BrdfLUT);
        commands->BeginRendering(render);
        commands->SetViewport({0, 0, float(sizes.BrdfLUT), float(sizes.BrdfLUT)});
        commands->BindPipeline(brdfPipeline);
        data.Projection = DeviceTextureProjection(glm::mat4(1), m_Device->GetCoordinateConventions());
        commands->BindGroup(DrawGroup(*m_Device, brdfPipeline->GetDesc().GroupLayouts[0], data));
        commands->Draw(4);
        commands->EndRendering();
        m_Device->Submit(commands);
        m_Device->WaitIdle();
        m_SkyBox                    = std::move(sky);
        m_Environment.IrradianceMap = std::move(irradiance);
        m_Environment.RadianceMap   = std::move(prefilter);
        m_BrdfLUT                   = std::move(brdf);
    }

    void SceneRenderer::UpdateEnvironment(std::string path)
    {
        m_RenderScene->m_EnvironmentPath = path;
        PrepareEnvironment();
    }

    void SceneRenderer::BeginScene(SceneInfo info)
    {

        CORE_ASSERT(m_RenderScene, "Render a invalid Scene with nullptr handle");
        m_Active    = true;
        m_SceneInfo = info;
        m_Stats     = {};
        m_ModelDraws.clear();
        m_GeometryDraws.clear();
        if (m_AssetCache)
            m_AssetCache->BeginFrame();
        m_Device->BeginFrame();
        auto              commands = m_Device->CreateCommandList();
        commands->BeginRendering(SceneTarget(RenderHardware::LoadOp::Clear));
        commands->EndRendering();
        m_Device->Submit(commands);
        Renderer2D::SetTarget(m_Device, SceneTarget(RenderHardware::LoadOp::Load));
    }

    void SceneRenderer::AppendModelDraws(const ProductModelComponent& model,
                                         const glm::mat4&             transform,
                                         uint32_t                     id,
                                         std::vector<MeshDraw>&       draws)
    {
        if (!m_AssetCache)
            return;
        if (const ModelInstances* instances = m_AssetCache->GetModel(model.Model))
            for (const auto& instance : instances->Instances)
                if (const GpuMesh* mesh = m_AssetCache->GetMesh(instance.Mesh))
                    draws.push_back({mesh, transform * instance.Transform, id});
    }

    uint32_t SceneRenderer::Pick(const std::vector<MeshDraw>& draws, const glm::mat4& viewProjection, glm::uvec2 pixel)
    {
        return m_Picker.Pick(draws, viewProjection, {m_ViewportWidth, m_ViewportHeight}, pixel);
    }

    void SceneRenderer::EndScene()
    {
        const glm::mat4 viewProjection =
            m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        if (m_Preview)
        {
            const SceneViewMode viewMode = m_ViewMode;
            m_ViewMode = SceneViewMode::Lit;
            SubmitModel(m_Preview->Model, m_Preview->Transform);
            m_ViewMode = viewMode;
        }
        DrawShaderMaterials(m_ModelDraws);
        for (const auto& draw : m_GeometryDraws)
            DrawModelGeometry(draw);
        m_ModelDraws.clear();
        m_GeometryDraws.clear();
        if (!m_Highlight.empty())
        {
            // Material/geometry passes may replace cached meshes. Resolve only at the outline draw.
            std::vector<MeshDraw> draws;
            for (const auto& model : m_Highlight)
                AppendModelDraws(model.Model, model.Transform, 0, draws);
            m_Outline.Draw(SceneTarget(RenderHardware::LoadOp::Load),
                           draws,
                           viewProjection,
                           {m_ViewportWidth, m_ViewportHeight},
                           {1.0f, 0.62f, 0.15f, 1.0f});
            m_Highlight.clear();
        }
        if (m_Preview)
        {
            // Resolve after the highlight too: collecting its models can update the cache.
            std::vector<MeshDraw> draws;
            AppendModelDraws(m_Preview->Model, m_Preview->Transform, 0, draws);
            m_Outline.Draw(SceneTarget(RenderHardware::LoadOp::Load),
                           draws,
                           viewProjection,
                           {m_ViewportWidth, m_ViewportHeight},
                           {0.35f, 0.7f, 1.0f, 1.0f});
            m_Preview.reset();
        }
        Renderer2D::ClearTarget();
        m_Device->EndFrame();
        m_Active = false;
    }

    PickHit SceneRenderer::PickSurface(const std::vector<MeshDraw>& draws,
                                       const glm::mat4&             viewProjection,
                                       glm::uvec2                   pixel)
    {
        return m_Picker.PickSurface(draws, viewProjection, {m_ViewportWidth, m_ViewportHeight}, pixel);
    }

    void SceneRenderer::SetPreview(const ProductModelComponent& model, const glm::mat4& transform)
    {
        m_Preview = ModelDraw {model, transform};
    }

    void SceneRenderer::SetScene(RenderScene* scene)
    {
        CORE_ASSERT(scene, "Handel a nullptr scene");
        m_RenderScene = scene;
    }

    void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth  = width;
        m_ViewportHeight = height;
        ResizeTarget();
    }

    void SceneRenderer::SubmitGrid(float size, float scale, glm::mat4 transform, ImmediatePassState state)
    {
        using namespace RenderHardware;
        auto desc = ScenePipeline("GridShader", SceneShaders::GridVertex, SceneShaders::GridFragment, false);
        if (!desc.Program)
            return;
        desc.VertexBindings             = {{0, 20, false}};
        desc.Attributes                 = {{0, 0, 0, VertexFormat::Float3}, {1, 0, 12, VertexFormat::Float2}};
        desc.DepthStencil.DepthCompare  = state.DepthCompare;
        desc.DepthStencil.DepthWrite    = state.DepthWrite;
        desc.Blends[0].Enabled          = state.Blend;
        desc.Blends[0].WriteMask        = state.WriteMask;
        desc.Blends[0].SourceAlpha      = BlendFactor::SourceAlpha;
        desc.Blends[0].DestinationAlpha = BlendFactor::OneMinusSourceAlpha;
        desc.Blends[0].SourceColor      = BlendFactor::SourceAlpha;
        desc.Blends[0].DestinationColor = BlendFactor::OneMinusSourceAlpha;
        desc.Blends[1]                  = desc.Blends[0];
        const float    vertices[]       = {-0.5f, 0, -0.5f, 0, 0, 0.5f,  0, -0.5f, 1, 0,
                                           0.5f,  0, 0.5f,  1, 1, -0.5f, 0, 0.5f,  0, 1};
        const uint32_t indices[]        = {0, 1, 2, 2, 3, 0};
        auto           vb = m_Device->CreateBuffer({sizeof(vertices), BufferUsage::Vertex, MemoryUsage::Upload});
        auto           ib = m_Device->CreateBuffer({sizeof(indices), BufferUsage::Index, MemoryUsage::Upload});
        vb->Write(0, vertices, sizeof(vertices));
        ib->Write(0, indices, sizeof(indices));
        SceneDrawData data;
        data.DeviceClip     = DeviceProjection(glm::mat4(1), m_Device->GetCoordinateConventions());
        data.ViewProjection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        data.Transform      = transform;
        data.Settings       = {scale, size, 0, 0};
        auto commands       = m_Device->CreateCommandList();
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight)});
        commands->BindPipeline(m_Device->CreatePipeline(desc));
        commands->BindGroup(DrawGroup(*m_Device, desc.GroupLayouts[0], data));
        commands->BindVertexBuffers({{0, vb, 0}});
        commands->BindIndexBuffer(ib, IndexType::Uint32);
        commands->DrawIndexed(6);
        commands->EndRendering();
        m_Device->Submit(commands);
    }

    void SceneRenderer::SubmitSkybox(RenderHardware::Handle<RenderHardware::Texture> skybox, ImmediatePassState state)
    {
        using namespace RenderHardware;
        if (!skybox)
            return;
        auto desc = ScenePipeline("skybox", SceneShaders::SkyboxVertex, SceneShaders::SkyboxFragment, true);
        if (!desc.Program)
            return;
        if (!m_Cube)
        {
            auto mesh = UploadMesh(MeshFactory::CreateCubeData(glm::vec3(2)), m_Device);
            if (auto* error = std::get_if<AssetError>(&mesh))
                throw std::runtime_error(error->Message);
            m_Cube = std::get<GpuMesh>(std::move(mesh));
        }
        desc.VertexBindings            = {{0, 64, false}};
        desc.Attributes                = {{0, 0, 0, VertexFormat::Float3}};
        desc.DepthStencil.DepthCompare = state.DepthCompare;
        desc.DepthStencil.DepthWrite   = state.DepthWrite;
        desc.Blends[0].Enabled         = state.Blend;
        desc.Blends[0].WriteMask       = state.WriteMask;
        desc.Blends[0].SourceColor = desc.Blends[0].SourceAlpha = BlendFactor::SourceAlpha;
        desc.Blends[0].DestinationColor = desc.Blends[0].DestinationAlpha = BlendFactor::OneMinusSourceAlpha;
        desc.Blends[1]                                                    = desc.Blends[0];
        SceneDrawData data;
        data.DeviceClip     = DeviceProjection(glm::mat4(1), m_Device->GetCoordinateConventions());
        data.ViewProjection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix();
        data.Transform      = m_SceneInfo.sceneCamera.viewMatrix;
        GetMaterialEnvironment();
        auto commands = m_Device->CreateCommandList();
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight)});
        commands->BindPipeline(m_Device->CreatePipeline(desc));
        commands->BindGroup(DrawGroup(*m_Device, desc.GroupLayouts[0], data, skybox, m_IBLSampler));
        commands->BindVertexBuffers({{0, m_Cube->Vertices, 0}});
        commands->BindIndexBuffer(m_Cube->Indices, IndexType::Uint32);
        commands->DrawIndexed(m_Cube->IndexCount);
        commands->EndRendering();
        m_Device->Submit(commands);
    }

} // namespace Kans
