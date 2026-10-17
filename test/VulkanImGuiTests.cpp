#include <imgui.h>
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include "Kans3D/RHI/ImGuiRenderer.h"
#include "Kans3D/RHI/Vulkan/VulkanPresentation.h"
#include "GpuTestContext.h"
#include "data/ImGuiHeadPixels.h"
using namespace Kans::RenderHardware;
namespace
{
    class VulkanImGui : public testing::Test
    {
    protected:
        GpuTestContext                 Context {GpuTestBackend::Vulkan};
        std::shared_ptr<Device>        GPU;
        std::unique_ptr<ImGuiRenderer> UI;
        bool                           HasContext = false;
        void                           SetUp() override
        {
            Context.SetUp();
            ASSERT_FALSE(HasFatalFailure());
            GPU = GetSharedDevice();
            ImGui::CreateContext();
            HasContext                 = true;
            ImGui::GetIO().IniFilename = nullptr;
            UI                         = CreateImGuiRenderer(Backend::Vulkan, nullptr);
        }
        void TearDown() override
        {
            UI.reset();
            if (HasContext)
                ImGui::DestroyContext();
            GPU.reset();
            Context.TearDown();
        }
        void Start()
        {
            UI->NewFrame();
            ImGui::GetIO().DisplaySize = {64, 32};
            ImGui::GetIO().DeltaTime   = 1.0f / 60;
            ImGui::NewFrame();
        }
        Handle<Texture> Upload(Format format, TextureUsage extra = TextureUsage {})
        {
            auto          texture = GPU->CreateTexture({1,
                                                        1,
                                                        1,
                                                        TextureDimension::Texture2D,
                                                        format,
                                                        TextureUsage::Sampled | TextureUsage::CopyDestination | extra});
            const uint8_t pixel[] = {128, 64, 192, 255};
            auto          buffer  = GPU->CreateBuffer({4, BufferUsage::CopySource, MemoryUsage::Upload});
            buffer->Write(0, pixel, 4);
            auto commands = GPU->CreateCommandList();
            commands->CopyBufferToTexture(buffer, 0, texture, {});
            GPU->Submit(commands);
            return texture;
        }
        std::vector<uint8_t> Finish()
        {
            ImGui::Render();
            UI->Render(ImGui::GetDrawData());
            auto target   = GetVulkanImGuiOffscreenTarget(*UI);
            auto readback = GPU->CreateBuffer({64 * 32 * 4, BufferUsage::CopyDestination, MemoryUsage::Readback});
            auto commands = GPU->CreateCommandList();
            commands->CopyTextureToBuffer(target, {}, readback);
            GPU->Submit(commands);
            GPU->WaitIdle();
            auto pixels = readback->Read(0, 64 * 32 * 4);
            UI->EndFrame();
            return pixels;
        }
        std::array<uint8_t, 3> At(const std::vector<uint8_t>& pixels, unsigned x, unsigned y)
        {
            auto offset = (y * 64 + x) * 4;
            return {pixels.at(offset), pixels.at(offset + 1), pixels.at(offset + 2)};
        }
    };
} // namespace
TEST_F(VulkanImGui, DescriptorsRetainReleasedImagesAndTrackUploadAndAttachmentLayouts)
{
    // Catches premature descriptor/image destruction across WaitIdle, descriptors
    // using the wrong view/sampler, and missing transfer/attachment -> sample barriers.
    Start();
    auto linear = Upload(Format::RGBA8Unorm), srgb = Upload(Format::RGBA8Srgb);
    auto target = GPU->CreateTexture(
        {1, 1, 1, TextureDimension::Texture2D, Format::RGBA8Unorm, TextureUsage::Sampled | TextureUsage::RenderTarget});
    auto          clear = GPU->CreateCommandList();
    RenderingDesc rendering;
    rendering.Colors.push_back({target, {}, LoadOp::Clear, StoreOp::Store, {0.2f, 0.4f, 0.6f, 1}});
    clear->BeginRendering(rendering);
    clear->EndRendering();
    GPU->Submit(clear);
    auto linearID = GPU->GetImGuiTextureID(linear), srgbID = GPU->GetImGuiTextureID(srgb),
         targetID = GPU->GetImGuiTextureID(target);
    EXPECT_NE(linearID, srgbID);
    EXPECT_EQ(linearID, GPU->GetImGuiTextureID(linear));
    SamplerDesc nearest;
    nearest.Min = nearest.Mag = Filter::Nearest;
    EXPECT_NE(linearID, GPU->GetImGuiTextureID(linear, &nearest));
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->AddImage(reinterpret_cast<ImTextureID>(linearID), {0, 0}, {20, 32});
    draw->AddImage(reinterpret_cast<ImTextureID>(srgbID), {20, 0}, {40, 32});
    draw->AddImage(reinterpret_cast<ImTextureID>(targetID), {40, 0}, {64, 32}, {0, 1}, {1, 0});
    std::weak_ptr<Texture> weak = linear, weakSrgb = srgb, weakTarget = target;
    linear.reset();
    srgb.reset();
    target.reset();
    clear.reset();
    rendering.Colors.clear();
    GPU->WaitIdle();
    EXPECT_FALSE(weak.expired());
    EXPECT_FALSE(weakSrgb.expired());
    EXPECT_FALSE(weakTarget.expired());
    auto pixels = Finish();
    EXPECT_EQ(At(pixels, 10, 16), (std::array<uint8_t, 3> {128, 64, 192}));
    auto decoded = At(pixels, 30, 16);
    EXPECT_NEAR(decoded[0], 55, 1);
    EXPECT_NEAR(decoded[1], 13, 1);
    EXPECT_NEAR(decoded[2], 134, 1);
    EXPECT_EQ(At(pixels, 52, 16), (std::array<uint8_t, 3> {51, 102, 153}));
    EXPECT_TRUE(weak.expired());
    EXPECT_TRUE(weakSrgb.expired());
    EXPECT_TRUE(weakTarget.expired());
}
TEST_F(VulkanImGui, FontsAndUnencodedUIColoursRenderToTheExpectedRows)
{
    // Catches a missing font upload, UNORM replaced by SRGB, flipped UI rows,
    // and stale dynamic scene rendering state bleeding into ImGui's render pass.
    Start();
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled({0, 0}, {64, 32}, IM_COL32(37, 91, 163, 255));
    draw->AddRectFilled({32, 16}, {64, 32}, IM_COL32(51, 102, 153, 255));
    draw->AddText({2, 2}, IM_COL32_WHITE, "Vulkan");
    auto pixels = Finish();
    EXPECT_EQ(At(pixels, 60, 4), (std::array<uint8_t, 3> {37, 91, 163}));
    EXPECT_EQ(At(pixels, 60, 28), (std::array<uint8_t, 3> {51, 102, 153}));
    size_t fontPixels = 0;
    for (unsigned y = 2; y < 15; ++y)
        for (unsigned x = 2; x < 30; ++x)
            if (At(pixels, x, y)[0] > 37)
                ++fontPixels;
    EXPECT_GT(fontPixels, 20u);
}
TEST_F(VulkanImGui, RenderTargetUVAdaptationRestoresDrawListsAndPreservesUploadedIcons)
{
    // Catches mirrored editor viewport/preview exports, a double flip on redraw,
    // or applying the framebuffer-origin adjustment to authored icon textures.
    Start();
    auto make = [&](TextureUsage usage) {
        auto          texture = GPU->CreateTexture({2,
                                                    2,
                                                    1,
                                                    TextureDimension::Texture2D,
                                                    Format::RGBA8Unorm,
                                                    TextureUsage::Sampled | TextureUsage::CopyDestination | usage});
        const uint8_t bytes[] = {37, 91, 163, 255, 37, 91, 163, 255, 51, 102, 153, 255, 51, 102, 153, 255};
        auto          buffer  = GPU->CreateBuffer({16, BufferUsage::CopySource, MemoryUsage::Upload});
        buffer->Write(0, bytes, 16);
        auto commands = GPU->CreateCommandList();
        commands->CopyBufferToTexture(buffer, 0, texture, {});
        GPU->Submit(commands);
        return texture;
    };
    auto        scene = make(TextureUsage::RenderTarget), icon = make(TextureUsage {});
    SamplerDesc sampling;
    sampling.Min = sampling.Mag = Filter::Nearest;
    sampling.U = sampling.V = AddressMode::ClampToEdge;
    auto* draw              = ImGui::GetBackgroundDrawList();
    draw->AddImage(
        reinterpret_cast<ImTextureID>(GPU->GetImGuiTextureID(scene, &sampling)), {0, 0}, {32, 32}, {0, 1}, {1, 0});
    draw->AddImage(reinterpret_cast<ImTextureID>(GPU->GetImGuiTextureID(icon, &sampling)), {32, 0}, {64, 32});
    ImGui::Render();
    auto*                   data = ImGui::GetDrawData();
    std::vector<ImDrawVert> before(draw->VtxBuffer.begin(), draw->VtxBuffer.end());
    UI->Render(data);
    GPU->WaitIdle();
    UI->Render(data);
    EXPECT_EQ(std::memcmp(before.data(), draw->VtxBuffer.Data, before.size() * sizeof(ImDrawVert)), 0);
    auto target   = GetVulkanImGuiOffscreenTarget(*UI);
    auto buffer   = GPU->CreateBuffer({64 * 32 * 4, BufferUsage::CopyDestination, MemoryUsage::Readback});
    auto commands = GPU->CreateCommandList();
    commands->CopyTextureToBuffer(target, {}, buffer);
    GPU->Submit(commands);
    GPU->WaitIdle();
    auto pixels = buffer->Read(0, 64 * 32 * 4);
    EXPECT_EQ(At(pixels, 16, 4), (std::array<uint8_t, 3> {37, 91, 163}));
    EXPECT_EQ(At(pixels, 48, 4), (std::array<uint8_t, 3> {37, 91, 163}));
    EXPECT_EQ(At(pixels, 16, 28), (std::array<uint8_t, 3> {51, 102, 153}));
    UI->EndFrame();
}
TEST(VulkanWindowFrames, SlotReuseRetiresOnlyCompletedResourcesAtBoundsOneTwoAndThree)
{
    ASSERT_STREQ(std::getenv("VK_DRIVER_FILES"), "/usr/share/vulkan/icd.d/lvp_icd.json");
    ASSERT_STREQ(std::getenv("VK_ICD_FILENAMES"), "/usr/share/vulkan/icd.d/lvp_icd.json");
    for (unsigned slots : {1u, 2u, 3u})
    {
        VulkanDeviceOptions options;
        options.RequireCpuDevice = true;
        options.Validation = options.SynchronizationValidation = true;
        options.AsynchronousWindowFrames                       = true;
        options.ValidationError = [](const std::string& message) { ADD_FAILURE() << message; };
        DeviceDesc desc;
        desc.FramesInFlight                        = slots;
        auto                                device = CreateVulkanDevice(desc, options);
        std::vector<std::weak_ptr<Texture>> images;
        double                              endNs = 0, beginNs = 0;
        for (unsigned frame = 0; frame < 24; ++frame)
        {
            auto start = std::chrono::steady_clock::now();
            BeginVulkanDeviceWindowFrame(*device);
            beginNs += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
            if (frame >= slots)
                EXPECT_TRUE(images[frame - slots].expired());
            device->BeginFrame();
            auto image = device->CreateTexture({256,
                                                256,
                                                1,
                                                TextureDimension::Texture2D,
                                                Format::RGBA8Unorm,
                                                TextureUsage::RenderTarget | TextureUsage::CopySource});
            images.push_back(image);
            auto          command = device->CreateCommandList();
            RenderingDesc render;
            render.Colors.push_back({image, {}, LoadOp::Clear, StoreOp::Store, {0.2f, 0.4f, 0.6f, 1}});
            command->BeginRendering(render);
            command->EndRendering();
            device->Submit(command);
            command.reset();
            image.reset();
            start = std::chrono::steady_clock::now();
            device->EndFrame();
            endNs += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
            EXPECT_FALSE(images.back().expired());
            FinishVulkanDeviceWindowFrame(*device);
            EXPECT_FALSE(images.back().expired());
        }
        device->WaitIdle();
        for (auto& image : images)
            EXPECT_TRUE(image.expired());
        EXPECT_EQ(device->GetStats().LiveResources, 0u);
        std::cout << "Window slots=" << slots << " mean EndFrame=" << endNs / 24
                  << " ns; slot wait+retirement=" << beginNs / 24 << " ns\n";
    }
}

TEST(VulkanImGuiParity, OpaqueAlphaAndFontPixelsMatchHead)
{
    // Exact HEAD (5a8a2af) Vulkan pixels: opaque/alpha rectangles and the default font.
    const std::vector<uint8_t> reference(std::begin(ImGuiHeadPixels), std::end(ImGuiHeadPixels));
    for (auto backend : {GpuTestBackend::Vulkan})
    {
        GpuTestContext context(backend);
        context.SetUp();
        ASSERT_FALSE(HasFatalFailure());
        auto device = GetSharedDevice();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        auto renderer              = CreateImGuiRenderer(Backend::Vulkan, nullptr);
        renderer->NewFrame();
        ImGui::GetIO().DisplaySize = {16, 16};
        ImGui::GetIO().DeltaTime   = 1.0f / 60;
        ImGui::NewFrame();
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->AddRectFilled({0, 0}, {16, 16}, IM_COL32(37, 91, 163, 255));
        draw->AddRectFilled({0, 8}, {16, 16}, IM_COL32(127, 203, 15, 128));
        draw->AddText({2, 1}, IM_COL32_WHITE, "A");
        ImGui::Render();
        renderer->Render(ImGui::GetDrawData());
        renderer->EndFrame();
        std::vector<uint8_t> pixels(16 * 16 * 4);
        {
            auto target   = GetVulkanImGuiOffscreenTarget(*renderer);
            auto buffer   = device->CreateBuffer({pixels.size(), BufferUsage::CopyDestination, MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(target, {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            pixels = buffer->Read(0, pixels.size());
            EXPECT_EQ(pixels, reference);
        }
        renderer.reset();
        ImGui::DestroyContext();
        device.reset();
        context.TearDown();
    }
}
