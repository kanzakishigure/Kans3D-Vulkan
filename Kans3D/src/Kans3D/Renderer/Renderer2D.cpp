#include "kspch.h"
#include "Renderer2D.h"
#include <iostream>
#include "Kans3D/RHI/Backend.h"
#include "Resource/InternalShader.h"

namespace Kans
{
    namespace
    {
        using namespace RenderHardware;
        constexpr uint32_t MaxQuads = 10000;
        struct QuadVertex
        {
            glm::vec3 Position;
            glm::vec4 Color;
            glm::vec2 UV;
            float     Tiling;
        };
        const char* Vertex   = R"(#version 450
layout(location = 0) in vec3 Position;
layout(location = 1) in vec4 Color;
layout(location = 2) in vec2 UV;
layout(location = 3) in float Tiling;
layout(set = 0, binding = 0, std140) uniform Frame { mat4 ViewProjection; };
layout(location = 0) out vec4 V_Color;
layout(location = 1) out vec2 V_UV;
layout(location = 2) out float V_Tiling;
void main() {
    gl_Position = ViewProjection * vec4(Position,1);
    V_Color = Color; V_UV = UV; V_Tiling = Tiling;
})";
        const char* Fragment = R"(#version 450
layout(location = 0) in vec4 V_Color;
layout(location = 1) in vec2 V_UV;
layout(location = 2) in float V_Tiling;
layout(set = 0, binding = 1) uniform sampler2D Image;
layout(location = 0) out vec4 Color;
void main() { Color = V_Color * texture(Image,V_UV * V_Tiling); }
)";
        struct Renderer2DData
        {
            std::shared_ptr<Device>         GPU;
            Handle<RenderHardware::Shader>  Program;
            Handle<BindGroupLayout>         Layout;
            Handle<RenderHardware::Texture> White;
            Handle<RenderHardware::Buffer>  Indices;
            std::vector<QuadVertex>         Vertices;
            Ref<Texture2D>                  Texture;
            glm::mat4                       ViewProjection {1};
            Renderer2D::Statistics          Stats;
            ImmediatePassState              State;
        };
        Renderer2DData Data;
        // Target routing is independent of Init so a scene can provide it before the
        // first sprite. Each standalone consumer may explicitly supply an RHI target.
        std::shared_ptr<Device> TargetDevice;
        RenderingDesc           Target;
        uint32_t                Width = 0, Height = 0;
        void                    Append(const glm::mat4&      transform,
                                       const Ref<Texture2D>& texture,
                                       const glm::vec4&      color,
                                       const glm::vec2*      uv,
                                       float                 tiling)
        {
            // Consecutive quads with the same texture share one draw. Flushing on a
            // texture change preserves blend order without descriptor arrays/indexing.
            if (Data.Vertices.size() == MaxQuads * 4 || Data.Texture != texture)
                Renderer2D::Flush();
            Data.Texture                    = texture;
            constexpr glm::vec4 positions[] = {
                {-.5f, -.5f, 0, 1}, {.5f, -.5f, 0, 1}, {.5f, .5f, 0, 1}, {-.5f, .5f, 0, 1}};
            for (uint32_t i = 0; i < 4; ++i)
                Data.Vertices.push_back({glm::vec3(transform * positions[i]), color, uv[i], tiling});
            ++Data.Stats.QuadCount;
        }
        glm::mat4 Transform(glm::vec3 position, glm::vec2 size, float rotation = 0)
        {
            return glm::translate(glm::mat4(1), position) * glm::rotate(glm::mat4(1), rotation, glm::vec3(0, 0, 1)) *
                   glm::scale(glm::mat4(1), glm::vec3(size, 1));
        }
        constexpr glm::vec2 UV[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    } // namespace

    void Renderer2D::Init(std::shared_ptr<Device> device)
    {
        if (Data.GPU)
            return;
        Data.GPU = device ? std::move(device) : TargetDevice;
        if (!Data.GPU)
        {
            Data.GPU = RenderHardware::GetSharedDevice();
        }
        auto result = CreateInternalShader(*Data.GPU, "Renderer2D", Vertex, Fragment);
        if (auto* error = std::get_if<AssetError>(&result))
        {
            if (Log::GetCoreLogger())
                CORE_ERROR("{}", error->Message);
            else
                std::cerr << error->Message << std::endl;
            return;
        }
        Data.Program = std::get<Handle<RenderHardware::Shader>>(std::move(result));
        Data.Layout  = Data.GPU->CreateBindGroupLayout(
            {0, {{0, BindingType::UniformBuffer}, {1, BindingType::CombinedTextureSampler}}});
        std::vector<uint32_t> indices;
        indices.reserve(MaxQuads * 6);
        for (uint32_t i = 0; i < MaxQuads; ++i)
            for (uint32_t n : {0u, 1u, 2u, 2u, 3u, 0u})
                indices.push_back(i * 4 + n);
        Data.Indices =
            Data.GPU->CreateBuffer({indices.size() * sizeof(uint32_t), BufferUsage::Index, MemoryUsage::Upload});
        Data.Indices->Write(0, indices.data(), indices.size() * sizeof(uint32_t));
        TextureDesc desc;
        desc.Usage      = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
        Data.White      = Data.GPU->CreateTexture(desc);
        auto     buffer = Data.GPU->CreateBuffer({4, BufferUsage::CopySource, MemoryUsage::Upload});
        uint32_t white  = 0xffffffff;
        buffer->Write(0, &white, 4);
        auto copy = Data.GPU->CreateCommandList();
        copy->CopyBufferToTexture(buffer, 0, Data.White);
        Data.GPU->Submit(copy);
        Data.GPU->WaitIdle();
        Data.Vertices.reserve(MaxQuads * 4);
    }
    void Renderer2D::SetTarget(std::shared_ptr<Device> device, const RenderingDesc& target)
    {
        TargetDevice = std::move(device);
        Target       = target;
        for (auto& color : Target.Colors)
            color.Load = LoadOp::Load;
        Target.Depth.Load = LoadOp::Load;
        const auto& desc  = Target.Colors.at(0).Target->GetDesc();
        Width             = std::max(1u, desc.Width >> Target.Colors[0].Subresource.Mip);
        Height            = std::max(1u, desc.Height >> Target.Colors[0].Subresource.Mip);
    }
    void Renderer2D::ClearTarget()
    {
        Target = {};
        TargetDevice.reset();
    }
    void Renderer2D::BeginScene(const OrthographicCamera& camera, ImmediatePassState state)
    {
        BeginScene(Camera(camera.GetViewProjectionMatrtix()), glm::mat4(1), state);
    }
    void Renderer2D::BeginScene(const Camera& camera, const glm::mat4& view, ImmediatePassState state)
    {
        Data.Vertices.clear();
        Data.Texture.reset();
        Data.State = state;
        Data.ViewProjection =
            DeviceProjection(camera.GetProjectionMatrix() * view, Data.GPU->GetCoordinateConventions());
    }
    void Renderer2D::EndScene() { Flush(); }
    void Renderer2D::Shutdown()
    {
        ClearTarget();
        if (Data.GPU)
            Data.GPU->WaitIdle();
        Data = {};
    }
    void Renderer2D::Flush()
    {
        if (Data.Vertices.empty())
            return;
        if (!Data.Program || Target.Colors.empty())
            throw std::logic_error("Renderer2D requires an RHI target and initialized shader");
        if (TargetDevice != Data.GPU)
            throw std::logic_error("Renderer2D target belongs to another device");
        auto&       device = *Data.GPU;
        auto        image  = Data.White;
        SamplerDesc sampling;
        if (Data.Texture)
        {
            image    = Data.Texture->GetResource();
            sampling = Data.Texture->GetSampling();
        }
        auto vertices =
            device.CreateBuffer({Data.Vertices.size() * sizeof(QuadVertex), BufferUsage::Vertex, MemoryUsage::Upload});
        vertices->Write(0, Data.Vertices.data(), Data.Vertices.size() * sizeof(QuadVertex));
        auto frame = device.CreateBuffer({sizeof(glm::mat4), BufferUsage::Uniform, MemoryUsage::Upload});
        frame->Write(0, &Data.ViewProjection, sizeof(glm::mat4));
        PipelineDesc desc;
        desc.Program         = Data.Program;
        desc.GroupLayouts    = {Data.Layout};
        desc.VertexBindings  = {{0, sizeof(QuadVertex), false}};
        desc.Attributes      = {{0, 0, offsetof(QuadVertex, Position), VertexFormat::Float3},
                                {1, 0, offsetof(QuadVertex, Color), VertexFormat::Float4},
                                {2, 0, offsetof(QuadVertex, UV), VertexFormat::Float2},
                                {3, 0, offsetof(QuadVertex, Tiling), VertexFormat::Float}};
        desc.Rasterizer.Cull = CullMode::None;
        desc.HasDepthStencil = bool(Target.Depth.Target);
        if (desc.HasDepthStencil)
            desc.DepthStencilFormat = Target.Depth.Target->GetDesc().PixelFormat;
        desc.DepthStencil.DepthTest    = desc.HasDepthStencil;
        desc.DepthStencil.DepthWrite   = desc.HasDepthStencil && Data.State.DepthWrite;
        desc.DepthStencil.DepthCompare = Data.State.DepthCompare;
        BlendState blend;
        blend.Enabled   = Data.State.Blend;
        blend.WriteMask = Data.State.WriteMask;
        // Preserve the original straight-alpha equation, including target alpha.
        blend.SourceAlpha      = BlendFactor::SourceAlpha;
        blend.DestinationAlpha = BlendFactor::OneMinusSourceAlpha;
        blend.SourceColor      = BlendFactor::SourceAlpha;
        blend.DestinationColor = BlendFactor::OneMinusSourceAlpha;
        for (const auto& color : Target.Colors)
        {
            desc.ColorFormats.push_back(color.Target->GetDesc().PixelFormat);
            desc.Blends.push_back(blend);
        }
        auto commands = device.CreateCommandList();
        commands->BeginRendering(Target);
        commands->SetViewport({0, 0, float(Width), float(Height)});
        commands->BindPipeline(device.CreatePipeline(desc));
        commands->BindGroup(device.CreateBindGroup(
            {Data.Layout,
             {{0, frame, 0, sizeof(glm::mat4), {}, {}}, {1, {}, 0, 0, image, device.CreateSampler(sampling)}}}));
        commands->BindVertexBuffers({{0, vertices, 0}});
        commands->BindIndexBuffer(Data.Indices, IndexType::Uint32);
        commands->DrawIndexed(uint32_t(Data.Vertices.size() / 4) * 6);
        commands->EndRendering();
        device.Submit(commands);
        ++Data.Stats.DrawCalls;
        Data.Vertices.clear();
        Data.Texture.reset();
    }
    void Renderer2D::DrawQuad(const glm::vec2& p, const glm::vec2& s, const glm::vec4& c)
    {
        DrawQuad(glm::vec3(p, 0), s, c);
    }
    void Renderer2D::DrawQuad(const glm::vec3& p, const glm::vec2& s, const glm::vec4& c)
    {
        DrawQuad(Transform(p, s), c);
    }
    void Renderer2D::DrawQuad(const glm::mat4& t, const glm::vec4& c) { Append(t, {}, c, UV, 1); }
    void Renderer2D::DrawQuad(const glm::vec2&      p,
                              const glm::vec2&      s,
                              const Ref<Texture2D>& image,
                              const glm::vec4&      c,
                              float                 tiling)
    {
        DrawQuad(glm::vec3(p, 0), s, image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::vec3&      p,
                              const glm::vec2&      s,
                              const Ref<Texture2D>& image,
                              const glm::vec4&      c,
                              float                 tiling)
    {
        DrawQuad(Transform(p, s), image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::mat4& t, const Ref<Texture2D>& image, const glm::vec4& c, float tiling)
    {
        Append(t, image, c, UV, tiling);
    }
    void Renderer2D::DrawQuad(const glm::vec2&         p,
                              const glm::vec2&         s,
                              const Ref<SubTexture2D>& image,
                              const glm::vec4&         c,
                              float                    tiling)
    {
        DrawQuad(glm::vec3(p, 0), s, image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::vec3&         p,
                              const glm::vec2&         s,
                              const Ref<SubTexture2D>& image,
                              const glm::vec4&         c,
                              float                    tiling)
    {
        DrawQuad(Transform(p, s), image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::mat4& t, const Ref<SubTexture2D>& image, const glm::vec4& c, float tiling)
    {
        Append(t, image->GetTexture(), c, image->GetCroods(), tiling);
    }
    void Renderer2D::DrawRotateQuad(const glm::vec2& p, float r, const glm::vec2& s, const glm::vec4& c)
    {
        DrawRotateQuad(glm::vec3(p, 1), r, s, c);
    }
    void Renderer2D::DrawRotateQuad(const glm::vec3& p, float r, const glm::vec2& s, const glm::vec4& c)
    {
        DrawQuad(Transform(p, s, r), c);
    }
    void Renderer2D::DrawRotateQuad(const glm::vec2&      p,
                                    float                 r,
                                    const glm::vec2&      s,
                                    const Ref<Texture2D>& image,
                                    const glm::vec4&      c,
                                    float                 tiling)
    {
        DrawRotateQuad(glm::vec3(p, 1), r, s, image, c, tiling);
    }
    void Renderer2D::DrawRotateQuad(const glm::vec3&      p,
                                    float                 r,
                                    const glm::vec2&      s,
                                    const Ref<Texture2D>& image,
                                    const glm::vec4&      c,
                                    float                 tiling)
    {
        DrawQuad(Transform(p, s, r), image, c, tiling);
    }
    void                    Renderer2D::ResetStats() { Data.Stats = {}; }
    Renderer2D::Statistics& Renderer2D::GetStats() { return Data.Stats; }
    void                    Renderer2D::FlushAndReset() { Flush(); }
} // namespace Kans
