#pragma once
#include "Kans3D/RHI/RHI.h"
namespace Kans
{
    class Window;
    struct WindowSpecification;
} // namespace Kans
namespace Kans::RenderHardware
{
    // Headless tests provide a current, loaded context. A window factory binds and loads its context.
    std::unique_ptr<Device> CreateOpenGLDevice(const DeviceDesc& desc = {});
    std::unique_ptr<Device> CreateOpenGLDevice(Window* window, const DeviceDesc& desc = {});
    void*                   CreateOpenGLWindow(const WindowSpecification& specification);
    void                    PresentOpenGLWindow(Window& window);
    void                    SetOpenGLWindowVSync(Window& window, bool enabled);
    using OpenGLLoadProc = void* (*)(const char*);
    void LoadOpenGLEntryPoints(OpenGLLoadProc load);
} // namespace Kans::RenderHardware
