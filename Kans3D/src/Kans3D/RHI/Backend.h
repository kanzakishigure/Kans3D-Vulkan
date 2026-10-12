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
        OpenGL,
        Vulkan
    };
    void    SetBackend(Backend backend);
    Backend GetBackend();
    // Creates the native surface/context using backend-owned API hints.
    void*                   CreateWindowSurface(Backend backend, const WindowSpecification& specification);
    std::shared_ptr<Device> InitializeDevice(Backend backend, Window* window = nullptr, const DeviceDesc& desc = {});
    // Installs a caller-created headless device (including test validation policy).
    void                    InitializeHeadlessDevice(Backend backend, std::shared_ptr<Device> device);
    std::shared_ptr<Device> GetSharedDevice();
    void                    ShutdownDevice();
    void                    PresentWindow(Window& window);
    void                    SetWindowVSync(Window& window, bool enabled);
} // namespace Kans::RenderHardware
