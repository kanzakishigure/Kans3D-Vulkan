#include <array>
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <spirv_cross/spirv.hpp>
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/RHI/RHI.h"
#include "Kans3D/Renderer/Resource/InternalShader.h"
#include "Kans3D/Renderer/Resource/SelectionTools.h"
#include "GpuTestContext.h"
#include "MaterialShaderTestUtilities.h"

namespace
{
    using namespace Kans;
    using namespace Kans::RenderHardware;
    using Buffer                = Kans::RenderHardware::Buffer;
    using Texture               = Kans::RenderHardware::Texture;
    using Shader                = Kans::RenderHardware::Shader;
    using TextureUsage          = Kans::RenderHardware::TextureUsage;
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

    TEST_P(RHIConformance, HeadlessContextInstallsTheChosenSharedDeviceAndRejectsReplacement)
    {
        // Bug: renderer consumers silently keep/use a GL shared device even
        // though the fixture requested Vulkan, or a live shared device can be replaced.
        const auto expected = GetParam() == GpuTestBackend::OpenGL ? Backend::OpenGL : Backend::Vulkan;
        EXPECT_EQ(GetBackend(), expected);
        const auto shared = GetSharedDevice();
        ASSERT_TRUE(shared);
        const auto actualCoordinates   = shared->GetCoordinateConventions();
        const auto expectedCoordinates = GPU->GetCoordinateConventions();
        EXPECT_EQ(actualCoordinates.ClipY, expectedCoordinates.ClipY);
        EXPECT_EQ(actualCoordinates.Depth, expectedCoordinates.Depth);
        EXPECT_EQ(actualCoordinates.Origin, expectedCoordinates.Origin);
        EXPECT_THROW(InitializeHeadlessDevice(expected, shared), std::logic_error);
        EXPECT_THROW(InitializeHeadlessDevice(expected, {}), std::invalid_argument);
        EXPECT_EQ(GetSharedDevice(), shared);
    }

    TEST_P(RHIConformance, InternalShaderCreationKeepsTheCompilerDiagnostic)
    {
        // Catches CreateInternalShader replacing the compiler message with a generic
        // error, which would leave a broken editor shader undiagnosable in the log.
        const auto result = CreateInternalShader(*GPU,
                                                 "DiagnosticTest",
                                                 "#version 450\nvoid main(){gl_Position=vec4(0);}",
                                                 "#version 450\nlayout(location=0) out vec4 c;void main(){c=undefined_symbol;}");
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        const auto& message = std::get<AssetError>(result).Message;
        EXPECT_NE(message.find("undefined_symbol"), std::string::npos) << message;
        EXPECT_NE(message.find("DiagnosticTest"), std::string::npos) << message;
    }

    TEST_P(RHIConformance, StandaloneSubmissionRetainsReadbacksAndDoesNotStartAFrame)
    {
        // Catches a backend requiring BeginFrame for a picker/thumbnail, implicitly
        // retiring standalone lists, or leaving an implicit frame active.
        GPU->EndFrame();
        const std::array<uint32_t, 2> expected {7, 0xfedcba98u};
        auto                          upload   = Upload(expected.data(), sizeof(expected));
        auto                          result   = Readback(sizeof(expected));
        auto                          commands = GPU->CreateCommandList();
        commands->CopyBuffer(upload, 0, result, 0, sizeof(expected));
        GPU->Submit(commands);
        commands.reset();
        upload.reset();
        EXPECT_THROW(result->Read(0, sizeof(expected)), std::invalid_argument);
        GPU->WaitIdle();
        const auto              bytes = result->Read(0, sizeof(expected));
        std::array<uint32_t, 2> actual;
        std::memcpy(actual.data(), bytes.data(), sizeof(actual));
        EXPECT_EQ(actual, expected);
        GPU->BeginFrame(); // Fixture teardown ends this frame.
    }

    TEST_P(RHIConformance, PickerUsesBottomLeftRowsAndClearsResizedTargets)
    {
        auto     device = std::shared_ptr<Device>(GPU.get(), [](Device*) {});
        MeshData quad;
        for (auto p :
             {glm::vec3(-1, -1, 0.25f), glm::vec3(1, -1, 0.25f), glm::vec3(1, 0, 0.25f), glm::vec3(-1, 0, 0.25f)})
        {
            MeshVertexData v;
            v.Position = {p.x, p.y, p.z};
            quad.Vertices.push_back(v);
        }
        quad.Indices  = {0, 1, 2, 2, 3, 0};
        BuildSingleSection(quad);
        auto uploaded = UploadMesh(quad, device);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded));
        const auto&                 mesh = std::get<GpuMesh>(uploaded);
        EntityPicker                picker(device);
        const std::vector<MeshDraw> draws {{&mesh, glm::mat4(1), 0xfedcba98u}};
        // Asymmetric rows expose a Y inversion even with an identity camera.
        auto lower = picker.PickSurface(draws, glm::mat4(1), {20, 12}, {10, 2});
        ASSERT_EQ(lower.ID, 0xfedcba98u);
        ASSERT_TRUE(lower.Position);
        EXPECT_NEAR(lower.Position->x, 0.05f, 1e-6f);
        EXPECT_NEAR(lower.Position->y, -7.0f / 12.0f, 1e-6f);
        EXPECT_NEAR(lower.Position->z, 0.25f, 1e-6f);
        EXPECT_EQ(picker.Pick(draws, glm::mat4(1), {20, 12}, {10, 9}), 0u);
        // Reallocation must not return a stale ID/depth from the former target.
        EXPECT_EQ(picker.Pick(draws, glm::mat4(1), {10, 6}, {5, 4}), 0u);
        const auto resized = picker.PickSurface(draws, glm::mat4(1), {10, 6}, {5, 1});
        ASSERT_EQ(resized.ID, 0xfedcba98u);
        ASSERT_TRUE(resized.Position);
        EXPECT_NEAR(resized.Position->x, 0.1f, 1e-6f);
        EXPECT_NEAR(resized.Position->y, -0.5f, 1e-6f);
        EXPECT_NEAR(resized.Position->z, 0.25f, 1e-6f);
        EXPECT_EQ(picker.Pick(draws, glm::mat4(1), {10, 6}, {10, 1}), 0u);
        // WaitIdle inside picking must leave the caller's active frame active.
    }

    TEST_P(RHIConformance, OutlinePreservesSceneAlphaAndDoesNotKeepOldSelectionOnResize)
    {
        auto     device = std::shared_ptr<Device>(GPU.get(), [](Device*) {});
        MeshData quad;
        for (auto p : {glm::vec3(-0.75f, -0.75f, 0),
                       glm::vec3(-0.25f, -0.75f, 0),
                       glm::vec3(-0.25f, 0.75f, 0),
                       glm::vec3(-0.75f, 0.75f, 0)})
        {
            MeshVertexData v;
            v.Position = {p.x, p.y, p.z};
            quad.Vertices.push_back(v);
        }
        quad.Indices  = {0, 1, 2, 2, 3, 0};
        BuildSingleSection(quad);
        auto uploaded = UploadMesh(quad, device);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded));
        const auto&      mesh = std::get<GpuMesh>(uploaded);
        SelectionOutline outline(device);
        auto             target    = Target();
        auto             auxiliary = Target();
        RenderingDesc    scope;
        scope.Colors               = {{target}, {auxiliary}};
        scope.Colors[0].ClearColor = {0, 0, 0, 0.4f};
        scope.Colors[1].ClearColor = {0, 1, 0, 0.6f};
        auto clear                 = GPU->CreateCommandList();
        clear->BeginRendering(scope);
        clear->EndRendering();
        GPU->Submit(clear);
        outline.Draw(scope, {{&mesh, glm::mat4(1), 1}}, glm::mat4(1), {Size, Size}, {1, 0, 0, 1}, 1);
        auto read   = GPU->CreateCommandList();
        auto output = Readback(Size * Size * 4);
        read->CopyTextureToBuffer(target, {}, output);
        GPU->Submit(read);
        GPU->WaitIdle();
        auto bytes = output->Read(0, Size * Size * 4);
        EXPECT_EQ(At(bytes, 6, ClipRow(8)), (Pixel {255, 0, 0, 102})); // Border.
        EXPECT_EQ(At(bytes, 4, ClipRow(8)), (Pixel {31, 0, 0, 102}));  // 12% fill.
        EXPECT_EQ(At(bytes, 12, ClipRow(8)), (Pixel {0, 0, 0, 102}));  // Unselected right side.
        // A colour-0 composite must not clear/write unrelated scene attachments.
        read = GPU->CreateCommandList();
        read->CopyTextureToBuffer(auxiliary, {}, output);
        GPU->Submit(read);
        GPU->WaitIdle();
        Solid(output->Read(0, Size * Size * 4), Pixel {0, 255, 0, 153});
        // Catches a stale mask on resize/selection replacement: move selection right.
        scope.Colors.resize(1);
        target                 = Target(Format::RGBA8Unorm, 32, 16);
        scope.Colors[0].Target = target;
        clear                  = GPU->CreateCommandList();
        clear->BeginRendering(scope);
        clear->EndRendering();
        GPU->Submit(clear);
        glm::mat4 moved(1);
        moved[3][0] = 1;
        outline.Draw(scope, {{&mesh, moved, 2}}, glm::mat4(1), {32, 16}, {1, 0, 0, 1}, 1);
        read   = GPU->CreateCommandList();
        output = Readback(32 * 16 * 4);
        read->CopyTextureToBuffer(target, {}, output);
        GPU->Submit(read);
        GPU->WaitIdle();
        bytes = output->Read(0, 32 * 16 * 4);
        EXPECT_EQ(bytes[(8 * 32 + 8) * 4], 0); // Old selection must be gone.
        EXPECT_EQ(bytes[(8 * 32 + 24) * 4], 31);
    }

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

    TEST_P(RHIConformance, FullFloatTargetPreservesValuesBelowHalfPrecision)
    {
        // A mistaken RGBA16F allocation/transfer rounds these binary-exact values.
        const std::array<float, 4> expected {1.0001220703125f, -2.000244140625f, 0.50006103515625f, 1};
        auto                       target   = Target(Format::RGBA32Float);
        auto                       commands = Begin(Render(target));
        Draw(commands, GPU->CreatePipeline(Description(Format::RGBA32Float)), {expected, {}});
        commands->EndRendering();
        auto output = Readback(Size * Size * sizeof(expected));
        commands->CopyTextureToBuffer(target, {}, output);
        GPU->Submit(commands);
        GPU->WaitIdle();
        auto bytes = output->Read(0, Size * Size * sizeof(expected));
        for (size_t i = 0; i < Size * Size; ++i)
        {
            std::array<float, 4> actual;
            std::memcpy(actual.data(), bytes.data() + i * sizeof(actual), sizeof(actual));
            EXPECT_EQ(actual, expected) << i;
        }
    }

    TEST_P(RHIConformance, OddWidthTwoChannelMipsAndSampledComponents)
    {
        // Detects RG mapped as R/RGBA, padded odd-width rows, wrong mip extent,
        // and a matching upload/readback mistake by independently sampling mip zero.
        TextureDesc desc;
        desc.Width       = 3;
        desc.Height      = 5;
        desc.MipLevels   = 3;
        desc.PixelFormat = Format::RG8Unorm;
        desc.Usage       = TextureUsage::Sampled | TextureUsage::CopySource | TextureUsage::CopyDestination;
        auto                              texture = GPU->CreateTexture(desc);
        std::vector<std::vector<uint8_t>> patterns;
        std::vector<Handle<Buffer>>       outputs;
        auto                              copy = GPU->CreateCommandList();
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            size_t count = std::max(1u, 3u >> mip) * std::max(1u, 5u >> mip) * 2;
            patterns.emplace_back(count);
            for (size_t i = 0; i < count; ++i)
                patterns.back()[i] = uint8_t(37 * i + 59 * mip + 11);
            if (mip == 0)
            {
                patterns.back()[0] = 85;
                patterns.back()[1] = 170;
            }
            auto upload = Upload(patterns.back().data(), count);
            copy->CopyBufferToTexture(upload, 0, texture, {mip, 0});
            outputs.push_back(Readback(count));
        }
        for (uint32_t mip = 0; mip < 3; ++mip)
            copy->CopyTextureToBuffer(texture, {mip, 0}, outputs[mip]);
        GPU->Submit(copy);
        GPU->WaitIdle();
        for (uint32_t mip = 0; mip < 3; ++mip)
            EXPECT_EQ(outputs[mip]->Read(0, patterns[mip].size()), patterns[mip]);
        Program               = ShaderFrom(Vertex, R"(#version 450
layout(set=1,binding=9) uniform sampler2D image;
layout(location=0) out vec4 color;
void main() { color=texelFetch(image,ivec2(0),0); })");
        auto           layout = GPU->CreateBindGroupLayout({1, {{9, BindingType::CombinedTextureSampler}}});
        BindGroupEntry entry;
        entry.Slot            = 9;
        entry.TextureResource = texture;
        entry.SamplerResource = GPU->CreateSampler();
        auto group            = GPU->CreateBindGroup({layout, {entry}});
        auto d                = Description();
        d.GroupLayouts        = {layout};
        auto target           = Target();
        auto commands         = Begin(Render(target));
        commands->BindGroup(group);
        Draw(commands, GPU->CreatePipeline(d));
        Solid(Finish(commands, target), {85, 170, 0, 255});
    }

    TEST_P(RHIConformance, SparsePublishedSlotsAndRecordedPerDrawUniformRanges)
    {
        // Slots above 7 must load unchanged. Colliding logical slots in both
        // groups must stay independent; each recorded draw must retain its range.
        ShaderData data;
        data.Code = CompileTestMaterialShader(Vertex, R"(#version 450
layout(set=0,binding=17,std140) uniform Frame { vec4 Value; } frame;
layout(set=1,binding=17,std140) uniform Material { vec4 Value; } material;
layout(set=0,binding=23) uniform sampler2D first;
layout(set=1,binding=23) uniform sampler2D second;
layout(location=0) out vec4 color;
void main() { color=frame.Value*texture(first,vec2(.5))+material.Value*texture(second,vec2(.5)); })");
        // shaderc imposes a source-language binding ceiling. SPIR-V uses uint32
        // binding decorations; patch only those numbers to exercise the full RHI address.
        for (auto& module : data.Code.Modules)
            for (size_t i = 5; i < module.Words.size();)
            {
                const uint32_t count = module.Words[i] >> 16;
                ASSERT_NE(count, 0u);
                if ((module.Words[i] & 0xffffu) == spv::OpDecorate && count == 4 &&
                    module.Words[i + 2] == spv::DecorationBinding)
                {
                    auto& slot = module.Words[i + 3];
                    if (slot == 17)
                        slot = 65537;
                    else if (slot == 23)
                        slot = 100023;
                }
                i += count;
            }
        Program = GPU->CreateShader(data);
        std::vector<Handle<BindGroupLayout>> layouts;
        for (uint32_t group = 0; group < 2; ++group)
            layouts.push_back(GPU->CreateBindGroupLayout(
                {group, {{65537, BindingType::UniformBuffer}, {100023, BindingType::CombinedTextureSampler}}}));
        const size_t stride = std::max(size_t(16), GPU->GetBufferBindingAlignment(BindingType::UniformBuffer));
        auto         frames = GPU->CreateBuffer({stride * 2, BufferUsage::Uniform, MemoryUsage::Upload});
        const std::array<float, 4> first {1, 1, 1, 0}, second {.5f, 1, 1, 0}, material {1, 1, .5f, 1};
        frames->Write(0, first.data(), 16);
        frames->Write(stride, second.data(), 16);
        auto                           parameters = Upload(material.data(), 16, BufferUsage::Uniform);
        auto                           sampler    = GPU->CreateSampler();
        auto                           commands   = GPU->CreateCommandList();
        std::array<Handle<Texture>, 2> textures;
        const std::array<Pixel, 2>     pixels {{{255, 0, 0, 255}, {0, 0, 255, 255}}};
        for (size_t i = 0; i < 2; ++i)
        {
            TextureDesc td;
            td.Usage    = TextureUsage::Sampled | TextureUsage::CopyDestination;
            textures[i] = GPU->CreateTexture(td);
            commands->CopyBufferToTexture(Upload(pixels[i].data(), 4), 0, textures[i]);
        }
        auto group = [&](uint32_t index, size_t offset) {
            BindGroupEntry buffer;
            buffer.Slot           = 65537;
            buffer.BufferResource = index ? parameters : frames;
            buffer.Offset         = offset;
            buffer.Bytes          = 16;
            BindGroupEntry image;
            image.Slot            = 100023;
            image.TextureResource = textures[index];
            image.SamplerResource = sampler;
            return GPU->CreateBindGroup({layouts[index], {buffer, image}});
        };
        auto d         = Description();
        d.GroupLayouts = layouts;
        auto pipeline  = GPU->CreatePipeline(d);
        auto target    = Target();
        commands->BeginRendering(Render(target));
        commands->SetViewport({0, 0, float(Size), float(Size), 0, 1});
        commands->BindGroup(group(1, 0));
        commands->BindGroup(group(0, 0));
        commands->SetScissor({0, 0, 8, Size});
        Draw(commands, pipeline);
        commands->BindGroup(group(0, stride));
        commands->SetScissor({8, 0, 8, Size});
        Draw(commands, pipeline);
        // Release all caller references to demonstrate command ownership.
        frames.reset();
        parameters.reset();
        textures   = {};
        auto bytes = Finish(commands, target);
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
                EXPECT_EQ(At(bytes, x, y), (x < 8 ? Pixel {255, 0, 128, 255} : Pixel {128, 0, 128, 255}))
                    << x << ',' << y;
    }

    TEST_P(RHIConformance, InstancedTriangleStripUsesBindingDivisorAndFirstInstance)
    {
        // Bugs: PerInstance mapped to per-vertex input, firstInstance discarded,
        // or TriangleStrip mapped to a triangle list (leaving half of each tile clear).
        Program                 = ShaderFrom(R"(#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 offset;
layout(location=2) in vec4 tint;
layout(location=0) out vec4 vTint;
void main() { gl_Position=vec4(position*vec2(.5,1)+offset,0,1); vTint=tint; })",
                                             R"(#version 450
layout(location=0) in vec4 vTint;
layout(location=0) out vec4 color;
void main() { color=vTint; })");
        const float positions[] = {-1, -1, 1, -1, -1, 1, 1, 1};
        const float instances[] = {0, 0, 1, 0, 1, 1, -.5f, 0, 1, 0, 0, 1, .5f, 0, 0, 1, 0, 1};
        auto        d           = Description();
        d.PushConstantBytes     = 0;
        d.Primitive             = Topology::TriangleStrip;
        d.VertexBindings        = {{0, 8, false}, {4, 24, true}};
        d.Attributes            = {
            {0, 0, 0, VertexFormat::Float2}, {1, 4, 0, VertexFormat::Float2}, {2, 4, 8, VertexFormat::Float4}};
        auto target   = Target();
        auto commands = Begin(Render(target));
        commands->BindPipeline(GPU->CreatePipeline(d));
        commands->BindVertexBuffers({{0, Upload(positions, sizeof(positions), BufferUsage::Vertex), 0},
                                     {4, Upload(instances, sizeof(instances), BufferUsage::Vertex), 0}});
        commands->Draw(4, 2, 0, 1);
        auto raw = Finish(commands, target);
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
                ASSERT_EQ(At(raw, x, y), (x < 8 ? Pixel {255, 0, 0, 255} : Pixel {0, 255, 0, 255}));
    }

    TEST_P(RHIConformance, PointTopologyAndFirstVertexSelectOnlyTheRequestedPoint)
    {
        // Bugs: PointList mapped to a line/triangle topology, or firstVertex ignored.
        Program       = ShaderFrom(R"(#version 450
void main() { vec2 p[2]=vec2[](vec2(-.9375,-.9375),vec2(.0625,.0625));
    gl_Position=vec4(p[gl_VertexIndex],0,1); gl_PointSize=1; })",
                                   Fragment);
        auto d        = Description();
        d.Primitive   = Topology::PointList;
        auto target   = Target();
        auto commands = Begin(Render(target));
        commands->BindPipeline(GPU->CreatePipeline(d));
        Constants constants;
        commands->PushConstants(0, &constants, sizeof(constants));
        commands->Draw(1, 1, 1, 0);
        auto raw = Finish(commands, target);
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
                ASSERT_EQ(At(raw, x, y), (x == 8 && y == ClipRow(8) ? Pixel {255, 0, 0, 255} : Pixel {0, 0, 0, 255}));
    }

    TEST_P(RHIConformance, RenderedTextureSamplingAndReplacementKeepRecordedDescriptors)
    {
        // Bugs: missing attachment -> sampled layout dependency; writing descriptors
        // to the wrong sparse binding; replacing a texture mutates an older recorded set.
        TextureDesc sampled;
        sampled.Width = sampled.Height = Size;
        sampled.Usage                  = TextureUsage::RenderTarget | TextureUsage::Sampled;
        auto oldImage = GPU->CreateTexture(sampled), newImage = GPU->CreateTexture(sampled);
        auto producer = GPU->CreateCommandList();
        producer->BeginRendering(Render(oldImage, {1, 0, 0, 1}));
        producer->EndRendering();
        producer->BeginRendering(Render(newImage, {0, 1, 0, 1}));
        producer->EndRendering();
        GPU->Submit(producer);
        Program      = ShaderFrom(Vertex, R"(#version 450
layout(set=0,binding=19) uniform sampler2D image;
layout(location=0) out vec4 color;
void main() { color=texture(image,vec2(.5)); })");
        auto layout  = GPU->CreateBindGroupLayout({0, {{19, BindingType::CombinedTextureSampler}}});
        auto sampler = GPU->CreateSampler();
        auto group   = [&](const Handle<Texture>& texture) {
            BindGroupEntry entry;
            entry.Slot            = 19;
            entry.TextureResource = texture;
            entry.SamplerResource = sampler;
            return GPU->CreateBindGroup({layout, {entry}});
        };
        auto d           = Description();
        d.GroupLayouts   = {layout};
        auto pipeline    = GPU->CreatePipeline(d);
        auto target      = Target();
        auto commands    = Begin(Render(target));
        auto replacement = group(oldImage);
        commands->BindGroup(replacement);
        commands->SetScissor({0, 0, 8, 16});
        Draw(commands, pipeline);
        replacement = group(newImage);
        oldImage.reset();
        commands->BindGroup(replacement);
        commands->SetScissor({8, 0, 8, 16});
        Draw(commands, pipeline);
        newImage.reset();
        replacement.reset();
        auto raw = Finish(commands, target);
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
                ASSERT_EQ(At(raw, x, y), (x < 8 ? Pixel {255, 0, 0, 255} : Pixel {0, 255, 0, 255}));
    }

    TEST_P(RHIConformance, SampledReadOnlyDepthAttachmentKeepsDepthAndPassesDepthTest)
    {
        // Bugs: depth descriptor uses a color view/layout; missing render -> sample
        // transition; attached sampled depth is transitioned back to a writable layout.
        TextureDesc depthDesc;
        depthDesc.Width = depthDesc.Height = Size;
        depthDesc.PixelFormat              = Format::Depth32Float;
        depthDesc.Usage     = TextureUsage::DepthStencil | TextureUsage::Sampled | TextureUsage::CopySource;
        auto          depth = GPU->CreateTexture(depthDesc);
        auto          clear = GPU->CreateCommandList();
        RenderingDesc clearScope;
        clearScope.Depth.Target     = depth;
        clearScope.Depth.ClearDepth = .5f;
        clear->BeginRendering(clearScope);
        clear->EndRendering();
        GPU->Submit(clear);
        Program               = ShaderFrom(Vertex, R"(#version 450
layout(set=0,binding=7) uniform sampler2D depthImage;
layout(location=0) out vec4 color;
layout(push_constant) uniform Object { vec4 Color; vec4 Params; } p;
void main() { color=vec4(texture(depthImage,vec2(.5)).r,1,1,1)*p.Color; })");
        auto           layout = GPU->CreateBindGroupLayout({0, {{7, BindingType::CombinedTextureSampler}}});
        BindGroupEntry entry;
        entry.Slot                = 7;
        entry.TextureResource     = depth;
        entry.SamplerResource     = GPU->CreateSampler();
        auto d                    = Description();
        d.GroupLayouts            = {layout};
        d.HasDepthStencil         = true;
        d.DepthStencil.DepthTest  = true;
        d.DepthStencil.DepthWrite = false;
        auto pipeline             = GPU->CreatePipeline(d);
        auto target               = Target();
        auto scope                = Render(target, {0, 0, 0, 1}, depth);
        scope.Depth.Load          = LoadOp::Load;
        auto commands             = Begin(scope);
        commands->BindGroup(GPU->CreateBindGroup({layout, {entry}}));
        Draw(commands, pipeline, {{1, 1, 0, 1}, {-0.5f, 0, 0, 0}});
        // A draw beyond the stored depth must fail, even though sampling is enabled.
        Draw(commands, pipeline, {{0, 0, 1, 1}, {0.5f, 0, 0, 0}});
        commands->EndRendering();
        auto colors = Readback(Size * Size * 4), depths = Readback(Size * Size * 4);
        commands->CopyTextureToBuffer(target, {}, colors);
        commands->CopyTextureToBuffer(depth, {}, depths);
        GPU->Submit(commands);
        GPU->WaitIdle();
        Solid(colors->Read(0, Size * Size * 4), {128, 255, 0, 255});
        auto raw = depths->Read(0, Size * Size * 4);
        for (size_t i = 0; i < raw.size(); i += 4)
        {
            float value;
            std::memcpy(&value, raw.data() + i, 4);
            ASSERT_EQ(value, .5f);
        }
    }

    TEST_P(RHIConformance, DepthWritesAndAttachedSamplingWithinOneScopeAreOrdered)
    {
        // Bug: treating the whole RHI scope as writable when a later draw samples
        // attached depth, or emitting a layout barrier inside dynamic rendering.
        TextureDesc desc;
        desc.Width = desc.Height = Size;
        desc.PixelFormat         = Format::Depth32Float;
        desc.Usage               = TextureUsage::DepthStencil | TextureUsage::Sampled | TextureUsage::CopySource;
        auto depth = GPU->CreateTexture(desc), target = Target();
        auto d                       = Description();
        d.HasDepthStencil            = true;
        d.DepthStencil.DepthTest     = true;
        d.DepthStencil.DepthWrite    = true;
        d.DepthStencil.DepthCompare  = CompareOp::Always;
        auto           writer        = GPU->CreatePipeline(d);
        auto           readerProgram = ShaderFrom(Vertex, R"(#version 450
layout(set=0,binding=9) uniform sampler2D depthImage;
layout(location=0) out vec4 color;
void main() { color=vec4(vec3(texelFetch(depthImage,ivec2(gl_FragCoord.xy),0).r),1); })");
        auto           layout        = GPU->CreateBindGroupLayout({0, {{9, BindingType::CombinedTextureSampler}}});
        BindGroupEntry entry;
        entry.Slot                = 9;
        entry.TextureResource     = depth;
        entry.SamplerResource     = GPU->CreateSampler();
        auto group                = GPU->CreateBindGroup({layout, {entry}});
        d.Program                 = readerProgram;
        d.GroupLayouts            = {layout};
        d.DepthStencil.DepthWrite = false;
        auto reader               = GPU->CreatePipeline(d);
        auto commands             = Begin(Render(target, {0, 0, 0, 1}, depth));
        commands->BindGroup(group);
        // First sample also requires separating the native clear from read-only attachment use.
        Draw(commands, reader);
        commands->SetScissor({0, 0, 8, 16});
        Draw(commands, writer, {{1, 0, 0, 1}, {-0.5f, 0, 0, 0}});
        Draw(commands, reader);
        commands->SetScissor({8, 0, 8, 16});
        Draw(commands, writer, {{0, 1, 0, 1}, {0, 0, 0, 0}});
        Draw(commands, reader);
        auto raw = Finish(commands, target);
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
                ASSERT_EQ(At(raw, x, y), (x < 8 ? Pixel {64, 64, 64, 255} : Pixel {128, 128, 128, 255}));
    }

    TEST_P(RHIConformance, CachedPipelinesRenderTheirOwnTargetFormats)
    {
        // Bug: the description hash/cache omits attachment format and returns the
        // RGBA8 pipeline for an HDR target. Actual draws also exercise native compatibility.
        auto ldrDesc = Description(), hdrDesc = Description(Format::RGBA32Float);
        auto ldr = GPU->CreatePipeline(ldrDesc), hdr = GPU->CreatePipeline(hdrDesc);
        ASSERT_NE(ldr, hdr);
        EXPECT_EQ(GPU->CreatePipeline(ldrDesc), ldr);
        EXPECT_EQ(GPU->CreatePipeline(hdrDesc), hdr);
        auto target   = Target();
        auto commands = Begin(Render(target));
        Draw(commands, ldr);
        Solid(Finish(commands, target), {255, 0, 0, 255});
        target   = Target(Format::RGBA32Float);
        commands = Begin(Render(target));
        const std::array<float, 4> expected {2.25f, -.125f, .250123f, 1};
        Draw(commands, hdr, {expected, {}});
        auto raw = Finish(commands, target, Size * Size * 16);
        for (size_t i = 0; i < raw.size(); i += 16)
        {
            std::array<float, 4> value;
            std::memcpy(value.data(), raw.data() + i, 16);
            ASSERT_EQ(value, expected);
        }
    }

    TEST_P(RHIConformance, ClearOnlyFormatsPreserveTypedValuesAndLoad)
    {
        // Wrong native sRGB/UNORM or float format, a missing attachment->copy
        // layout transition, and Load implemented as Clear each fail independently.
        for (auto format : {Format::RGBA8Unorm,
                            Format::RGBA8Srgb,
                            Format::RGBA16Float,
                            Format::RGBA32Float,
                            Format::RG8Unorm,
                            Format::R8Unorm,
                            Format::R32Uint})
        {
            SCOPED_TRACE(int(format));
            auto                       target = Target(format);
            const std::array<float, 4> clearValue =
                format == Format::RGBA32Float ?
                    std::array<float, 4> {1.0001220703125f, -2.000244140625f, 0.50006103515625f, 1} :
                format == Format::RGBA16Float ? std::array<float, 4> {2, -0.5f, 0.25f, 1} :
                                                std::array<float, 4> {0.25f, 0.5f, 1, 1};
            auto clear                = Render(target, clearValue);
            clear.Colors[0].ClearUint = 0xfedcba98u;
            auto commands             = GPU->CreateCommandList();
            commands->BeginRendering(clear);
            // Clear/load must ignore the previous scope's scissor; negative
            // origins also exercise neutral clipping without native validation errors.
            commands->SetScissor({-2, -2, 3, 3});
            commands->EndRendering();
            GPU->Submit(commands);
            auto load                 = clear;
            load.Colors[0].Load       = LoadOp::Load;
            load.Colors[0].ClearColor = {0, 0, 0, 0};
            load.Colors[0].ClearUint  = 0;
            commands                  = GPU->CreateCommandList();
            commands->BeginRendering(load);
            commands->EndRendering();
            size_t pixelBytes = format == Format::RGBA32Float ? 16 :
                                format == Format::RGBA16Float ? 8 :
                                format == Format::RG8Unorm    ? 2 :
                                format == Format::R8Unorm     ? 1 :
                                                                4;
            auto   output     = Readback(Size * Size * pixelBytes);
            commands->CopyTextureToBuffer(target, {}, output);
            GPU->Submit(commands);
            EXPECT_THROW(output->Read(0, pixelBytes), std::invalid_argument);
            GPU->WaitIdle();
            auto raw = output->Read(0, Size * Size * pixelBytes);
            for (size_t i = 0; i < raw.size(); i += pixelBytes)
            {
                if (format == Format::RGBA32Float)
                {
                    std::array<float, 4> value;
                    std::memcpy(value.data(), raw.data() + i, 16);
                    EXPECT_EQ(value, clearValue);
                }
                else if (format == Format::RGBA16Float)
                {
                    std::array<uint16_t, 4> value;
                    std::memcpy(value.data(), raw.data() + i, 8);
                    EXPECT_EQ(value, (std::array<uint16_t, 4> {0x4000, 0xb800, 0x3400, 0x3c00}));
                }
                else if (format == Format::R32Uint)
                {
                    uint32_t value;
                    std::memcpy(&value, raw.data() + i, 4);
                    EXPECT_EQ(value, 0xfedcba98u);
                }
                else if (format == Format::RGBA8Srgb)
                {
                    // Same one-byte conversion precision allowance as the existing
                    // sRGB drawing case; all other components/bytes are exact.
                    EXPECT_NEAR(raw[i], 137, 1);
                    EXPECT_NEAR(raw[i + 1], 188, 1);
                    EXPECT_EQ(raw[i + 2], 255);
                    EXPECT_EQ(raw[i + 3], 255);
                }
                else
                {
                    const std::array<uint8_t, 4> expected {64, 128, 255, 255};
                    for (size_t c = 0; c < pixelBytes; ++c)
                        EXPECT_EQ(raw[i + c], expected[c]);
                }
            }
        }
    }

    TEST_P(RHIConformance, ClearOnlyDepthStencilMipReadback)
    {
        // Wrong mip views, missing depth attachment/copy transitions or packed
        // depth/stencil component ordering changes these exact bytes.
        for (auto format : {Format::Depth32Float, Format::Depth24Stencil8})
        {
            TextureDesc desc;
            desc.Width = desc.Height = 4;
            desc.MipLevels           = 3;
            desc.PixelFormat         = format;
            desc.Usage               = TextureUsage::DepthStencil | TextureUsage::CopySource;
            auto target              = GPU->CreateTexture(desc);
            auto commands            = GPU->CreateCommandList();
            for (uint32_t mip = 0; mip < 3; ++mip)
            {
                RenderingDesc scope;
                scope.Depth.Target          = target;
                scope.Depth.Subresource.Mip = mip;
                // Endpoints have exact representations in D24 as well as D32.
                scope.Depth.ClearDepth   = mip == 1 ? 0 : 1;
                scope.Depth.ClearStencil = 0x21 + mip * 31;
                commands->BeginRendering(scope);
                commands->EndRendering();
            }
            GPU->Submit(commands);
            for (uint32_t mip = 0; mip < 3; ++mip)
            {
                const size_t bytes  = size_t(4 >> mip) * (4 >> mip) * 4;
                auto         output = Readback(bytes);
                commands            = GPU->CreateCommandList();
                commands->CopyTextureToBuffer(target, {mip, 0}, output);
                GPU->Submit(commands);
                GPU->WaitIdle();
                auto raw = output->Read(0, bytes);
                for (size_t i = 0; i < bytes; i += 4)
                {
                    if (format == Format::Depth32Float)
                    {
                        float value;
                        std::memcpy(&value, raw.data() + i, 4);
                        EXPECT_EQ(value, mip == 1 ? 0.0f : 1.0f);
                    }
                    else
                    {
                        uint32_t value;
                        std::memcpy(&value, raw.data() + i, 4);
                        EXPECT_EQ(value, (mip == 1 ? 0u : 0xffffff00u) | (0x21 + mip * 31));
                    }
                }
            }
        }
    }

    TEST_P(RHIConformance, CubeMipAttachmentCopiesFollowSubmissionOrder)
    {
        // Recording-order tracking (instead of submission order) loses layouts;
        // mip-zero/face-zero copies overwrite sentinel subresources. Attachment
        // clears independently address the face/mip that transfers later read.
        TextureDesc desc;
        desc.Width = desc.Height = 4;
        desc.MipLevels           = 3;
        desc.Dimension           = TextureDimension::Cube;
        desc.PixelFormat         = Format::R32Uint;
        desc.Usage = TextureUsage::RenderTarget | TextureUsage::CopySource | TextureUsage::CopyDestination;
        auto                        texture = GPU->CreateTexture(desc);
        std::vector<Handle<Buffer>> outputs;
        auto                        read = GPU->CreateCommandList();
        for (uint32_t face = 0; face < 6; ++face)
            for (uint32_t mip = 0; mip < 3; ++mip)
            {
                auto output = Readback(size_t(4 >> mip) * (4 >> mip) * 4);
                read->CopyTextureToBuffer(texture, {mip, face}, output);
                outputs.push_back(output);
            }
        auto write = GPU->CreateCommandList();
        for (uint32_t face = 0; face < 6; ++face)
            for (uint32_t mip = 0; mip < 3; ++mip)
            {
                const uint32_t        value = 0xf0000000u + face * 17 + mip;
                std::vector<uint32_t> pixels(size_t(4 >> mip) * (4 >> mip), value);
                auto                  upload = Upload(pixels.data(), pixels.size() * 4);
                write->CopyBufferToTexture(upload, 0, texture, {mip, face});
            }
        GPU->Submit(write);
        RenderingDesc scope;
        scope.Colors.push_back({texture, {1, 4}, LoadOp::Clear, StoreOp::Store, {}, 0x87654321u});
        auto clear = GPU->CreateCommandList();
        clear->BeginRendering(scope);
        clear->EndRendering();
        GPU->Submit(clear);
        GPU->Submit(read);
        GPU->WaitIdle();
        for (uint32_t face = 0; face < 6; ++face)
            for (uint32_t mip = 0; mip < 3; ++mip)
            {
                const size_t bytes = size_t(4 >> mip) * (4 >> mip) * 4;
                auto         raw   = outputs[face * 3 + mip]->Read(0, bytes);
                for (size_t i = 0; i < bytes; i += 4)
                {
                    uint32_t value;
                    std::memcpy(&value, raw.data() + i, 4);
                    EXPECT_EQ(value, face == 4 && mip == 1 ? 0x87654321u : 0xf0000000u + face * 17 + mip);
                }
            }
    }

    TEST_P(RHIConformance, PackedDepthStencilCopiesKeepMipBytesThroughDeviceLocalBuffers)
    {
        // Missing aspect transitions, wrong mip addressing and interpreting
        // packed D24S8 as a single native copy corrupt exact depth/stencil bits.
        TextureDesc desc;
        desc.Width = desc.Height                   = 4;
        desc.MipLevels                             = 3;
        desc.PixelFormat                           = Format::Depth24Stencil8;
        desc.Usage                                 = TextureUsage::CopySource | TextureUsage::CopyDestination;
        auto                               texture = GPU->CreateTexture(desc);
        std::vector<std::vector<uint32_t>> patterns;
        std::vector<Handle<Buffer>>        outputs;
        auto                               commands = GPU->CreateCommandList();
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            std::vector<uint32_t> pattern(size_t(4 >> mip) * (4 >> mip));
            for (size_t i = 0; i < pattern.size(); ++i)
                pattern[i] = uint32_t(0x12345600u + mip * 0x310000u + i * 0x10203u);
            auto upload = Upload(pattern.data(), pattern.size() * 4);
            auto local  = GPU->CreateBuffer(
                {pattern.size() * 4, BufferUsage::CopySource | BufferUsage::CopyDestination, MemoryUsage::DeviceLocal});
            commands->CopyBuffer(upload, 0, local, 0, pattern.size() * 4);
            commands->CopyBufferToTexture(local, 0, texture, {mip, 0});
            outputs.push_back(Readback(pattern.size() * 4));
            patterns.push_back(pattern);
        }
        GPU->Submit(commands);
        commands = GPU->CreateCommandList();
        for (uint32_t mip = 0; mip < 3; ++mip)
            commands->CopyTextureToBuffer(texture, {mip, 0}, outputs[mip]);
        GPU->Submit(commands);
        GPU->WaitIdle();
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            auto                  raw = outputs[mip]->Read(0, patterns[mip].size() * 4);
            std::vector<uint32_t> actual(patterns[mip].size());
            std::memcpy(actual.data(), raw.data(), raw.size());
            EXPECT_EQ(actual, patterns[mip]);
        }
    }

    TEST_P(RHIConformance, RepeatedFrameSlotsAndStandaloneWorkRetireReleasedResources)
    {
        // Resetting a pool/fence while commands still use it, omitting a fence
        // reset, or releasing submitted memory early fails bytes or validation.
        for (uint32_t count : {1u, 2u, 3u})
        {
            auto device = Context->CreateDevice({count});
            EXPECT_EQ(device->GetDesc().FramesInFlight, count);
            for (uint32_t frame = 0; frame < 18; ++frame)
            {
                const std::array<uint32_t, 4> pattern {frame, frame * 37, 0xfedcba98u, count};
                auto upload = device->CreateBuffer({sizeof(pattern), BufferUsage::CopySource, MemoryUsage::Upload});
                upload->Write(0, pattern.data(), sizeof(pattern));
                auto output =
                    device->CreateBuffer({sizeof(pattern), BufferUsage::CopyDestination, MemoryUsage::Readback});
                auto commands = device->CreateCommandList();
                commands->CopyBuffer(upload, 0, output, 0, sizeof(pattern));
                std::weak_ptr<Buffer> lifetime = upload;
                if (frame % 2 == 0)
                    device->BeginFrame();
                device->Submit(commands);
                commands.reset();
                upload.reset();
                EXPECT_FALSE(lifetime.expired());
                EXPECT_THROW(output->Read(0, sizeof(pattern)), std::invalid_argument);
                if (frame % 2 == 0)
                    device->EndFrame();
                else
                    device->WaitIdle();
                EXPECT_TRUE(lifetime.expired());
                auto                    raw = output->Read(0, sizeof(pattern));
                std::array<uint32_t, 4> actual;
                std::memcpy(actual.data(), raw.data(), sizeof(actual));
                EXPECT_EQ(actual, pattern);
                output.reset();
                device->WaitIdle();
                EXPECT_EQ(device->GetStats().LiveResources, 0u);
                EXPECT_EQ(device->GetStats().PendingDestructions, 0u);
            }
        }
    }

    TEST_P(RHIConformance, OddWidthTwoChannelMipTransfers)
    {
        // Wrong mip extents/offsets or an R/RGBA mapping loses these independent
        // two-channel bytes. The offset is texel-aligned but not four-byte-aligned.
        TextureDesc desc;
        desc.Width                                 = 3;
        desc.Height                                = 5;
        desc.MipLevels                             = 3;
        desc.PixelFormat                           = Format::RG8Unorm;
        desc.Usage                                 = TextureUsage::CopySource | TextureUsage::CopyDestination;
        auto                              texture  = GPU->CreateTexture(desc);
        auto                              commands = GPU->CreateCommandList();
        std::vector<std::vector<uint8_t>> patterns;
        std::vector<Handle<Buffer>>       outputs;
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            const size_t         bytes = size_t(std::max(1u, 3u >> mip)) * std::max(1u, 5u >> mip) * 2;
            std::vector<uint8_t> raw(bytes + 2);
            for (size_t i = 0; i < bytes; ++i)
                raw[i + 2] = uint8_t(i * 37 + mip * 53 + 11);
            auto upload = Upload(raw.data(), raw.size());
            commands->CopyBufferToTexture(upload, 2, texture, {mip, 0});
            patterns.emplace_back(raw.begin() + 2, raw.end());
            outputs.push_back(Readback(bytes + 2));
        }
        for (uint32_t mip = 0; mip < 3; ++mip)
            commands->CopyTextureToBuffer(texture, {mip, 0}, outputs[mip], 2);
        GPU->Submit(commands);
        GPU->WaitIdle();
        for (uint32_t mip = 0; mip < 3; ++mip)
            EXPECT_EQ(outputs[mip]->Read(2, patterns[mip].size()), patterns[mip]);
    }

    INSTANTIATE_TEST_SUITE_P(Backends,
                             RHIConformance,
                             testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                             [](const testing::TestParamInfo<GpuTestBackend>& info) {
                                 return info.param == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan";
                             });
} // namespace
