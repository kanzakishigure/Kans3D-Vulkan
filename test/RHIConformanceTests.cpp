#include <array>
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include "Kans3D/RHI/RHI.h"
#include "GpuTestContext.h"
#include "MaterialShaderTestUtilities.h"

namespace
{
    using namespace Kans;
    using namespace Kans::RenderHardware;
    constexpr uint32_t Size     = 16;
    const char*        Vertex   = R"(#version 450
layout(push_constant) uniform Object { vec4 Color; vec4 Params; } p;
void main() {
    vec2 positions[3] = vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position = vec4(positions[gl_VertexIndex] + p.Params.yz, p.Params.x, 1);
})";
    const char*        Fragment = R"(#version 450
layout(push_constant) uniform Object { vec4 Color; vec4 Params; } p;
layout(location=0) out vec4 color;
void main() { color = p.Color; })";
    struct Constants
    {
        std::array<float, 4> Color {1, 0, 0, 1};
        std::array<float, 4> Params {0, 0, 0, 0};
    };
    using Pixel = std::array<uint8_t, 4>;
    class RHIConformance : public testing::TestWithParam<GpuTestBackend>
    {
    protected:
        std::unique_ptr<GpuTestContext> Context;
        std::unique_ptr<Device>         GPU;
        Handle<Shader>                  Program;
        void                            SetUp() override
        {
            Context = std::make_unique<GpuTestContext>(GetParam());
            ASSERT_NO_FATAL_FAILURE(Context->SetUp());
            GPU = Context->CreateDevice();
            GPU->BeginFrame();
            Program = ShaderFrom(Vertex, Fragment);
        }
        void TearDown() override
        {
            if (GPU)
            {
                GPU->EndFrame();
                Program.reset();
                GPU->WaitIdle();
                GPU.reset();
            }
            Context.reset();
        }
        Handle<Shader> ShaderFrom(const std::string& vertex, const std::string& fragment)
        {
            ShaderData data;
            data.Code = CompileTestMaterialShader(vertex, fragment);
            return GPU->CreateShader(data);
        }
        PipelineDesc Description(Format format = Format::RGBA8Unorm)
        {
            PipelineDesc d;
            d.Program           = Program;
            d.PushConstantBytes = 32;
            d.Rasterizer.Cull   = CullMode::None;
            d.ColorFormats      = {format};
            d.Blends.resize(1);
            return d;
        }
        Handle<Texture> Target(Format format = Format::RGBA8Unorm, uint32_t width = Size, uint32_t height = Size)
        {
            TextureDesc d;
            d.Width       = width;
            d.Height      = height;
            d.PixelFormat = format;
            d.Usage       = TextureUsage::RenderTarget | TextureUsage::CopySource;
            return GPU->CreateTexture(d);
        }
        Handle<Buffer> Upload(const void* data, size_t bytes, BufferUsage usage = BufferUsage::CopySource)
        {
            auto buffer = GPU->CreateBuffer({bytes, usage, MemoryUsage::Upload});
            buffer->Write(0, data, bytes);
            return buffer;
        }
        Handle<Buffer> Readback(size_t bytes)
        {
            return GPU->CreateBuffer({bytes, BufferUsage::CopyDestination, MemoryUsage::Readback});
        }
        RenderingDesc
        Render(const Handle<Texture>& target, std::array<float, 4> clear = {0, 0, 0, 1}, Handle<Texture> depth = {})
        {
            RenderingDesc r;
            r.Colors.push_back({target, {}, LoadOp::Clear, StoreOp::Store, clear});
            r.Depth.Target = depth;
            return r;
        }
        Handle<CommandList> Begin(const RenderingDesc& desc)
        {
            auto commands = GPU->CreateCommandList();
            commands->BeginRendering(desc);
            commands->SetViewport({0, 0, float(Size), float(Size), 0, 1});
            return commands;
        }
        Constants NativeConstants(Constants value)
        {
            // Tests express depth in [-1,1]; translate using the same device
            // parameter that future callers use to build their projections.
            if (GPU->GetCoordinateConventions().Depth == DepthRange::ZeroToOne)
                value.Params[0] = value.Params[0] * .5f + .5f;
            return value;
        }
        uint32_t ClipRow(uint32_t upwardRow)
        {
            const auto c = GPU->GetCoordinateConventions();
            const bool positiveAtHighRow =
                (c.ClipY == ClipYDirection::Up) == (c.Origin == FramebufferOrigin::LowerLeft);
            return positiveAtHighRow ? upwardRow : Size - 1 - upwardRow;
        }
        void Draw(const Handle<CommandList>& commands, const Handle<Pipeline>& pipeline, Constants constants = {})
        {
            constants = NativeConstants(constants);
            commands->BindPipeline(pipeline);
            commands->PushConstants(0, &constants, sizeof(constants));
            commands->Draw(3);
        }
        std::vector<uint8_t>
        Finish(const Handle<CommandList>& commands, const Handle<Texture>& target, size_t bytes = Size * Size * 4)
        {
            commands->EndRendering();
            auto output = Readback(bytes);
            commands->CopyTextureToBuffer(target, {}, output);
            GPU->Submit(commands);
            GPU->WaitIdle();
            return output->Read(0, bytes);
        }
        Pixel At(const std::vector<uint8_t>& bytes, uint32_t x, uint32_t y)
        {
            const auto i = (y * Size + x) * 4;
            return {bytes.at(i), bytes.at(i + 1), bytes.at(i + 2), bytes.at(i + 3)};
        }
        void Solid(const std::vector<uint8_t>& bytes, Pixel expected)
        {
            ASSERT_EQ(bytes.size(), Size * Size * 4);
            for (uint32_t y = 0; y < Size; ++y)
                for (uint32_t x = 0; x < Size; ++x)
                    ASSERT_EQ(At(bytes, x, y), expected) << "pixel " << x << "," << y;
        }
    };

    TEST_P(RHIConformance, BufferUploadCopyReadbackRanges)
    {
        // Catches ignored source/destination offsets, truncated copies and writes.
        std::array<uint8_t, 96> pattern;
        for (size_t i = 0; i < pattern.size(); ++i)
            pattern[i] = uint8_t(i * 37 + 11);
        auto          upload  = Upload(pattern.data(), pattern.size());
        const uint8_t patch[] = {9, 8, 7, 6};
        upload->Write(19, patch, sizeof(patch));
        std::memcpy(pattern.data() + 19, patch, sizeof(patch));
        auto local =
            GPU->CreateBuffer({128, BufferUsage::CopySource | BufferUsage::CopyDestination, MemoryUsage::DeviceLocal});
        auto output   = Readback(128);
        auto commands = GPU->CreateCommandList();
        commands->CopyBuffer(upload, 11, local, 23, 67);
        commands->CopyBuffer(local, 23, output, 7, 67);
        EXPECT_THROW(output->Read(7, 67), std::invalid_argument);
        GPU->Submit(commands);
        EXPECT_THROW(output->Read(7, 67), std::invalid_argument);
        GPU->WaitIdle();
        EXPECT_EQ(output->Read(7, 67), (std::vector<uint8_t>(pattern.begin() + 11, pattern.begin() + 78)));
    }

    TEST_P(RHIConformance, EveryMipAndCubeFaceRoundTrips)
    {
        // Unique bytes per pixel/mip/face catch swapped faces, mip zero copies,
        // wrong extent/offset, row alignment and a flipped row origin.
        for (auto dimension : {TextureDimension::Texture2D, TextureDimension::Cube})
        {
            TextureDesc d;
            d.Width                = 8;
            d.Height               = 8;
            d.MipLevels            = 4;
            d.Dimension            = dimension;
            d.Usage                = TextureUsage::CopySource | TextureUsage::CopyDestination;
            auto           texture = GPU->CreateTexture(d);
            const uint32_t faces   = dimension == TextureDimension::Cube ? 6 : 1;
            struct Copy
            {
                uint32_t             Face, Mip;
                std::vector<uint8_t> Expected;
                Handle<Buffer>       Output;
            };
            std::vector<Copy> copies;
            auto              commands = GPU->CreateCommandList();
            for (uint32_t face = 0; face < faces; ++face)
                for (uint32_t mip = 0; mip < 4; ++mip)
                {
                    const size_t         count = size_t(8 >> mip) * (8 >> mip) * 4;
                    std::vector<uint8_t> pattern(count + 16);
                    for (size_t i = 0; i < count; ++i)
                        pattern[i + 16] = uint8_t(i * 17 + face * 29 + mip * 53);
                    auto source = Upload(pattern.data(), pattern.size());
                    commands->CopyBufferToTexture(source, 16, texture, {mip, face});
                    copies.push_back({face, mip, {pattern.begin() + 16, pattern.end()}, Readback(count + 8)});
                }
            // Read only after all uploads: a shared wrong-subresource mapping in
            // both copy directions must not cancel out and falsely pass.
            for (const auto& c : copies)
                commands->CopyTextureToBuffer(texture, {c.Mip, c.Face}, c.Output, 8);
            GPU->Submit(commands);
            GPU->WaitIdle();
            for (const auto& c : copies)
                EXPECT_EQ(c.Output->Read(8, c.Expected.size()), c.Expected) << "face " << c.Face << " mip " << c.Mip;
        }
    }

    TEST_P(RHIConformance, OddWidthSingleChannelRows)
    {
        TextureDesc d;
        d.Width       = 3;
        d.Height      = 5;
        d.PixelFormat = Format::R8Unorm;
        d.Usage       = TextureUsage::CopySource | TextureUsage::CopyDestination;
        const std::array<uint8_t, 15> pattern {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        auto                          texture  = GPU->CreateTexture(d);
        auto                          source   = Upload(pattern.data(), pattern.size());
        auto                          output   = Readback(pattern.size());
        auto                          commands = GPU->CreateCommandList();
        commands->CopyBufferToTexture(source, 0, texture);
        commands->CopyTextureToBuffer(texture, {}, output);
        GPU->Submit(commands);
        GPU->WaitIdle();
        EXPECT_EQ(output->Read(0, 15), std::vector<uint8_t>(pattern.begin(), pattern.end()));
    }

    TEST_P(RHIConformance, TriangleCoverageAndExactColor)
    {
        Program       = ShaderFrom(R"(#version 450
layout(push_constant) uniform Object { vec4 Color; vec4 Params; } p;
void main() { vec2 pos[3]=vec2[](vec2(-.75,-.75),vec2(.75,-.75),vec2(0,.75)); gl_Position=vec4(pos[gl_VertexIndex],0,1); })",
                                   Fragment);
        auto target   = Target();
        auto commands = Begin(Render(target));
        Draw(commands, GPU->CreatePipeline(Description()), {{1, 0.5f, 0, 1}, {}});
        const auto bytes = Finish(commands, target);
        // Away from boundary: catches missing draw, wrong viewport, topology or shader.
        EXPECT_EQ(At(bytes, 8, ClipRow(5)), (Pixel {255, 128, 0, 255}));
        EXPECT_EQ(At(bytes, 0, ClipRow(0)), (Pixel {0, 0, 0, 255}));
        EXPECT_EQ(At(bytes, 8, ClipRow(11)), (Pixel {255, 128, 0, 255}));
        EXPECT_EQ(At(bytes, 2, ClipRow(13)), (Pixel {0, 0, 0, 255}));
    }

    TEST_P(RHIConformance, DepthTestWritesAndReadback)
    {
        TextureDesc dd;
        dd.Width                  = Size;
        dd.Height                 = Size;
        dd.PixelFormat            = Format::Depth32Float;
        dd.Usage                  = TextureUsage::DepthStencil | TextureUsage::CopySource;
        auto depth                = GPU->CreateTexture(dd);
        auto target               = Target();
        auto d                    = Description();
        d.HasDepthStencil         = true;
        d.DepthStencil.DepthTest  = true;
        d.DepthStencil.DepthWrite = true;
        auto write                = GPU->CreatePipeline(d);
        d.DepthStencil.DepthWrite = false;
        auto noWrite              = GPU->CreatePipeline(d);
        auto commands             = Begin(Render(target, {0, 0, 0, 1}, depth));
        Draw(commands, write, {{0, 0, 1, 1}, {0.5f, 0, 0, 0}});    // depth .75
        Draw(commands, noWrite, {{1, 0, 0, 1}, {-0.5f, 0, 0, 0}}); // depth .25, must not write
        Draw(commands, write, {{0, 1, 0, 1}, {0, 0, 0, 0}});       // depth .5, passes only if prior write disabled
        Draw(commands, write, {{1, 0, 1, 1}, {0.75f, 0, 0, 0}});   // depth .875, must fail
        commands->EndRendering();
        auto colors = Readback(Size * Size * 4), depths = Readback(Size * Size * 4);
        commands->CopyTextureToBuffer(target, {}, colors);
        commands->CopyTextureToBuffer(depth, {}, depths);
        GPU->Submit(commands);
        GPU->WaitIdle();
        Solid(colors->Read(0, Size * Size * 4), {0, 255, 0, 255});
        auto raw = depths->Read(0, Size * Size * 4);
        for (size_t i = 0; i < raw.size(); i += 4)
        {
            float value;
            std::memcpy(&value, raw.data() + i, 4);
            EXPECT_FLOAT_EQ(value, 0.5f);
        }
    }

    TEST_P(RHIConformance, DepthCompareAndBias)
    {
        TextureDesc dd;
        dd.Width                       = Size;
        dd.Height                      = Size;
        dd.PixelFormat                 = Format::Depth32Float;
        dd.Usage                       = TextureUsage::DepthStencil;
        auto depth                     = GPU->CreateTexture(dd);
        auto target                    = Target();
        auto d                         = Description();
        d.HasDepthStencil              = true;
        d.DepthStencil.DepthTest       = true;
        d.DepthStencil.DepthWrite      = true;
        auto base                      = GPU->CreatePipeline(d);
        d.DepthStencil.DepthCompare    = CompareOp::LessEqual;
        auto equal                     = GPU->CreatePipeline(d);
        d.DepthStencil.DepthCompare    = CompareOp::Less;
        d.Rasterizer.DepthBiasConstant = -16;
        auto biased                    = GPU->CreatePipeline(d);
        auto commands                  = Begin(Render(target, {0, 0, 0, 1}, depth));
        Draw(commands, base);
        commands->SetScissor({0, 0, 8, 16});
        Draw(commands, equal, {{0, 1, 0, 1}, {}});
        commands->SetScissor({8, 0, 8, 16});
        Draw(commands, biased, {{0, 0, 1, 1}, {}});
        // Equal depth fails Less, but the negative bias must make this draw pass.
        auto raw = Finish(commands, target);
        EXPECT_EQ(At(raw, 3, 8), (Pixel {0, 255, 0, 255}));
        EXPECT_EQ(At(raw, 12, 8), (Pixel {0, 0, 255, 255}));
    }

    TEST_P(RHIConformance, SeparateColorAlphaBlendAndWriteMask)
    {
        auto  target       = Target();
        auto  d            = Description();
        auto& b            = d.Blends[0];
        b.Enabled          = true;
        b.SourceColor      = BlendFactor::SourceAlpha;
        b.DestinationColor = BlendFactor::OneMinusSourceAlpha;
        b.SourceAlpha      = BlendFactor::Zero;
        b.DestinationAlpha = BlendFactor::One;
        b.WriteMask        = 1 | 2 | 8;
        auto commands      = Begin(Render(target, {0, 0, 1, 1}));
        Draw(commands, GPU->CreatePipeline(d), {{1, 1, 0, 0.25f}, {}});
        // Source alpha must blend RGB but preserve destination alpha and masked blue.
        Solid(Finish(commands, target), {64, 64, 255, 255});
    }

    TEST_P(RHIConformance, BlendEquationsAndDisableTransition)
    {
        auto  target       = Target();
        auto  d            = Description();
        auto& b            = d.Blends[0];
        b.Enabled          = true;
        b.SourceColor      = BlendFactor::One;
        b.DestinationColor = BlendFactor::One;
        b.ColorOp          = BlendOp::ReverseSubtract;
        b.SourceAlpha      = BlendFactor::One;
        b.DestinationAlpha = BlendFactor::Zero;
        auto subtract      = GPU->CreatePipeline(d);
        b.Enabled          = false;
        auto opaque        = GPU->CreatePipeline(d);
        auto commands      = Begin(Render(target, {1, 1, 1, 1}));
        Draw(commands, subtract, {{0.25f, 0.5f, 1, 0.5f}, {}});
        commands->SetScissor({8, 0, 8, 16});
        Draw(commands, opaque, {{0, 1, 0, 1}, {}});
        auto raw = Finish(commands, target);
        EXPECT_EQ(At(raw, 3, 8), (Pixel {191, 127, 0, 128}));
        EXPECT_EQ(At(raw, 12, 8), (Pixel {0, 255, 0, 255}));
    }

    TEST_P(RHIConformance, CullFrontBackAndWinding)
    {
        // The oversized triangle is CCW when clip Y points up. Each case catches inverted
        // front face, ignored cull mode, or stale enable state after pipeline changes.
        auto target = Target();
        for (auto front : {FrontFace::CounterClockwise, FrontFace::Clockwise})
            for (auto cull : {CullMode::None, CullMode::Back, CullMode::Front})
            {
                auto d             = Description();
                d.Rasterizer.Cull  = cull;
                d.Rasterizer.Front = front;
                auto commands      = Begin(Render(target));
                Draw(commands, GPU->CreatePipeline(d));
                const bool triangleFront = (front == FrontFace::CounterClockwise) ==
                                           (GPU->GetCoordinateConventions().ClipY == ClipYDirection::Up);
                const bool visible       = cull == CullMode::None || (cull == CullMode::Back) == triangleFront;
                Solid(Finish(commands, target), visible ? Pixel {255, 0, 0, 255} : Pixel {0, 0, 0, 255});
            }
    }

    TEST_P(RHIConformance, WireframeAndFillTransition)
    {
        Program              = ShaderFrom(R"(#version 450
void main() { vec2 pos[3]=vec2[](vec2(-.75,-.75),vec2(.75,-.75),vec2(0,.75)); gl_Position=vec4(pos[gl_VertexIndex],0,1); })",
                                          Fragment);
        auto target          = Target();
        auto d               = Description();
        d.Rasterizer.Polygon = PolygonMode::Line;
        auto wire            = GPU->CreatePipeline(d);
        d.Rasterizer.Polygon = PolygonMode::Fill;
        auto fill            = GPU->CreatePipeline(d);
        auto commands        = Begin(Render(target));
        Draw(commands, wire);
        const auto raw = Finish(commands, target);
        EXPECT_EQ(At(raw, 8, 6), (Pixel {0, 0, 0, 255})); // interior must stay untouched
        // Horizontal edge lies at y=2; diamond-exit rasterization covers row 1.
        EXPECT_EQ(At(raw, 7, ClipRow(1)), (Pixel {255, 0, 0, 255}));
        commands = Begin(Render(target));
        Draw(commands, fill);
        EXPECT_EQ(At(Finish(commands, target), 8, 6), (Pixel {255, 0, 0, 255}));
    }

    TEST_P(RHIConformance, ScissorAndViewportBounds)
    {
        auto target   = Target();
        auto commands = Begin(Render(target));
        commands->SetViewport({2, 3, 10, 9, 0, 1});
        commands->SetScissor({5, 1, 4, 7});
        Draw(commands, GPU->CreatePipeline(Description()));
        auto raw = Finish(commands, target);
        // The intersection is [5,9) x [3,8), including exclusive upper bounds.
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
                EXPECT_EQ(At(raw, x, y),
                          (x >= 5 && x < 9 && y >= 3 && y < 8 ? Pixel {255, 0, 0, 255} : Pixel {0, 0, 0, 255}))
                    << x << "," << y;
    }

    TEST_P(RHIConformance, PushConstantsPartialWritesAndRecordedValues)
    {
        Program            = ShaderFrom(Vertex, R"(#version 450
layout(push_constant) uniform Object { vec4 Color; vec4 Params; } p;
layout(location=0) out vec4 color;
void main() { color=p.Color*p.Params.w; })");
        auto      target   = Target();
        auto      commands = Begin(Render(target));
        auto      p        = GPU->CreatePipeline(Description());
        Constants constants;
        constants.Params[3] = 1; // A partial Color update must preserve this value.
        commands->BindPipeline(p);
        commands->PushConstants(0, &constants, sizeof(constants));
        commands->SetScissor({0, 0, 8, 16});
        commands->Draw(3);
        std::array<float, 4> green {0, 1, 0, 1};
        commands->PushConstants(0, green.data(), 16);
        green = {0, 0, 1, 1}; // Record must own a snapshot of the supplied bytes.
        commands->SetScissor({8, 0, 8, 16});
        commands->Draw(3);
        auto raw = Finish(commands, target);
        EXPECT_EQ(At(raw, 3, 8), (Pixel {255, 0, 0, 255}));
        EXPECT_EQ(At(raw, 12, 8), (Pixel {0, 255, 0, 255}));
    }

    TEST_P(RHIConformance, TwoGroupsWithCollidingSlotsAndCombinedSamplers)
    {
        Program = ShaderFrom(Vertex, R"(#version 450
layout(set=0,binding=0,std140) uniform Frame { vec4 Value; } a;
layout(set=1,binding=0,std140) uniform Material { vec4 Value; } b;
layout(set=0,binding=1) uniform sampler2D first;
layout(set=1,binding=1) uniform sampler2D second;
layout(location=0) out vec4 color;
void main() { color=a.Value*texture(first,vec2(.625,.25))+b.Value*texture(second,vec2(.625,.25)); })");
        std::vector<Handle<BindGroupLayout>> layouts;
        std::vector<Handle<BindGroup>>       groups;
        auto                                 commands = GPU->CreateCommandList();
        for (uint32_t g = 0; g < 2; ++g)
        {
            auto l = GPU->CreateBindGroupLayout(
                {g, {{0, BindingType::UniformBuffer}, {1, BindingType::CombinedTextureSampler}}});
            layouts.push_back(l);
            std::array<float, 4> values =
                g == 0 ? std::array<float, 4> {1, 0, 0, 0} : std::array<float, 4> {0, 1, 0, 1};
            const auto           offset = GPU->GetBufferBindingAlignment(BindingType::UniformBuffer);
            std::vector<uint8_t> padded(offset + sizeof(values), 0);
            std::memcpy(padded.data() + offset, values.data(), sizeof(values));
            auto uniform = Upload(padded.data(), padded.size(), BufferUsage::Uniform);
            // Two texels make nearest filtering and repeat addressing observable.
            std::array<uint8_t, 8> pixels = g == 0 ? std::array<uint8_t, 8> {0, 0, 0, 255, 255, 0, 0, 255} :
                                                     std::array<uint8_t, 8> {0, 0, 0, 255, 0, 128, 0, 255};
            TextureDesc            td;
            td.Width    = 2;
            td.Usage    = TextureUsage::Sampled | TextureUsage::CopyDestination;
            auto t      = GPU->CreateTexture(td);
            auto upload = Upload(pixels.data(), pixels.size());
            commands->CopyBufferToTexture(upload, 0, t);
            SamplerDesc sd;
            sd.Min = sd.Mag = sd.Mip = Filter::Nearest;
            auto           sampler   = GPU->CreateSampler(sd);
            BindGroupEntry u;
            u.Slot           = 0;
            u.BufferResource = uniform;
            u.Offset         = offset;
            u.Bytes          = sizeof(values); // Reading offset zero would yield the zero padding.
            BindGroupEntry image;
            image.Slot            = 1;
            image.TextureResource = t;
            image.SamplerResource = sampler;
            groups.push_back(GPU->CreateBindGroup({l, {u, image}}));
        }
        auto target = Target();
        commands->BeginRendering(Render(target));
        commands->SetViewport({0, 0, 16, 16, 0, 1});
        auto d         = Description();
        d.GroupLayouts = layouts;
        commands->BindPipeline(GPU->CreatePipeline(d));
        for (auto g : groups)
            commands->BindGroup(g);
        Constants constants;
        commands->PushConstants(0, &constants, sizeof(constants));
        commands->Draw(3);
        Solid(Finish(commands, target), {255, 128, 0, 255});
    }

    TEST_P(RHIConformance, SeparateTextureAndSamplerBindings)
    {
        Program     = ShaderFrom(Vertex, R"(#version 450
layout(set=1,binding=2) uniform texture2D image;
layout(set=1,binding=5) uniform sampler filtering;
layout(location=0) out vec4 color;
void main() { color=texture(sampler2D(image,filtering),vec2(1.125,.5)); })");
        auto layout = GPU->CreateBindGroupLayout({1, {{2, BindingType::SampledTexture}, {5, BindingType::Sampler}}});
        TextureDesc td;
        td.Width               = 2;
        td.Usage               = TextureUsage::Sampled | TextureUsage::CopyDestination;
        auto          texture  = GPU->CreateTexture(td);
        const uint8_t pixels[] = {0, 0, 255, 255, 255, 0, 0, 255};
        auto          upload   = Upload(pixels, sizeof(pixels));
        auto          commands = GPU->CreateCommandList();
        commands->CopyBufferToTexture(upload, 0, texture);
        SamplerDesc sd;
        sd.Min = sd.Mag = sd.Mip = Filter::Nearest;
        sd.U                     = AddressMode::Repeat;
        BindGroupEntry t;
        t.Slot            = 2;
        t.TextureResource = texture;
        BindGroupEntry s;
        s.Slot            = 5;
        s.SamplerResource = GPU->CreateSampler(sd);
        auto group        = GPU->CreateBindGroup({layout, {t, s}});
        auto target       = Target();
        commands->BeginRendering(Render(target));
        commands->SetViewport({0, 0, 16, 16, 0, 1});
        auto d         = Description();
        d.GroupLayouts = {layout};
        commands->BindPipeline(GPU->CreatePipeline(d));
        commands->BindGroup(group);
        Constants c;
        commands->PushConstants(0, &c, sizeof(c));
        commands->Draw(3);
        Solid(Finish(commands, target), {0, 0, 255, 255});
    }

    TEST_P(RHIConformance, IntegerPickerTargetAndClear)
    {
        Program               = ShaderFrom(Vertex, R"(#version 450
layout(location=0) out uint id;
void main() { id=0x87654321u; })");
        auto target           = Target(Format::R32Uint);
        auto r                = Render(target);
        r.Colors[0].ClearUint = 0xf1234567u;
        auto commands         = Begin(r);
        commands->SetScissor({0, 0, 8, 16});
        Draw(commands, GPU->CreatePipeline(Description(Format::R32Uint)));
        auto raw = Finish(commands, target);
        // Values above 2^24 catch float conversions; clear outside the scissor catches signed/float clear paths.
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 16; ++x)
            {
                uint32_t value;
                std::memcpy(&value, raw.data() + (y * 16 + x) * 4, 4);
                EXPECT_EQ(value, x < 8 ? 0x87654321u : 0xf1234567u);
            }
    }

    TEST_P(RHIConformance, HalfFloatTargetPreservesHDRAndNegative)
    {
        auto target   = Target(Format::RGBA16Float);
        auto commands = Begin(Render(target));
        Draw(commands, GPU->CreatePipeline(Description(Format::RGBA16Float)), {{2, -0.5f, 0.25f, 1}, {}});
        auto raw = Finish(commands, target, Size * Size * 8);
        // These values have exact binary16 representations. Catches normalized
        // storage/readback, clamping, wrong external type and channel stride.
        const std::array<uint16_t, 4> expected {0x4000, 0xb800, 0x3400, 0x3c00};
        for (size_t i = 0; i < raw.size(); i += 8)
        {
            std::array<uint16_t, 4> value;
            std::memcpy(value.data(), raw.data() + i, 8);
            EXPECT_EQ(value, expected);
        }
    }

    TEST_P(RHIConformance, PipelineCacheIncludesEveryStateField)
    {
        auto base               = Description();
        base.HasDepthStencil    = true;
        base.DepthStencilFormat = Format::Depth24Stencil8;
        auto pipeline           = GPU->CreatePipeline(base);
        EXPECT_EQ(pipeline, GPU->CreatePipeline(base));
        // One-field changes expose omitted hash fields rather than depending on
        // a single convenient difference such as shader identity.
        std::vector<std::function<void(PipelineDesc&)>> changes {
            [](auto& d) { d.Rasterizer.Cull = CullMode::Front; },
            [](auto& d) { d.Rasterizer.Front = FrontFace::Clockwise; },
            [](auto& d) { d.Rasterizer.Polygon = PolygonMode::Line; },
            [](auto& d) { d.Rasterizer.DepthBiasConstant = 1; },
            [](auto& d) { d.Rasterizer.DepthBiasSlope = 1; },
            [](auto& d) { d.DepthStencil.DepthTest = true; },
            [](auto& d) { d.DepthStencil.DepthWrite = true; },
            [](auto& d) { d.DepthStencil.DepthCompare = CompareOp::Always; },
            [](auto& d) { d.DepthStencil.StencilTest = true; },
            [](auto& d) { d.DepthStencil.Front.Reference = 3; },
            [](auto& d) { d.DepthStencil.Back.ReadMask = 3; },
            [](auto& d) { d.DepthStencil.Front.WriteMask = 3; },
            [](auto& d) { d.DepthStencil.Back.Compare = CompareOp::Equal; },
            [](auto& d) { d.DepthStencil.Front.Pass = StencilOp::Replace; },
            [](auto& d) { d.DepthStencil.Back.Fail = StencilOp::Zero; },
            [](auto& d) { d.DepthStencil.Front.DepthFail = StencilOp::Invert; },
            [](auto& d) { d.Blends[0].Enabled = true; },
            [](auto& d) { d.Blends[0].WriteMask = 7; },
            [](auto& d) { d.Blends[0].SourceColor = BlendFactor::SourceAlpha; },
            [](auto& d) { d.Blends[0].DestinationColor = BlendFactor::One; },
            [](auto& d) { d.Blends[0].ColorOp = BlendOp::Max; },
            [](auto& d) { d.Blends[0].SourceAlpha = BlendFactor::Zero; },
            [](auto& d) { d.Blends[0].DestinationAlpha = BlendFactor::One; },
            [](auto& d) { d.Blends[0].AlphaOp = BlendOp::Min; },
            [](auto& d) { d.Primitive = Topology::TriangleStrip; },
            [](auto& d) { d.PushConstantBytes = 48; },
            [](auto& d) { d.ColorFormats[0] = Format::RGBA16Float; },
            [](auto& d) { d.DepthStencilFormat = Format::Depth32Float; },
            [](auto& d) { d.VertexBindings = {{0, 12, false}}; },
            [](auto& d) {
                d.VertexBindings = {{0, 12, false}};
                d.Attributes     = {{0, 0, 0, VertexFormat::Float3}};
            }};
        for (size_t i = 0; i < changes.size(); ++i)
        {
            auto other = base;
            changes[i](other);
            auto changed = GPU->CreatePipeline(other);
            EXPECT_NE(pipeline, changed) << "state " << i;
            EXPECT_EQ(changed, GPU->CreatePipeline(other));
        }
    }

    TEST_P(RHIConformance, PipelineCacheIncludesVertexAndBindingLayouts)
    {
        auto base                                                      = Description();
        base.VertexBindings                                            = {{0, 16, false}};
        base.Attributes                                                = {{0, 0, 0, VertexFormat::Float2}};
        auto                                                  original = GPU->CreatePipeline(base);
        const std::vector<std::function<void(PipelineDesc&)>> mutations {
            [](auto& d) { d.VertexBindings[0].Stride = 20; },
            [](auto& d) { d.VertexBindings[0].PerInstance = true; },
            [](auto& d) {
                d.VertexBindings[0].Slot   = 1;
                d.Attributes[0].BufferSlot = 1;
            },
            [](auto& d) { d.Attributes[0].Location = 1; },
            [](auto& d) { d.Attributes[0].Offset = 4; },
            [](auto& d) { d.Attributes[0].Type = VertexFormat::Float3; }};
        for (const auto& mutate : mutations)
        {
            auto d = base;
            mutate(d);
            EXPECT_NE(original, GPU->CreatePipeline(d));
        }
        base.GroupLayouts = {GPU->CreateBindGroupLayout({0, {{0, BindingType::UniformBuffer}}})};
        auto withGroup    = GPU->CreatePipeline(base);
        // Equivalent layout descriptions must share a pipeline even when their
        // handles differ; changing a slot/type/group must change the cache key.
        base.GroupLayouts = {GPU->CreateBindGroupLayout({0, {{0, BindingType::UniformBuffer}}})};
        EXPECT_EQ(withGroup, GPU->CreatePipeline(base));
        for (auto layout : {BindGroupLayoutDesc {0, {{1, BindingType::UniformBuffer}}},
                            BindGroupLayoutDesc {0, {{0, BindingType::StorageBuffer}}},
                            BindGroupLayoutDesc {1, {{0, BindingType::UniformBuffer}}}})
        {
            base.GroupLayouts = {GPU->CreateBindGroupLayout(layout)};
            EXPECT_NE(withGroup, GPU->CreatePipeline(base));
        }
        base         = Description();
        original     = GPU->CreatePipeline(base);
        base.Program = ShaderFrom(Vertex, Fragment);
        EXPECT_NE(original, GPU->CreatePipeline(base));
    }

    TEST_P(RHIConformance, ReleasedAllocationsSurviveUntilFrameRetires)
    {
        GPU->WaitIdle();
        const auto before = GPU->GetStats();
        auto       buffer = GPU->CreateBuffer({32, BufferUsage::CopySource, MemoryUsage::Upload});
        EXPECT_EQ(GPU->GetStats().LiveResources, before.LiveResources + 1);
        buffer.reset();
        // Native stats catch immediate deletion even for a resource never referenced
        // by a command. A shared_ptr/weak_ptr test alone would miss that bug.
        EXPECT_EQ(GPU->GetStats().LiveResources, before.LiveResources + 1);
        EXPECT_EQ(GPU->GetStats().PendingDestructions, 1u);
        GPU->EndFrame();
        EXPECT_EQ(GPU->GetStats().LiveResources, before.LiveResources);
        EXPECT_EQ(GPU->GetStats().PendingDestructions, 0u);
        GPU->BeginFrame();
    }

    TEST_P(RHIConformance, RecordedAndSubmittedResourcesAreRetained)
    {
        const std::array<uint8_t, 4> pattern {17, 93, 201, 44};
        auto                         source   = Upload(pattern.data(), 4);
        auto                         output   = Readback(4);
        std::weak_ptr<Buffer>        lifetime = source;
        auto                         commands = GPU->CreateCommandList();
        commands->CopyBuffer(source, 0, output, 0, 4);
        source.reset();
        EXPECT_FALSE(lifetime.expired()); // pending replay must own its resources
        GPU->Submit(commands);
        commands.reset();
        EXPECT_FALSE(lifetime.expired()); // frame ownership, not caller's command-list handle
        GPU->EndFrame();
        EXPECT_TRUE(lifetime.expired());
        EXPECT_EQ(output->Read(0, 4), std::vector<uint8_t>(pattern.begin(), pattern.end()));
        GPU->BeginFrame();
    }

    TEST_P(RHIConformance, CoordinateQueryMatchesCoverageDepthAndRows)
    {
        const auto convention = GPU->GetCoordinateConventions();
        Program               = ShaderFrom(R"(#version 450
void main() { vec2 pos[3]=vec2[](vec2(-.75,.125),vec2(.75,.125),vec2(0,.875)); gl_Position=vec4(pos[gl_VertexIndex],.25,1); })",
                                           Fragment);
        auto        target    = Target();
        TextureDesc dd;
        dd.Width                  = Size;
        dd.Height                 = Size;
        dd.PixelFormat            = Format::Depth32Float;
        dd.Usage                  = TextureUsage::DepthStencil | TextureUsage::CopySource;
        auto depth                = GPU->CreateTexture(dd);
        auto d                    = Description();
        d.HasDepthStencil         = true;
        d.DepthStencil.DepthTest  = true;
        d.DepthStencil.DepthWrite = true;
        auto commands             = Begin(Render(target, {0, 0, 0, 1}, depth));
        Draw(commands, GPU->CreatePipeline(d));
        commands->EndRendering();
        auto colorOutput = Readback(Size * Size * 4), depthOutput = Readback(Size * Size * 4);
        commands->CopyTextureToBuffer(target, {}, colorOutput);
        commands->CopyTextureToBuffer(depth, {}, depthOutput);
        GPU->Submit(commands);
        GPU->WaitIdle();
        // Convert native row index to upward clip-space orientation using both
        // parameters: changing either query alone must break this expectation.
        const bool positiveAtHighRow =
            (convention.ClipY == ClipYDirection::Up) == (convention.Origin == FramebufferOrigin::LowerLeft);
        const uint32_t coveredRow = positiveAtHighRow ? 11 : 4, emptyRow = 15 - coveredRow;
        auto           colors = colorOutput->Read(0, Size * Size * 4);
        EXPECT_EQ(At(colors, 8, coveredRow), (Pixel {255, 0, 0, 255}));
        EXPECT_EQ(At(colors, 8, emptyRow), (Pixel {0, 0, 0, 255}));
        auto  raw = depthOutput->Read(0, Size * Size * 4);
        float z;
        std::memcpy(&z, raw.data() + (coveredRow * Size + 8) * 4, 4);
        EXPECT_FLOAT_EQ(z, convention.Depth == DepthRange::MinusOneToOne ? .625f : .25f);
        // Scissor coordinates must follow the reported framebuffer origin too.
        commands = Begin(Render(target));
        commands->SetScissor({0, 0, 16, 4});
        Program = ShaderFrom(Vertex, Fragment);
        Draw(commands, GPU->CreatePipeline(Description()));
        colors = Finish(commands, target);
        EXPECT_EQ(At(colors, 8, 1), (Pixel {255, 0, 0, 255}));
        EXPECT_EQ(At(colors, 8, 14), (Pixel {0, 0, 0, 255}));
    }

    TEST_P(RHIConformance, IndexedVertexLayoutOffsetsAndBaseVertex)
    {
        Program = ShaderFrom(R"(#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 vTint;
void main() { gl_Position=vec4(position,0,1); vTint=tint; })",
                             R"(#version 450
layout(location=0) in vec4 vTint;
layout(location=0) out vec4 color;
void main() { color=vTint; })");
        struct VertexData
        {
            float                  Dummy, X, Y;
            std::array<uint8_t, 4> Color;
        };
        const VertexData vertices[] = {{77, 0, 0, {0, 0, 255, 255}},
                                       {77, -1, -1, {64, 128, 192, 255}},
                                       {77, 3, -1, {64, 128, 192, 255}},
                                       {77, -1, 3, {64, 128, 192, 255}}};
        // One prefix vertex tests the view offset. A second skipped vertex tests
        // baseVertex, while the attribute offset skips Dummy within each stride.
        std::vector<uint8_t> data(sizeof(vertices) + sizeof(VertexData), 0);
        std::memcpy(data.data() + sizeof(VertexData), vertices, sizeof(vertices));
        auto buffer = Upload(data.data(), data.size(), BufferUsage::Vertex);
        for (auto type : {IndexType::Uint16, IndexType::Uint32})
        {
            const uint16_t narrow[] = {99, 99, 0, 1, 2};
            const uint32_t wide[]   = {99, 99, 0, 1, 2};
            auto           index    = type == IndexType::Uint16 ? Upload(narrow, sizeof(narrow), BufferUsage::Index) :
                                                                  Upload(wide, sizeof(wide), BufferUsage::Index);
            auto           d        = Description();
            d.PushConstantBytes     = 0;
            d.VertexBindings        = {{3, sizeof(VertexData), false}};
            d.Attributes            = {{0, 3, 4, VertexFormat::Float2}, {1, 3, 12, VertexFormat::Unorm8x4}};
            auto target             = Target();
            auto commands           = Begin(Render(target));
            commands->BindPipeline(GPU->CreatePipeline(d));
            commands->BindVertexBuffers({{3, buffer, sizeof(VertexData)}});
            const size_t element = type == IndexType::Uint16 ? 2 : 4;
            commands->BindIndexBuffer(index, type, element);
            commands->DrawIndexed(3, 1, 1, 1);
            // Intermediate channels detect missing UNORM normalization: raw
            // byte values would saturate all three channels to white.
            Solid(Finish(commands, target), {64, 128, 192, 255});
        }
    }

    TEST_P(RHIConformance, AttachmentLoadAndClearIgnorePreviousMasks)
    {
        auto target           = Target();
        auto d                = Description();
        d.Blends[0].WriteMask = 1;
        auto commands         = Begin(Render(target));
        commands->SetScissor({0, 0, 4, 4});
        Draw(commands, GPU->CreatePipeline(d));
        commands->EndRendering();
        // Both mask and scissor must be reset before a later clear.
        commands->BeginRendering(Render(target, {0, 1, 1, 1}));
        auto raw = Finish(commands, target);
        Solid(raw, {0, 255, 255, 255});
        commands              = GPU->CreateCommandList();
        auto render           = Render(target);
        render.Colors[0].Load = LoadOp::Load;
        commands->BeginRendering(render);
        commands->SetViewport({0, 0, 16, 16, 0, 1});
        commands->SetScissor({0, 0, 8, 16});
        Draw(commands, GPU->CreatePipeline(Description()));
        raw = Finish(commands, target);
        EXPECT_EQ(At(raw, 3, 8), (Pixel {255, 0, 0, 255}));
        EXPECT_EQ(At(raw, 12, 8), (Pixel {0, 255, 255, 255}));
    }

    TEST_P(RHIConformance, IndependentBlendStatesForMultipleTargets)
    {
        Program    = ShaderFrom(Vertex, R"(#version 450
layout(location=0) out vec4 a;
layout(location=1) out vec4 b;
void main() { a=vec4(1,0,0,.25); b=vec4(0,1,0,.25); })");
        auto first = Target(), second = Target();
        auto r = Render(first, {0, 0, 1, 1});
        r.Colors.push_back({second, {}, LoadOp::Clear, StoreOp::Store, {0, 0, 1, 1}});
        auto d = Description();
        d.ColorFormats.push_back(Format::RGBA8Unorm);
        d.Blends.resize(2);
        d.Blends[0].Enabled          = true;
        d.Blends[0].SourceColor      = BlendFactor::SourceAlpha;
        d.Blends[0].DestinationColor = BlendFactor::OneMinusSourceAlpha;
        d.Blends[0].WriteMask        = 7;
        auto commands                = Begin(r);
        Draw(commands, GPU->CreatePipeline(d));
        commands->EndRendering();
        auto a = Readback(Size * Size * 4), b = Readback(Size * Size * 4);
        commands->CopyTextureToBuffer(first, {}, a);
        commands->CopyTextureToBuffer(second, {}, b);
        GPU->Submit(commands);
        GPU->WaitIdle();
        Solid(a->Read(0, Size * Size * 4), {64, 0, 191, 255});
        Solid(b->Read(0, Size * Size * 4), {0, 255, 0, 64});
    }

    TEST_P(RHIConformance, StencilReplaceCompareAndWriteMask)
    {
        TextureDesc td;
        td.Width                       = Size;
        td.Height                      = Size;
        td.PixelFormat                 = Format::Depth24Stencil8;
        td.Usage                       = TextureUsage::DepthStencil;
        auto depth                     = GPU->CreateTexture(td);
        auto target                    = Target();
        auto d                         = Description();
        d.HasDepthStencil              = true;
        d.DepthStencilFormat           = td.PixelFormat;
        d.DepthStencil.StencilTest     = true;
        d.DepthStencil.Front.Pass      = StencilOp::Replace;
        d.DepthStencil.Front.Reference = 5;
        d.DepthStencil.Back            = d.DepthStencil.Front;
        auto replace                   = GPU->CreatePipeline(d);
        // Mask 3 makes 5 become 1, so the final Equal reference must be 1.
        d.DepthStencil.Front.WriteMask = 3;
        d.DepthStencil.Back            = d.DepthStencil.Front;
        auto masked                    = GPU->CreatePipeline(d);
        d.DepthStencil.Front.Compare   = CompareOp::Equal;
        d.DepthStencil.Front.Reference = 1;
        d.DepthStencil.Front.Pass      = StencilOp::Keep;
        d.DepthStencil.Front.WriteMask = 0;
        d.DepthStencil.Back            = d.DepthStencil.Front;
        auto compare                   = GPU->CreatePipeline(d);
        auto commands                  = Begin(Render(target, {0, 0, 0, 1}, depth));
        commands->SetScissor({0, 0, 8, 16});
        Draw(commands, replace);
        commands->SetScissor({8, 0, 8, 16});
        Draw(commands, masked);
        commands->SetScissor({0, 0, 16, 16});
        Draw(commands, compare, {{0, 1, 0, 1}, {}});
        auto raw = Finish(commands, target);
        EXPECT_EQ(At(raw, 3, 8), (Pixel {255, 0, 0, 255}));
        EXPECT_EQ(At(raw, 12, 8), (Pixel {0, 255, 0, 255}));
    }

    TEST_P(RHIConformance, SrgbTargetEncodingAndLinearTransition)
    {
        auto srgb     = Target(Format::RGBA8Srgb);
        auto commands = Begin(Render(srgb));
        Draw(commands, GPU->CreatePipeline(Description(Format::RGBA8Srgb)), {{0.25f, 0.5f, 1, 1}, {}});
        auto raw = Finish(commands, srgb);
        // sRGB transfer at .25 and .5 rounds to 137 and 188. One encoded byte
        // tolerance accounts for the specified conversion precision on devices.
        auto pixel = At(raw, 8, 8);
        EXPECT_NEAR(pixel[0], 137, 1);
        EXPECT_NEAR(pixel[1], 188, 1);
        EXPECT_EQ(pixel[2], 255);
        EXPECT_EQ(pixel[3], 255);
        auto linear = Target();
        commands    = Begin(Render(linear));
        Draw(commands, GPU->CreatePipeline(Description()), {{0.25f, 0.5f, 1, 1}, {}});
        Solid(Finish(commands, linear), {64, 128, 255, 255});
    }

    TEST_P(RHIConformance, MipGenerationFiltersLevelZero)
    {
        TextureDesc td;
        td.Width = td.Height   = 2;
        td.MipLevels           = 2;
        td.Usage               = TextureUsage::CopySource | TextureUsage::CopyDestination;
        const uint8_t pixels[] = {0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 192, 192, 192, 255};
        auto          texture  = GPU->CreateTexture(td);
        auto          upload   = Upload(pixels, sizeof(pixels));
        auto          output   = Readback(4);
        auto          commands = GPU->CreateCommandList();
        commands->CopyBufferToTexture(upload, 0, texture);
        commands->GenerateMips(texture);
        commands->CopyTextureToBuffer(texture, {1, 0}, output);
        GPU->Submit(commands);
        GPU->WaitIdle();
        EXPECT_EQ(output->Read(0, 4), (std::vector<uint8_t> {96, 96, 96, 255}));
    }

    TEST_P(RHIConformance, StorageWritesFeedUniformsAndReadbackWithoutCallerBarriers)
    {
        auto writer = ShaderFrom(R"(#version 450
layout(set=1,binding=2,std430) buffer Storage { vec4 Color; } b;
void main() {
    vec2 positions[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
    if (gl_VertexIndex==0) b.Color=vec4(0,1,0,1);
})",
                                 R"(#version 450
layout(location=0) out vec4 color; void main() { color=vec4(1,0,0,1); })");
        auto reader = ShaderFrom(R"(#version 450
void main() { vec2 positions[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(positions[gl_VertexIndex],0,1); }
)",
                                 R"(#version 450
layout(set=1,binding=0,std140) uniform Uniform { vec4 Color; } b;
layout(location=0) out vec4 color; void main() { color=b.Color; })");
        auto buffer = GPU->CreateBuffer(
            {16, BufferUsage::Storage | BufferUsage::Uniform | BufferUsage::CopySource, MemoryUsage::DeviceLocal});
        auto           storageLayout = GPU->CreateBindGroupLayout({1, {{2, BindingType::StorageBuffer}}});
        auto           uniformLayout = GPU->CreateBindGroupLayout({1, {{0, BindingType::UniformBuffer}}});
        BindGroupEntry entry;
        entry.Slot           = 2;
        entry.BufferResource = buffer;
        auto storage         = GPU->CreateBindGroup({storageLayout, {entry}});
        entry.Slot           = 0;
        auto uniform         = GPU->CreateBindGroup({uniformLayout, {entry}});
        auto d               = Description();
        d.Program            = writer;
        d.PushConstantBytes  = 0;
        d.GroupLayouts       = {storageLayout};
        auto writePipeline   = GPU->CreatePipeline(d);
        d.Program            = reader;
        d.GroupLayouts       = {uniformLayout};
        auto readPipeline    = GPU->CreatePipeline(d);
        auto target          = Target();
        auto commands        = Begin(Render(target));
        commands->BindPipeline(writePipeline);
        commands->BindGroup(storage);
        commands->Draw(3);
        commands->BindPipeline(readPipeline);
        commands->BindGroup(uniform);
        commands->Draw(3);
        commands->EndRendering();
        auto colors = Readback(Size * Size * 4), values = Readback(16);
        commands->CopyTextureToBuffer(target, {}, colors);
        commands->CopyBuffer(buffer, 0, values, 0, 16);
        GPU->Submit(commands);
        GPU->WaitIdle();
        Solid(colors->Read(0, Size * Size * 4), {0, 255, 0, 255});
        const std::array<float, 4> expected {0, 1, 0, 1};
        auto                       raw = values->Read(0, 16);
        std::array<float, 4>       actual;
        std::memcpy(actual.data(), raw.data(), 16);
        EXPECT_EQ(actual, expected);
    }

    TEST_P(RHIConformance, PushConstantsRepackScalarArraysAndMixedTypes)
    {
        Program             = ShaderFrom(R"(#version 450
void main() { vec2 pos[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(pos[gl_VertexIndex],0,1); })",
                                         R"(#version 450
layout(push_constant) uniform Object { float channels[3]; uint alpha; } p;
layout(location=0) out vec4 color;
void main() { color=vec4(p.channels[0],p.channels[1],p.channels[2],float(p.alpha)); })");
        auto d              = Description();
        d.PushConstantBytes = 16;
        auto target         = Target();
        auto commands       = Begin(Render(target));
        commands->BindPipeline(GPU->CreatePipeline(d));
        const float    channels[] = {.25f, .5f, 1};
        const uint32_t alpha      = 1;
        // Source array stride is 4 bytes, but the emulation's uniform array stride
        // is 16 bytes. Mixed scalar types also catch naive vec4 flattening.
        commands->PushConstants(0, channels, 12);
        commands->PushConstants(12, &alpha, 4);
        commands->Draw(3);
        Solid(Finish(commands, target), {64, 128, 255, 255});
    }

    TEST_P(RHIConformance, PushConstantsRepackNestedStructArraysAndMatrices)
    {
        Program = ShaderFrom(R"(#version 450
void main() { vec2 pos[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(pos[gl_VertexIndex],0,1); })",
                             R"(#version 450
struct Item { float channels[2]; uint alpha; };
layout(push_constant) uniform Object { Item items[2]; mat2 factors; } p;
layout(location=0) out vec4 color;
void main() { color=vec4(p.items[1].channels[0]*p.factors[0][0],p.items[0].channels[1]*p.factors[1][1],p.items[1].channels[1],float(p.items[1].alpha)); })");
        struct Item
        {
            float    Channels[2];
            uint32_t Alpha;
        };
        struct Data
        {
            Item  Items[2];
            float Factors[4];
        };
        static_assert(sizeof(Data) == 40, "Source push block is tightly packed");
        const Data data     = {{{{.125f, .5f}, 0}, {{.25f, 1}, 1}}, {1, 0, 0, 1}};
        auto       d        = Description();
        d.PushConstantBytes = sizeof(Data);
        auto target         = Target();
        auto commands       = Begin(Render(target));
        commands->BindPipeline(GPU->CreatePipeline(d));
        commands->PushConstants(0, &data, sizeof(data));
        commands->Draw(3);
        // Struct array stride 12 and matrix column stride 8 both need repacking.
        // Distinct member values expose copying the first element or wrong offsets.
        Solid(Finish(commands, target), {64, 128, 255, 255});
    }

    TEST_P(RHIConformance, PipelineMasksSurviveRenderingScopeClears)
    {
        auto        target = Target();
        TextureDesc td;
        td.Width = td.Height      = Size;
        td.PixelFormat            = Format::Depth32Float;
        td.Usage                  = TextureUsage::DepthStencil | TextureUsage::CopySource;
        auto depth                = GPU->CreateTexture(td);
        auto d                    = Description();
        d.HasDepthStencil         = true;
        d.DepthStencil.DepthTest  = true;
        d.DepthStencil.DepthWrite = false;
        d.Blends[0].WriteMask     = 1;
        auto commands             = Begin(Render(target, {0, 0, 0, 1}, depth));
        Draw(commands, GPU->CreatePipeline(d));
        commands->EndRendering();
        commands->BeginRendering(Render(target, {0, 0, 1, 1}, depth));
        commands->SetViewport({0, 0, 16, 16, 0, 1});
        const Constants green = NativeConstants({{0, 1, 0, 1}, {-.5f, 0, 0, 0}});
        commands->PushConstants(0, &green, sizeof(green));
        commands->Draw(3);
        // Pipeline bindings persist across rendering scopes. Clears must not
        // silently replace that pipeline's color mask or disabled depth writes.
        commands->EndRendering();
        auto colors = Readback(Size * Size * 4), depths = Readback(Size * Size * 4);
        commands->CopyTextureToBuffer(target, {}, colors);
        commands->CopyTextureToBuffer(depth, {}, depths);
        GPU->Submit(commands);
        GPU->WaitIdle();
        Solid(colors->Read(0, Size * Size * 4), {0, 0, 255, 255});
        auto raw = depths->Read(0, Size * Size * 4);
        for (size_t i = 0; i < raw.size(); i += 4)
        {
            float value;
            std::memcpy(&value, raw.data() + i, 4);
            EXPECT_FLOAT_EQ(value, 1);
        }
    }

    TEST_P(RHIConformance, OlderRetirementDoesNotExposePendingReadbackWrites)
    {
        const std::array<uint8_t, 4> first {1, 2, 3, 4}, second {9, 8, 7, 6};
        auto                         a = Upload(first.data(), 4), b = Upload(second.data(), 4), output = Readback(4);
        auto                         older = GPU->CreateCommandList();
        older->CopyBuffer(a, 0, output, 0, 4);
        GPU->Submit(older);
        auto newer = GPU->CreateCommandList();
        newer->CopyBuffer(b, 0, output, 0, 4);
        GPU->WaitIdle();
        // The older fence cannot authorize reading a buffer whose newer copy is
        // still pending. A single Ready flag would expose stale/early results.
        EXPECT_THROW(output->Read(0, 4), std::invalid_argument);
        GPU->Submit(newer);
        GPU->WaitIdle();
        EXPECT_EQ(output->Read(0, 4), std::vector<uint8_t>(second.begin(), second.end()));
        auto canceled = GPU->CreateCommandList();
        canceled->CopyBuffer(a, 0, output, 0, 4);
        EXPECT_THROW(output->Read(0, 4), std::invalid_argument);
        canceled.reset();
        EXPECT_EQ(output->Read(0, 4), std::vector<uint8_t>(second.begin(), second.end()));
    }

    TEST_P(RHIConformance, PushConstantsRepackRowAndColumnMajorMatrixArrays)
    {
        Program = ShaderFrom(R"(#version 450
void main() { vec2 pos[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(pos[gl_VertexIndex],0,1); })",
                             R"(#version 450
layout(push_constant) uniform Object { layout(column_major) mat2 a[2]; layout(row_major) mat2 b[2]; } p;
layout(location=0) out vec4 color;
void main() { color=vec4(p.a[1][0][1],p.b[1][1][0],p.b[1][1][1],1); })");
        std::array<float, 16> data {};
        data[5]             = .25f;
        data[6]             = .75f;
        data[13]            = .5f;
        data[14]            = .75f;
        data[15]            = 1;
        auto d              = Description();
        d.PushConstantBytes = sizeof(data);
        auto target         = Target();
        auto commands       = Begin(Render(target));
        commands->BindPipeline(GPU->CreatePipeline(d));
        commands->PushConstants(0, data.data(), sizeof(data));
        commands->Draw(3);
        // Arrays require wider uniform strides. Off-diagonal values independently
        // expose losing row/column-major matrix order during code generation.
        Solid(Finish(commands, target), {64, 128, 255, 255});
    }

    INSTANTIATE_TEST_SUITE_P(Backends,
                             RHIConformance,
                             testing::Values(GpuTestBackend::OpenGL),
                             [](const testing::TestParamInfo<GpuTestBackend>&) { return "OpenGL"; });
} // namespace
