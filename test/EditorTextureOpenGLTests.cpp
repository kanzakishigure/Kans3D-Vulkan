#include <glad/glad.h>
#include <imgui.h>
#include <array>
#include <gtest/gtest.h>
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/RHI/ImGuiRenderer.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Resource/GpuShader.h"
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "GpuTestContext.h"
using namespace Kans;
namespace RH = Kans::RenderHardware;
namespace
{
    class EditorTextureGL : public testing::Test
    {
    protected:
        GpuTestContext                     Context {GpuTestBackend::OpenGL};
        std::unique_ptr<RH::ImGuiRenderer> UI;
        void                               SetUp() override
        {
            ASSERT_NO_FATAL_FAILURE(Context.SetUp());
            ASSERT_EQ(glIsEnabled(GL_DITHER), GL_TRUE); // Measured legacy UI context default, before any RHI replay.
            ImGui::CreateContext();
            auto& io       = ImGui::GetIO();
            io.DisplaySize = {16, 16};
            io.DeltaTime   = 1.0f / 60;
            io.IniFilename = nullptr;
            UI             = RH::CreateImGuiRenderer(RH::Backend::OpenGL, nullptr);
        }
        void TearDown() override
        {
            UI.reset();
            ImGui::DestroyContext();
            Renderer::Shutdown();
            Context.TearDown();
        }
        void BeginUI()
        {
            UI->NewFrame();
            ImGui::NewFrame();
        }
        void DrawUI()
        {
            ImGui::Render();
            UI->Render(ImGui::GetDrawData());
        }
        std::array<uint8_t, 4> Pixel()
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            std::array<uint8_t, 4> pixel;
            glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            return pixel;
        }
        Ref<Texture2D> Product(bool srgb)
        {
            TextureData data;
            data.Width = data.Height = 1;
            data.ColorSpace          = srgb ? TextureColorSpace::SRGB : TextureColorSpace::Linear;
            data.Mips                = {{{std::byte {128}, std::byte {64}, std::byte {32}, std::byte {255}}}};
            auto uploaded            = Texture2D::CreateFromProduct(data);
            if (auto* error = std::get_if<AssetError>(&uploaded))
            {
                ADD_FAILURE() << error->Message;
                return nullptr;
            }
            return std::get<Ref<Texture2D>>(std::move(uploaded));
        }
    };
    TEST_F(EditorTextureGL, ConsumersReuseTheInitializedDeviceAndItsResources)
    {
        // Bug: a lazy consumer or a second initialization constructs a separate device.
        auto device = RH::GetSharedDevice();
        EXPECT_EQ(RH::InitializeDevice(RH::Backend::OpenGL), device);
        Renderer::Init();
        auto product = Product(false);
        ASSERT_TRUE(product);
        auto white = Renderer::GetWhiteTexture();
        ASSERT_TRUE(white);
        EXPECT_EQ(product->GetDevice(), device);
        EXPECT_EQ(white->GetDevice(), device);
        auto meshData = MeshData {};
        meshData.Vertices.resize(3);
        meshData.Indices = {0, 1, 2};
        auto uploaded    = UploadMesh(meshData);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded));
        EXPECT_EQ(std::get<GpuMesh>(uploaded).Device, device);
        // Ownership equality alone misses creating resources on another device: the shared device must accept them.
        auto commands = device->CreateCommandList();
        auto bytes    = device->CreateBuffer({4, RH::BufferUsage::CopyDestination, RH::MemoryUsage::Readback});
        commands->CopyTextureToBuffer(product->GetResource(), {}, bytes, 0);
        device->Submit(commands);
        device->WaitIdle();
        EXPECT_EQ(bytes->Read(0, 4), (std::vector<uint8_t> {128, 64, 32, 255}));
    }
    TEST_F(EditorTextureGL, ImGuiSamplesLinearAndSrgbProductsThroughTheExportedId)
    {
        // Bugs: the wrapper loses sRGB/linear format, exports the wrong native ID, or exports an incomplete image.
        for (bool srgb : {false, true})
        {
            BeginUI();
            auto texture = Product(srgb);
            ASSERT_TRUE(texture);
            auto id = texture->GetImGuiTextureID();
            EXPECT_EQ(id, texture->GetDevice()->GetImGuiTextureID(texture->GetResource()));
            EXPECT_EQ(texture->GetRenererID(), id);
            ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(id), {0, 0}, {16, 16});
            DrawUI();
            auto pixel = Pixel();
            // IEC sRGB decoding of {128,64,32}, rounded to linear RGBA8. One byte for GL conversion precision.
            auto expected = srgb ? std::array<uint8_t, 3> {55, 13, 4} : std::array<uint8_t, 3> {128, 64, 32};
            for (int c = 0; c < 3; ++c)
                EXPECT_NEAR(pixel[c], expected[c], srgb ? 1 : 0);
            UI->EndFrame();
        }
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }
    TEST_F(EditorTextureGL, ReleasedThumbnailSurvivesInterveningRetirementUntilImGuiDraws)
    {
        // Bug: resizing/replacing a thumbnail destroys its texture after AddImage but before ImGui executes it.
        BeginUI();
        auto texture = Product(false);
        ASSERT_TRUE(texture);
        auto id = texture->GetImGuiTextureID();
        ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(id), {0, 0}, {16, 16});
        texture.reset();
        RH::GetSharedDevice()->WaitIdle();
        EXPECT_EQ(glIsTexture(GLuint(id)), GL_TRUE);
        DrawUI();
        auto pixel = Pixel();
        EXPECT_EQ(pixel[0], 128);
        EXPECT_EQ(pixel[1], 64);
        EXPECT_EQ(pixel[2], 32);
        UI->EndFrame();
        EXPECT_EQ(glIsTexture(GLuint(id)), GL_FALSE);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }
    TEST_F(EditorTextureGL, RealImGuiDrawIgnoresTheLastRhiTargetMaskSrgbAndWireframe)
    {
        // Bugs: ImGui draws into the last offscreen target, inherits its RGB mask/sRGB encoding, or remains wireframe.
        BeginUI();
        auto texture = Product(false);
        ASSERT_TRUE(texture);
        ImGui::GetBackgroundDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(texture->GetImGuiTextureID()), {0, 0}, {16, 16});
        // A nonzero default framebuffer makes an inherited XOR operation observable.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(32.0f / 255, 16.0f / 255, 8.0f / 255, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        auto            device = RH::GetSharedDevice();
        RH::TextureDesc desc;
        desc.Width = desc.Height   = 16;
        desc.PixelFormat           = RH::Format::RGBA8Srgb;
        desc.Usage                 = RH::TextureUsage::RenderTarget | RH::TextureUsage::CopySource;
        auto                target = device->CreateTexture(desc);
        RH::RenderingDesc   scope;
        RH::ColorAttachment color;
        color.Target     = target;
        color.Load       = RH::LoadOp::Clear;
        color.ClearColor = {0, 0, 0, 1};
        scope.Colors     = {color};
        auto commands    = device->CreateCommandList();
        commands->BeginRendering(scope);
        commands->EndRendering();
        device->Submit(commands);
        // A test-owned native FBO makes omitted UI framebuffer setup observably corrupt the RHI target.
        GLuint framebuffer = 0;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GLuint(device->GetImGuiTextureID(target)), 0);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
        glEnable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_RASTERIZER_DISCARD);
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(GL_XOR);
        glEnable(GL_CLIP_DISTANCE0);
        // State a geometry pipeline can leave behind. ImGui's bundled backend does not set the colour mask.
        glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_STENCIL_TEST);
        GLboolean uiDither = GL_FALSE;
        // Bug: replay's disabled dithering leaks into the UI; the bundled renderer does not set it.
        ImGui::GetBackgroundDrawList()->AddCallback(
            [](const ImDrawList*, const ImDrawCmd* command) {
                *static_cast<GLboolean*>(command->UserCallbackData) = glIsEnabled(GL_DITHER);
            },
            &uiDither);
        DrawUI();
        EXPECT_EQ(uiDither, GL_TRUE);
        EXPECT_EQ(glIsEnabled(GL_DITHER), GL_FALSE); // Only the native UI boundary changes this state.
        auto pixel = Pixel();
        EXPECT_EQ(pixel[0], 128);
        EXPECT_EQ(pixel[1], 64);
        EXPECT_EQ(pixel[2], 32);
        auto readback =
            device->CreateBuffer({16 * 16 * 4, RH::BufferUsage::CopyDestination, RH::MemoryUsage::Readback});
        commands = device->CreateCommandList();
        commands->CopyTextureToBuffer(target, {}, readback, 0);
        device->Submit(commands);
        device->WaitIdle();
        const auto data = readback->Read(0, 16 * 16 * 4);
        for (size_t p = 0; p < data.size(); p += 4)
        {
            EXPECT_EQ(data[p], 0);
            EXPECT_EQ(data[p + 1], 0);
            EXPECT_EQ(data[p + 2], 0);
            EXPECT_EQ(data[p + 3], 255);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
        UI->EndFrame();
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }
} // namespace
