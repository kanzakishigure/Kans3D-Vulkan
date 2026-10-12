#include "kspch.h"
#include "Backend.h"
#include "Vulkan/VulkanDevice.h"
#include <stdexcept>
#include "OpenGL/OpenGLDevice.h"
namespace Kans::RenderHardware
{
    namespace
    {
        Backend                 Selected = Backend::None;
        std::shared_ptr<Device> Shared;
        Window*                 Surface = nullptr;
    } // namespace
    void SetBackend(Backend backend)
    {
        if (Shared && backend != Selected)
            throw std::logic_error("Cannot change a live device's backend");
        Selected = backend;
    }
    Backend GetBackend() { return Selected; }
    void*   CreateWindowSurface(Backend backend, const WindowSpecification& specification)
    {
        if (backend != Backend::OpenGL)
            throw std::invalid_argument("Window backend is not implemented");
        return CreateOpenGLWindow(specification);
    }
    std::shared_ptr<Device> InitializeDevice(Backend backend, Window* window, const DeviceDesc& desc)
    {
        if (Shared)
        {
            if (backend != Selected || window != Surface)
                throw std::logic_error("Another device is already initialized");
            return Shared;
        }
        std::shared_ptr<Device> device;
        if (backend == Backend::OpenGL)
            device = CreateOpenGLDevice(window, desc);
        else if (backend == Backend::Vulkan)
        {
            if (window)
                throw std::invalid_argument("Vulkan surfaces are not implemented until phase 4");
            device = CreateVulkanDevice(desc);
        }
        else
            throw std::invalid_argument("Device backend is not implemented");
        Selected    = backend;
        Surface     = window;
        Shared      = std::move(device);
        return Shared;
    }
    void InitializeHeadlessDevice(Backend backend, std::shared_ptr<Device> device)
    {
        if (!device || backend == Backend::None)
            throw std::invalid_argument("A headless device and backend are required");
        if (Shared)
            throw std::logic_error("A shared device is already initialized");
        Selected = backend;
        Surface  = nullptr;
        Shared   = std::move(device);
    }
    std::shared_ptr<Device> GetSharedDevice()
    {
        if (!Shared)
            throw std::logic_error("No shared rendering device has been initialized");
        return Shared;
    }
    void ShutdownDevice()
    {
        if (Shared)
            Shared->WaitIdle();
        Shared.reset();
        Surface = nullptr;
    }
    void PresentWindow(Window& window)
    {
        if (Selected == Backend::OpenGL)
            PresentOpenGLWindow(window);
    }
    void SetWindowVSync(Window& window, bool enabled)
    {
        if (Selected == Backend::OpenGL)
            SetOpenGLWindowVSync(window, enabled);
    }
} // namespace Kans::RenderHardware
