#include <imgui.h>
#include <array>
#include <gtest/gtest.h>
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/RHI/ImGuiRenderer.h"
#include "Kans3D/RHI/Vulkan/VulkanPresentation.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Resource/GpuShader.h"
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "GpuReadback.h"
#include "GpuTestContext.h"
using namespace Kans;
namespace RH = Kans::RenderHardware;
namespace
{
    class EditorTextureGpu : public testing::Test
    {
    protected:
        GpuTestContext                     Context {GpuTestBackend::Vulkan};
        std::unique_ptr<RH::ImGuiRenderer> UI;
        void                               SetUp() override
        {
            ASSERT_NO_FATAL_FAILURE(Context.SetUp());
            ImGui::CreateContext();
            auto& io       = ImGui::GetIO();
            io.DisplaySize = {16, 16};
            io.DeltaTime   = 1.0f / 60;
            io.IniFilename = nullptr;
            UI             = RH::CreateImGuiRenderer(RH::Backend::Vulkan, nullptr);
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
            const auto             bytes = ReadGpuTexture(RH::GetVulkanImGuiOffscreenTarget(*UI));
            std::array<uint8_t, 4> pixel;
            std::copy_n(bytes.data() + (8 * 16 + 8) * 4, 4, pixel.data());
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
    TEST_F(EditorTextureGpu, ConsumersReuseTheInitializedDeviceAndItsResources)
    {
        // Bug: a lazy consumer or a second initialization constructs a separate device.
        auto device = RH::GetSharedDevice();
        EXPECT_EQ(RH::InitializeDevice(RH::Backend::Vulkan), device);
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
        BuildSingleSection(meshData);
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
    TEST_F(EditorTextureGpu, ImGuiSamplesLinearAndSrgbProductsThroughTheExportedId)
    {
        // Bugs: the wrapper loses sRGB/linear format, exports the wrong native ID, or exports an incomplete image.
        for (bool srgb : {false, true})
        {
            BeginUI();
            auto texture = Product(srgb);
            ASSERT_TRUE(texture);
            auto id = texture->GetImGuiTextureID();
            EXPECT_EQ(id, texture->GetDevice()->GetImGuiTextureID(texture->GetResource()));
            ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(id), {0, 0}, {16, 16});
            DrawUI();
            auto pixel = Pixel();
            // IEC sRGB decoding of {128,64,32}, rounded to linear RGBA8. One byte for GL conversion precision.
            auto expected = srgb ? std::array<uint8_t, 3> {55, 13, 4} : std::array<uint8_t, 3> {128, 64, 32};
            for (int c = 0; c < 3; ++c)
                EXPECT_NEAR(pixel[c], expected[c], srgb ? 1 : 0);
            UI->EndFrame();
        }
    }
    TEST_F(EditorTextureGpu, ReleasedThumbnailSurvivesInterveningRetirementUntilImGuiDraws)
    {
        // Bug: resizing/replacing a thumbnail destroys its texture after AddImage but before ImGui executes it.
        BeginUI();
        auto texture = Product(false);
        ASSERT_TRUE(texture);
        auto id = texture->GetImGuiTextureID();
        ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(id), {0, 0}, {16, 16});
        std::weak_ptr<RH::Texture> weak = texture->GetResource();
        texture.reset();
        RH::GetSharedDevice()->WaitIdle();
        EXPECT_FALSE(weak.expired());
        DrawUI();
        auto pixel = Pixel();
        EXPECT_EQ(pixel[0], 128);
        EXPECT_EQ(pixel[1], 64);
        EXPECT_EQ(pixel[2], 32);
        UI->EndFrame();
        RH::GetSharedDevice()->WaitIdle();
        EXPECT_TRUE(weak.expired());
    }
    TEST_F(EditorTextureGpu, RealImGuiDrawIgnoresTheLastRhiTargetMaskSrgbAndWireframe)
    {
        // Bugs: ImGui draws into the last offscreen target, inherits its RGB mask/sRGB encoding, or remains wireframe.
        BeginUI();
        auto texture = Product(false);
        ASSERT_TRUE(texture);
        ImGui::GetBackgroundDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(texture->GetImGuiTextureID()), {0, 0}, {16, 16});
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
        DrawUI();
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
        UI->EndFrame();
    }
} // namespace
