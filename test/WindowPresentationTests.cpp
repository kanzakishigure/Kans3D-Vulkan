#include <glad/glad.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#include "Kans3D/Core/Window.h"
#include "Kans3D/RHI/ImGuiRenderer.h"
#include "Kans3D/RHI/Vulkan/VulkanDevice.h"
#include "Kans3D/RHI/Vulkan/VulkanTestProbes.h"
using namespace Kans;
using namespace Kans::RenderHardware;
namespace
{
    class TestWindow : public Window
    {
    public:
        GLFWwindow*         Native = nullptr;
        WindowSpecification Spec;
        void                Init() override {}
        void                ProcessEvents() override { glfwPollEvents(); }
        void                SwapBuffers() override { PresentWindow(*this); }
        unsigned            GetWidth() const override
        {
            int w, h;
            if (!Native)
                return Spec.Width;
            glfwGetWindowSize(Native, &w, &h);
            return w;
        }
        unsigned GetHeight() const override
        {
            int w, h;
            if (!Native)
                return Spec.Height;
            glfwGetWindowSize(Native, &w, &h);
            return h;
        }
        const WindowSpecification& GetWindowSpecification() const override { return Spec; }
        const void                 SetWindowSpecification(const WindowSpecification& spec) override { Spec = spec; }
        void                       SetEventCallback(const EventCallbackFn&) override {}
        void                       SetVSync(bool enabled) override
        {
            SetWindowVSync(*this, enabled);
            Spec.VSync = enabled;
        }
        bool  IsVSync() const override { return Spec.VSync; }
        void* GetNativeWindow() const override { return Native; }
    };
    struct Ppm
    {
        int                    Width = 0, Height = 0;
        std::vector<uint8_t>   Pixels;
        std::array<uint8_t, 3> At(int x, int y) const
        {
            auto i = (size_t(y) * Width + x) * 3;
            return {Pixels.at(i), Pixels.at(i + 1), Pixels.at(i + 2)};
        }
    };
    Ppm ReadPpm(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        std::string   magic;
        int           max = 0;
        Ppm           result;
        input >> magic >> result.Width >> result.Height >> max;
        EXPECT_EQ(magic, "P6");
        EXPECT_EQ(max, 255);
        if (!input || result.Width <= 0 || result.Height <= 0)
            return result;
        input.get();
        result.Pixels.resize(size_t(result.Width) * result.Height * 3);
        input.read(reinterpret_cast<char*>(result.Pixels.data()), result.Pixels.size());
        EXPECT_TRUE(input.good());
        return result;
    }
    class WindowPresentation : public testing::TestWithParam<Backend>
    {
    protected:
        TestWindow                     Window;
        std::shared_ptr<Device>        GPU;
        std::unique_ptr<ImGuiRenderer> UI;
        bool                           Context = false;
        std::filesystem::path          CapturePath;
        void                           SetUp() override
        {
            if (GetParam() == Backend::Vulkan)
            {
                ASSERT_STREQ(std::getenv("VK_DRIVER_FILES"), "/usr/share/vulkan/icd.d/lvp_icd.json");
                ASSERT_STREQ(std::getenv("VK_ICD_FILENAMES"), "/usr/share/vulkan/icd.d/lvp_icd.json");
                ASSERT_STREQ(std::getenv("KANS_VULKAN_REQUIRE_CPU"), "1");
            }
            bool headless = false;
            if (GetParam() == Backend::Vulkan)
            {
                uint32_t count = 0;
                ASSERT_EQ(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), VK_SUCCESS);
                std::vector<VkExtensionProperties> extensions(count);
                ASSERT_EQ(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()), VK_SUCCESS);
                headless = std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
                    return std::strcmp(extension.extensionName, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME) == 0;
                });
            }
            Window.Spec.Width        = 96;
            Window.Spec.Height       = 72;
            Window.Spec.HideTitlebar = false;
            SetBackend(GetParam());
            if (!headless)
            {
                if (!glfwInit())
                    GTEST_SKIP() << "Hidden GLFW surface unavailable: no accessible X11/Wayland display and no Vulkan "
                                    "headless surface.";
                glfwDefaultWindowHints();
                glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
                Window.Native = static_cast<GLFWwindow*>(CreateWindowSurface(GetParam(), Window.Spec));
                ASSERT_NE(Window.Native, nullptr);
            }
            if (GetParam() == Backend::Vulkan)
            {
                VulkanDeviceOptions options;
                options.NativeWindow              = Window.Native;
                options.HeadlessSurface           = headless;
                options.AsynchronousWindowFrames  = true;
                options.RequireCpuDevice          = true;
                options.Validation                = true;
                options.SynchronizationValidation = true;
                options.ValidationError           = [](const std::string& message) { ADD_FAILURE() << message; };
                GPU                               = CreateVulkanDevice({}, options);
                InitializeHeadlessDevice(GetParam(), GPU);
                EXPECT_EQ(VulkanTestProbes::Inspect(*GPU).Type, VK_PHYSICAL_DEVICE_TYPE_CPU);
                if (Window.Native)
                    EXPECT_EQ(glfwGetWindowAttrib(Window.Native, GLFW_CLIENT_API), GLFW_NO_API);
            }
            else
                GPU = InitializeDevice(GetParam(), &Window);
            ImGui::CreateContext();
            Context                    = true;
            ImGui::GetIO().IniFilename = nullptr;
            UI                         = CreateImGuiRenderer(GetParam(), &Window);
            CapturePath = std::filesystem::temp_directory_path() /
                          (GetParam() == Backend::Vulkan ? "kans-present-vk.ppm" : "kans-present-gl.ppm");
            std::filesystem::remove(CapturePath);
        }
        void TearDown() override
        {
            UI.reset();
            if (Context)
                ImGui::DestroyContext();
            ShutdownDevice();
            GPU.reset();
            if (Window.Native)
                glfwDestroyWindow(Window.Native);
            glfwTerminate();
            if (!CapturePath.empty())
                std::filesystem::remove(CapturePath);
        }
        void Start()
        {
            BeginWindowFrame(Window);
            UI->NewFrame();
            int width = Window.GetWidth(), height = Window.GetHeight();
            ImGui::GetIO().DisplaySize = {float(width), float(height)};
            ImGui::NewFrame();
        }
        Ppm Finish(bool capture = true, bool late = false)
        {
            if (capture && !late)
                RequestFrameCapture(Window, CapturePath.string());
            ImGui::Render();
            UI->Render(ImGui::GetDrawData());
            UI->EndFrame();
            if (capture && late)
                RequestFrameCapture(Window, CapturePath.string());
            Window.SwapBuffers();
            return capture ? ReadPpm(CapturePath) : Ppm {};
        }
    };
} // namespace
TEST_P(WindowPresentation, ResizeVSyncAndCaptureReadTheCurrentImageWithUnencodedUIColours)
{
    // Catches stale image views/recreation leaks, missing present semaphore waits,
    // accidental sRGB attachment encoding, previous-image capture and row/BGRA swaps.
    for (int frame = 0; frame < 12; ++frame)
    {
        if (frame % 3 == 0)
        {
            Window.Spec.Width  = 96 + frame * 2;
            Window.Spec.Height = 72 + frame;
            if (Window.Native)
                glfwSetWindowSize(Window.Native, Window.Spec.Width, Window.Spec.Height);
            Window.SetVSync(frame % 2 == 0);
            if (Window.Native)
                glfwPollEvents();
        }
        Start();
        auto*     draw = ImGui::GetBackgroundDrawList();
        const int r = 31 + frame * 11, g = 53 + frame * 7, b = 79 + frame * 3;
        auto      size = ImGui::GetIO().DisplaySize;
        draw->AddRectFilled({0, 0}, size, IM_COL32(r, g, b, 255));
        draw->AddRectFilled({0, size.y * 0.5f}, size, IM_COL32(b, r, g, 255));
        auto image = Finish(true, frame % 2 != 0);
        int  w = Window.GetWidth(), h = Window.GetHeight();
        if (Window.Native)
            glfwGetFramebufferSize(Window.Native, &w, &h);
        ASSERT_EQ(image.Width, w);
        ASSERT_EQ(image.Height, h);
        EXPECT_EQ(image.At(w / 2, h / 4), (std::array<uint8_t, 3> {uint8_t(r), uint8_t(g), uint8_t(b)}));
        EXPECT_EQ(image.At(w / 2, h * 3 / 4), (std::array<uint8_t, 3> {uint8_t(b), uint8_t(r), uint8_t(g)}));
    }
}
TEST_P(WindowPresentation, ReleasedTextureSurvivesUIEndAndInterveningIdleUntilPresent)
{
    // Concrete bug: EndFrame releases exported IDs before main-window present.
    // Also catches descriptor-layout transitions omitted for upload -> UI sampling.
    Start();
    const std::array<uint8_t, 4> bytes {37, 91, 163, 255};
    auto                         texture = GPU->CreateTexture({1,
                                                               1,
                                                               1,
                                                               TextureDimension::Texture2D,
                                                               Format::RGBA8Unorm,
                                                               TextureUsage::Sampled | TextureUsage::CopyDestination});
    auto                         upload  = GPU->CreateBuffer({4, BufferUsage::CopySource, MemoryUsage::Upload});
    upload->Write(0, bytes.data(), bytes.size());
    auto commands = GPU->CreateCommandList();
    commands->CopyBufferToTexture(upload, 0, texture, {});
    GPU->Submit(commands);
    const auto id = GPU->GetImGuiTextureID(texture);
    EXPECT_NE(id, 0u);
    EXPECT_EQ(id, GPU->GetImGuiTextureID(texture));
    ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(id), {0, 0}, ImGui::GetIO().DisplaySize);
    std::weak_ptr<Texture> weak = texture;
    texture.reset();
    commands.reset();
    upload.reset();
    GPU->WaitIdle();
    EXPECT_FALSE(weak.expired());
    RequestFrameCapture(Window, CapturePath.string());
    ImGui::Render();
    UI->Render(ImGui::GetDrawData());
    UI->EndFrame();
    // GL's existing contract ends at UI draw completion. Vulkan needs the present boundary.
    if (GetParam() == Backend::Vulkan)
    {
        GPU->WaitIdle();
        EXPECT_FALSE(weak.expired());
    }
    Window.SwapBuffers();
    auto capture = ReadPpm(CapturePath);
    ASSERT_GT(capture.Width, 0);
    EXPECT_EQ(capture.At(capture.Width / 2, capture.Height / 2), (std::array<uint8_t, 3> {37, 91, 163}));
    GPU->WaitIdle();
    EXPECT_TRUE(weak.expired());
}
TEST_P(WindowPresentation, RenderTargetExportPreservesTheEditorsOpenGLUVConvention)
{
    // Concrete bug: Vulkan viewport/preview appears vertically mirrored with the
    // editor's unchanged (0,1)->(1,0) UVs. Each half has a different exact colour.
    Start();
    auto target =
        GPU->CreateTexture({4,
                            4,
                            1,
                            TextureDimension::Texture2D,
                            Format::RGBA8Unorm,
                            TextureUsage::Sampled | TextureUsage::RenderTarget | TextureUsage::CopyDestination});
    std::array<uint8_t, 64> pixels {};
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
        {
            const bool top    = GPU->GetCoordinateConventions().Origin == FramebufferOrigin::UpperLeft ? y < 2 : y >= 2;
            auto       offset = (y * 4 + x) * 4;
            pixels[offset]    = top ? 37 : 51;
            pixels[offset + 1] = top ? 91 : 102;
            pixels[offset + 2] = top ? 163 : 153;
            pixels[offset + 3] = 255;
        }
    auto upload = GPU->CreateBuffer({pixels.size(), BufferUsage::CopySource, MemoryUsage::Upload});
    upload->Write(0, pixels.data(), pixels.size());
    auto commands = GPU->CreateCommandList();
    commands->CopyBufferToTexture(upload, 0, target, {});
    GPU->Submit(commands);
    ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(GPU->GetImGuiTextureID(target)),
                                             {0, 0},
                                             ImGui::GetIO().DisplaySize,
                                             {0, 1},
                                             {1, 0});
    auto capture = Finish();
    ASSERT_GT(capture.Width, 0);
    EXPECT_EQ(capture.At(capture.Width / 2, capture.Height / 4), (std::array<uint8_t, 3> {37, 91, 163}));
    EXPECT_EQ(capture.At(capture.Width / 2, capture.Height * 3 / 4), (std::array<uint8_t, 3> {51, 102, 153}));
}
INSTANTIATE_TEST_SUITE_P(Software,
                         WindowPresentation,
                         testing::Values(Backend::OpenGL, Backend::Vulkan),
                         [](const auto& info) { return info.param == Backend::OpenGL ? "OpenGL" : "Vulkan"; });
TEST_P(WindowPresentation, MinimizedSurfaceDefersCaptureUntilTheRestoredFrame)
{
    if (Window.Native)
        GTEST_SKIP() << "Zero framebuffer extent is deterministic only with VK_EXT_headless_surface; OS minimization "
                        "is compositor dependent.";
    Window.Spec.Width = Window.Spec.Height = 0;
    Start();
    RequestFrameCapture(Window, CapturePath.string());
    Finish(false);
    EXPECT_FALSE(std::filesystem::exists(CapturePath));
    Window.SetVSync(false);
    Window.Spec.Width  = 80;
    Window.Spec.Height = 60;
    Start();
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, {80, 60}, IM_COL32(39, 87, 151, 255));
    Finish(false);
    auto capture = ReadPpm(CapturePath);
    ASSERT_EQ(capture.Width, 80);
    ASSERT_EQ(capture.Height, 60);
    EXPECT_EQ(capture.At(40, 30), (std::array<uint8_t, 3> {39, 87, 151}));
}
TEST_P(WindowPresentation, HeadlessPlatformWindowsShareTextureIDsAcrossResizeAndPresentation)
{
    if (GetParam() != Backend::Vulkan || Window.Native)
        GTEST_SKIP() << "Headless platform-renderer WSI test requires VK_EXT_headless_surface.";
    // Drives exactly the renderer callbacks used by ImGui's GLFW platform path.
    // Catches per-window descriptor ownership, incompatible passes, stale views,
    // callback teardown, and main UI retirement before detached-window rendering.
    Start();
    auto          texture = GPU->CreateTexture({1,
                                                1,
                                                1,
                                                TextureDimension::Texture2D,
                                                Format::RGBA8Unorm,
                                                TextureUsage::Sampled | TextureUsage::CopyDestination});
    const uint8_t bytes[] = {37, 91, 163, 255};
    auto          buffer  = GPU->CreateBuffer({4, BufferUsage::CopySource, MemoryUsage::Upload});
    buffer->Write(0, bytes, 4);
    auto command = GPU->CreateCommandList();
    command->CopyBufferToTexture(buffer, 0, texture, {});
    GPU->Submit(command);
    ImGui::GetBackgroundDrawList()->AddImage(
        reinterpret_cast<ImTextureID>(GPU->GetImGuiTextureID(texture)), {0, 0}, {96, 72});
    std::weak_ptr<Texture> weak = texture;
    texture.reset();
    buffer.reset();
    command.reset();
    ImGui::Render();
    auto* mainData = ImGui::GetDrawData();
    UI->Render(mainData);
    auto& callbacks = ImGui::GetPlatformIO();
    ASSERT_NE(callbacks.Renderer_CreateWindow, nullptr);
    ImGuiViewport viewport;
    viewport.ID   = 0x12345678;
    viewport.Size = {96, 72};
    callbacks.Renderer_CreateWindow(&viewport);
    ImDrawData platformData    = *mainData;
    platformData.OwnerViewport = &viewport;
    viewport.DrawData          = &platformData;
    for (int i = 0; i < 4; ++i)
    {
        viewport.Size            = {96.0f + 8 * i, 72.0f + 4 * i};
        platformData.DisplaySize = viewport.Size;
        callbacks.Renderer_SetWindowSize(&viewport, viewport.Size);
        callbacks.Renderer_RenderWindow(&viewport, nullptr);
        callbacks.Renderer_SwapBuffers(&viewport, nullptr);
    }
    GPU->WaitIdle();
    EXPECT_FALSE(weak.expired());
    UI->EndFrame();
    Window.SwapBuffers();
    callbacks.Renderer_DestroyWindow(&viewport);
    GPU->WaitIdle();
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(viewport.RendererUserData, nullptr);
}
