#pragma once
#include "RHI.h"
namespace Kans
{
    class Window;
    struct WindowSpecification;
} // namespace Kans
namespace Kans::RenderHardware
{
    enum class Backend
    {
        None,
        Vulkan
    };
    // Startup parsing is independent of native window/device initialization.
    Backend ResolveBackend(const std::string& configPath, const std::vector<std::string>& arguments = {});
    struct StartupFrameCapture
    {
        std::string Path;
        uint32_t    AfterFrames = 60;
    };
    StartupFrameCapture ResolveFrameCaptureArguments(const std::vector<std::string>& arguments);
    StartupFrameCapture GetStartupFrameCapture();
    void                SetStartupArguments(int argc, char** argv);
    Backend             ResolveStartupBackend(const std::string& configPath);
    void                BeginWindowFrame(Window& window);
    // PPM, top-left rows, next successfully presented main-window image.
    void    RequestFrameCapture(Window& window, const std::string& path);
    void    SetBackend(Backend backend);
    Backend GetBackend();
    // Creates the native surface using backend-owned API hints.
    void*                   CreateWindowSurface(Backend backend, const WindowSpecification& specification);
    std::shared_ptr<Device> InitializeDevice(Backend backend, Window* window = nullptr, const DeviceDesc& desc = {});
    // Installs a caller-created headless device (including test validation policy).
    void                    InitializeHeadlessDevice(Backend backend, std::shared_ptr<Device> device);
    std::shared_ptr<Device> GetSharedDevice();
    void                    ShutdownDevice();
    void                    PresentWindow(Window& window);
    void                    SetWindowVSync(Window& window, bool enabled);
} // namespace Kans::RenderHardware
