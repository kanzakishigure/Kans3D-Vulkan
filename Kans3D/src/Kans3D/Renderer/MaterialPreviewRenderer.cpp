#include "kspch.h"
#include "MaterialPreviewRenderer.h"
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Resource/InternalShader.h"
#include "Resource/MaterialPass.h"
#include "SceneRenderer.h"

namespace Kans
{
    namespace
    {
        struct PreviewRetirement
        {
            RenderHardware::Device& Device;
            ~PreviewRetirement() { Device.WaitIdle(); }
        };
        const char*    CheckerVertex   = R"(#version 450
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2 - 1, 0, 1);
})";
        const char*    CheckerFragment = R"(#version 450
layout(set=0,binding=0,std140) uniform Background { vec4 Size; } p;
layout(location=0) out vec4 color;
void main() {
    float y = p.Size.z != 0 ? p.Size.y - gl_FragCoord.y : gl_FragCoord.y;
    float c = mod(floor(gl_FragCoord.x / 16) + floor(y / 16), 2);
    color = vec4(vec3(mix(0.22, 0.32, c)), 1);
})";
        MeshVertexData Vertex(glm::vec3 p, glm::vec3 n, glm::vec2 uv, glm::vec4 t)
        {
            MeshVertexData v;
            v.Position = {p.x, p.y, p.z};
            v.Normal   = {n.x, n.y, n.z};
            v.UV       = {uv.x, uv.y};
            v.Tangent  = {t.x, t.y, t.z, t.w};
            return v;
        }
        MeshData Shape(MaterialPreviewShape shape)
        {
            MeshData mesh;
            if (shape == MaterialPreviewShape::Sphere)
            {
                constexpr uint32_t rings = 32, segments = 64;
                constexpr float    pi = 3.14159265358979323846f;
                for (uint32_t y = 0; y <= rings; ++y)
                    for (uint32_t x = 0; x <= segments; ++x)
                    {
                        float     u = float(x) / segments, v = float(y) / rings;
                        float     a = u * 2 * pi, b = v * pi;
                        glm::vec3 n {std::sin(b) * std::cos(a), std::cos(b), std::sin(b) * std::sin(a)};
                        // +U follows longitude; cross(N,T) points south, along +V.
                        mesh.Vertices.push_back(Vertex(n, n, {u, v}, {-std::sin(a), 0, std::cos(a), 1}));
                    }
                for (uint32_t y = 0; y < rings; ++y)
                    for (uint32_t x = 0; x < segments; ++x)
                    {
                        uint32_t a = y * (segments + 1) + x, b = a + segments + 1;
                        if (y != 0)
                            mesh.Indices.insert(mesh.Indices.end(), {a, a + 1, b});
                        if (y != rings - 1)
                            mesh.Indices.insert(mesh.Indices.end(), {a + 1, b + 1, b});
                    }
            }
            else
            {
                const auto face = [&](glm::vec3 n, glm::vec3 t, glm::vec3 b) {
                    uint32_t  start  = uint32_t(mesh.Vertices.size());
                    glm::vec3 center = shape == MaterialPreviewShape::Plane ? glm::vec3(0) : n * 0.75f;
                    float     half   = shape == MaterialPreviewShape::Plane ? 1.0f : 0.75f;
                    for (auto uv : {glm::vec2 {0, 0}, {1, 0}, {1, 1}, {0, 1}})
                        mesh.Vertices.push_back(
                            Vertex(center + half * ((2 * uv.x - 1) * t + (2 * uv.y - 1) * b), n, uv, glm::vec4(t, 1)));
                    mesh.Indices.insert(mesh.Indices.end(), {start, start + 1, start + 2, start + 2, start + 3, start});
                };
                face({0, 0, 1}, {1, 0, 0}, {0, 1, 0});
                if (shape == MaterialPreviewShape::Cube)
                {
                    face({0, 0, -1}, {-1, 0, 0}, {0, 1, 0});
                    face({1, 0, 0}, {0, 0, -1}, {0, 1, 0});
                    face({-1, 0, 0}, {0, 0, 1}, {0, 1, 0});
                    face({0, 1, 0}, {1, 0, 0}, {0, 0, -1});
                    face({0, -1, 0}, {1, 0, 0}, {0, 0, 1});
                }
            }
            return mesh;
        }
        bool SameShader(const ShaderData& a, const ShaderData& b)
        {
            if (a.TextureFallbacks != b.TextureFallbacks || a.Code.Modules.size() != b.Code.Modules.size())
                return false;
            for (size_t i = 0; i < a.Code.Modules.size(); ++i)
            {
                const auto& x = a.Code.Modules[i];
                const auto& y = b.Code.Modules[i];
                if (x.Stage != y.Stage || x.EntryPoint != y.EntryPoint || x.Words != y.Words)
                    return false;
            }
            return true;
        }
    } // namespace

    MaterialPreviewRenderer::MaterialPreviewRenderer(SceneRenderer& scene) : m_Scene(scene), m_Device(scene.GetDevice())
    {}
    MaterialPreviewRenderer::~MaterialPreviewRenderer() = default;

    bool MaterialPreviewRenderer::Resize(glm::uvec2 size)
    {
        if (!size.x || !size.y || size.x > 4096 || size.y > 4096)
            return false;
        if (size == m_Size && m_Color)
            return true;
        using namespace RenderHardware;
        TextureDesc desc;
        desc.Width       = size.x;
        desc.Height      = size.y;
        desc.PixelFormat = Format::RGBA8Unorm;
        desc.Usage       = RenderHardware::TextureUsage::RenderTarget | RenderHardware::TextureUsage::Sampled |
                           RenderHardware::TextureUsage::CopySource;
        m_Color          = m_Device->CreateTexture(desc);
        desc.PixelFormat = Format::Depth24Stencil8;
        desc.Usage       = RenderHardware::TextureUsage::DepthStencil;
        m_Depth          = m_Device->CreateTexture(desc);
        m_Size = size;
        return true;
    }

    MaterialPreviewResult MaterialPreviewRenderer::Render(const MaterialData&           material,
                                                          const ShaderData*             source,
                                                          const MaterialPreviewOptions& options)
    {
        const auto image = RenderImage(material, source, options);
        return {image.Target ? m_Device->GetImGuiTextureID(image.Target) : 0, image.Complete};
    }
    MaterialPreviewResult MaterialPreviewRenderer::Render(AssetID material, const MaterialPreviewOptions& options)
    {
        const auto image = RenderImage(material, options);
        return {image.Target ? m_Device->GetImGuiTextureID(image.Target) : 0, image.Complete};
    }
    MaterialPreviewImageResult MaterialPreviewRenderer::RenderImage(const MaterialData&           material,
                                                                    const ShaderData*             source,
                                                                    const MaterialPreviewOptions& options)
    {
        if (!m_Scene.GetAssetCache())
            return {};
        PreviewRetirement retirement {*m_Device};
        auto&            cache    = *m_Scene.GetAssetCache();
        const GpuShader* shader   = nullptr;
        bool             complete = true;
        if (source && !IsStandardShader(material.Shader))
        {
            if (!m_ShaderSource || !SameShader(*m_ShaderSource, *source))
            {
                m_ShaderSource = *source;
                m_Shader.reset();
                auto built = GpuShader::Create(*source, m_Device);
                if (auto* value = std::get_if<GpuShader>(&built))
                    m_Shader = CreateScope<GpuShader>(std::move(*value));
            }
            shader = m_Shader.get();
        }
        MaterialData data = material;
        if (!shader)
        {
            complete = IsStandardShader(material.Shader);
            if (!complete)
            {
                data        = MaterialData {};
                data.Render = material.Render;
            }
            auto resolved = ResolveStandardMaterial(data);
            if (std::holds_alternative<AssetError>(resolved))
                return {};
            data   = std::get<MaterialData>(std::move(resolved));
            source = &GetStandardShader();
            shader = cache.GetShader(StandardShaderID());
        }
        if (!shader)
            return {};
        auto packed = MakeGpuMaterialBindings(data, *source, m_Device);
        if (auto* bindings = std::get_if<GpuMaterialBindings>(&packed))
            return Draw(*shader, *bindings, options, complete);
        // A malformed custom recipe takes the same Standard fallback as the scene.
        if (!IsStandardShader(material.Shader))
        {
            MaterialData fallback;
            fallback.Render = material.Render;
            auto image      = RenderImage(fallback, nullptr, options);
            image.Complete  = false;
            return image;
        }
        return {};
    }

    MaterialPreviewImageResult MaterialPreviewRenderer::RenderImage(AssetID                       material,
                                                                    const MaterialPreviewOptions& options)
    {
        if (!m_Scene.GetAssetCache())
            return {};
        PreviewRetirement retirement {*m_Device};
        auto& cache    = *m_Scene.GetAssetCache();
        auto  status   = cache.GetAssetManager().Request<MaterialDataAsset>(material);
        bool  complete = status.IsReady();
        if (status.Asset && !IsStandardShader(status.Asset->GetData().Shader))
            complete &= cache.GetAssetManager().Request<ShaderDataAsset>(status.Asset->GetData().Shader).IsReady();
        const auto* bindings = cache.GetMaterialBindings(material);
        const auto* shader   = bindings ? cache.GetShader(bindings->Shader) : nullptr;
        if (!bindings || !shader)
            return {};
        if (status.Asset && !IsStandardShader(status.Asset->GetData().Shader))
            complete &= bindings->Shader == status.Asset->GetData().Shader;
        return Draw(*shader, *bindings, options, complete);
    }

    MaterialPreviewImageResult MaterialPreviewRenderer::Draw(const GpuShader&              shader,
                                                             const GpuMaterialBindings&    bindings,
                                                             const MaterialPreviewOptions& options,
                                                             bool                          complete)
    {
        const auto shape = size_t(options.Shape);
        if (shape >= m_Shapes.size() || !std::isfinite(options.Rotation.x) || !std::isfinite(options.Rotation.y) ||
            !Resize(options.Size))
            return {};
        if (!m_Shapes[shape])
        {
            auto uploaded = UploadMesh(Shape(options.Shape), m_Device);
            if (auto* mesh = std::get_if<GpuMesh>(&uploaded))
                m_Shapes[shape] = std::move(*mesh);
            else
                return {};
        }
        using namespace RenderHardware;
        if (!m_Checker)
        {
            auto built = CreateInternalShader(*m_Device, "MaterialChecker", CheckerVertex, CheckerFragment);
            if (auto* error = std::get_if<AssetError>(&built))
            {
                if (Log::GetCoreLogger())
                    CORE_ERROR("{}", error->Message);
                else
                    std::cerr << error->Message << std::endl;
                return {};
            }
            PipelineDesc desc;
            desc.Program         = std::get<Handle<RenderHardware::Shader>>(std::move(built));
            desc.GroupLayouts    = {m_Device->CreateBindGroupLayout({0, {{0, BindingType::UniformBuffer}}})};
            desc.Rasterizer.Cull = CullMode::None;
            desc.ColorFormats    = {Format::RGBA8Unorm};
            desc.Blends.resize(1);
            desc.HasDepthStencil    = true;
            desc.DepthStencilFormat = Format::Depth24Stencil8;
            m_Checker               = m_Device->CreatePipeline(desc);
        }
        auto          commands = m_Device->CreateCommandList();
        RenderingDesc target;
        target.Colors       = {{m_Color}};
        target.Depth.Target = m_Depth;
        commands->BeginRendering(target);
        commands->SetViewport({0, 0, float(options.Size.x), float(options.Size.y), 0, 1});
        commands->SetScissor({0, 0, options.Size.x, options.Size.y});
        commands->BindPipeline(m_Checker);
        const auto      coordinates = m_Device->GetCoordinateConventions();
        const glm::vec4 background {float(options.Size.x),
                                    float(options.Size.y),
                                    coordinates.Origin == FramebufferOrigin::UpperLeft ? 1.0f : 0.0f,
                                    0};
        auto checkerData = m_Device->CreateBuffer({sizeof(background), BufferUsage::Uniform, MemoryUsage::Upload});
        checkerData->Write(0, &background, sizeof(background));
        commands->BindGroup(m_Device->CreateBindGroup({m_Checker->GetDesc().GroupLayouts[0], {{0, checkerData}}}));
        commands->Draw(3);
        EngineFrameUniforms frame;
        frame.CameraPosition = {0, 0, 3.5f, 1};
        frame.ViewProjection =
            glm::perspective(glm::radians(45.0f), float(options.Size.x) / options.Size.y, 0.1f, 20.0f) *
            glm::lookAt(glm::vec3(frame.CameraPosition), glm::vec3(0), glm::vec3(0, 1, 0));
        frame.LightPositions[0] = {-3, 4, 5, 1};
        frame.LightColors[0]    = {40, 40, 40, 1};
        frame.LightDiffuse[0]   = {1, 1, 1, 1};
        frame.Time              = m_Scene.GetElapsedTime();
        frame.ViewProjection    = DeviceProjection(frame.ViewProjection, coordinates);
        auto frameBuffer        = m_Device->CreateBuffer({sizeof(frame), BufferUsage::Uniform, MemoryUsage::Upload});
        frameBuffer->Write(0, &frame, sizeof(frame));
        EngineObjectUniforms object;
        object.Transform      = glm::rotate(glm::mat4(1), options.Rotation.y, {1, 0, 0}) *
                                glm::rotate(glm::mat4(1), options.Rotation.x, {0, 1, 0});
        object.NormalMatrix   = glm::transpose(glm::inverse(object.Transform));
        object.MaterialRender = {float(bindings.Render.AlphaMode),
                                 bindings.Render.AlphaCutoff,
                                 bindings.Render.DoubleSided ? 1.0f : 0.0f,
                                 0};
        auto objectBuffer     = m_Device->CreateBuffer({sizeof(object), BufferUsage::Uniform, MemoryUsage::Upload});
        objectBuffer->Write(0, &object, sizeof(object));
        MaterialPassData pass;
        pass.Frame              = frameBuffer;
        pass.Object             = objectBuffer;
        pass.Environment        = m_Scene.GetMaterialEnvironment();
        pass.EnvironmentSampler = m_Scene.GetMaterialEnvironmentSampler();
        pass.Render             = bindings.Render;
        complete &= RecordMaterialDraw(*m_Scene.GetAssetCache(), *commands, shader, bindings, *m_Shapes[shape], pass);
        commands->EndRendering();
        m_Device->Submit(commands);
        return {m_Color, complete};
    }

    std::optional<MaterialThumbnail> MaterialPreviewRenderer::RenderThumbnail(AssetID                       material,
                                                                              const MaterialPreviewOptions& options)
    {
        PreviewRetirement retirement {*m_Device};
        const auto        rendered = RenderImage(material, options);
        if (!rendered.Target || !rendered.Complete)
            return std::nullopt;
        MaterialThumbnail result;
        result.Size = options.Size;
        result.Pixels.resize(size_t(options.Size.x) * options.Size.y * 4);
        using namespace RenderHardware;
        auto readback =
            m_Device->CreateBuffer({result.Pixels.size(), BufferUsage::CopyDestination, MemoryUsage::Readback});
        auto commands = m_Device->CreateCommandList();
        commands->CopyTextureToBuffer(m_Color, {}, readback);
        m_Device->Submit(commands);
        m_Device->WaitIdle();
        result.Pixels  = readback->Read(0, result.Pixels.size());
        const auto row = size_t(options.Size.x) * 4;
        if (m_Device->GetCoordinateConventions().Origin == FramebufferOrigin::LowerLeft)
            for (size_t y = 0; y < options.Size.y / 2; ++y)
                std::swap_ranges(result.Pixels.begin() + y * row,
                                 result.Pixels.begin() + (y + 1) * row,
                                 result.Pixels.begin() + (options.Size.y - y - 1) * row);
        return result;
    }
} // namespace Kans
