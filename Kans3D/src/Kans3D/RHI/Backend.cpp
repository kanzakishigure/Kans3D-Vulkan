#include "kspch.h"
#include "Backend.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanPresentation.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace Kans::RenderHardware
{
    namespace
    {
        std::vector<std::string> StartupArguments;
        Backend                 Selected = Backend::None;
        std::shared_ptr<Device> Shared;
        Window*                 Surface = nullptr;
    } // namespace
    Backend ResolveBackend(const std::string& configPath, const std::vector<std::string>& arguments)
    {
        auto parse = [](std::string value) {
            if (value == "Vulkan" || value == "vulkan")
                return Backend::Vulkan;
            throw std::invalid_argument("Invalid RenderBackend: " + value);
        };
        auto trim = [](std::string value) {
            auto first = value.find_first_not_of(" \t\r\n");
            return first == std::string::npos ? std::string {} :
                                                value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
        };
        Backend       result = Backend::Vulkan;
        std::ifstream input(configPath);
        std::string   line;
        bool          warnedLegacy = false;
        while (std::getline(input, line))
        {
            if (line.compare(0, 3, "\xef\xbb\xbf") == 0)
                line.erase(0, 3);
            auto split = line.find('=');
            if (split != std::string::npos && trim(line.substr(0, split)) == "RenderBackend")
            {
                const auto value = trim(line.substr(split + 1));
                if (value == "OpenGL" || value == "opengl")
                {
                    if (!warnedLegacy)
                    {
                        // Startup parsing precedes logger initialization.
                        std::cerr << "Warning: RenderBackend=OpenGL is no longer supported; using Vulkan.\n";
                        warnedLegacy = true;
                    }
                    result = Backend::Vulkan;
                }
                else
                    result = parse(value);
            }
        }
        bool seen = false;
        for (const auto& argument : arguments)
        {
            if (argument == "--render-backend")
                throw std::invalid_argument("Use --render-backend=vulkan");
            if (argument.rfind("--render-backend=", 0) == 0)
            {
                if (seen)
                    throw std::invalid_argument("Duplicate --render-backend option");
                result = parse(argument.substr(17));
                seen   = true;
            }
        }
        return result;
    }
    StartupFrameCapture ResolveFrameCaptureArguments(const std::vector<std::string>& arguments)
    {
        StartupFrameCapture result;
        bool                pathSeen = false, countSeen = false;
        for (const auto& argument : arguments)
        {
            if (argument.rfind("--frame-capture=", 0) == 0)
            {
                if (pathSeen)
                    throw std::invalid_argument("Duplicate --frame-capture option");
                result.Path = argument.substr(16);
                if (result.Path.empty())
                    throw std::invalid_argument("Empty --frame-capture path");
                pathSeen = true;
            }
            if (argument.rfind("--frame-capture-after=", 0) == 0)
            {
                if (countSeen)
                    throw std::invalid_argument("Duplicate --frame-capture-after option");
                auto count  = argument.substr(22);
                auto parsed = std::from_chars(count.data(), count.data() + count.size(), result.AfterFrames);
                if (parsed.ec != std::errc {} || parsed.ptr != count.data() + count.size() || !result.AfterFrames)
                    throw std::invalid_argument("--frame-capture-after needs a positive frame count");
                countSeen = true;
            }
        }
        if (countSeen && !pathSeen)
            throw std::invalid_argument("--frame-capture-after requires --frame-capture");
        return result;
    }
    StartupFrameCapture GetStartupFrameCapture() { return ResolveFrameCaptureArguments(StartupArguments); }
    void                SetStartupArguments(int argc, char** argv)
    {
        StartupArguments.clear();
        for (int i = 1; i < argc; ++i)
            StartupArguments.emplace_back(argv[i]);
    }
    Backend ResolveStartupBackend(const std::string& configPath)
    {
        return ResolveBackend(configPath, StartupArguments);
    }
    void BeginWindowFrame(Window& window)
    {
        if (Selected == Backend::Vulkan)
            BeginVulkanWindowFrame(*GetSharedDevice(), window);
    }
    void RequestFrameCapture(Window& window, const std::string& path)
    {
        if (path.empty())
            throw std::invalid_argument("Frame capture path is empty");
        if (Selected == Backend::Vulkan)
            RequestVulkanFrameCapture(*GetSharedDevice(), window, path);
        else
            throw std::logic_error("No presentation backend");
    }
    void SetBackend(Backend backend)
    {
        if (Shared && backend != Selected)
            throw std::logic_error("Cannot change a live device's backend");
        Selected = backend;
    }
    Backend GetBackend() { return Selected; }
    void*   CreateWindowSurface(Backend backend, const WindowSpecification& specification)
    {
        if (backend == Backend::Vulkan)
            return CreateVulkanWindow(specification);
        throw std::invalid_argument("Window backend is not implemented");
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
        if (backend == Backend::Vulkan)
        {
            device = window ? CreateVulkanWindowDevice(*window, desc) : CreateVulkanDevice(desc);
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
        if (Selected == Backend::Vulkan)
            PresentVulkanWindow(*GetSharedDevice(), window);
    }
    void SetWindowVSync(Window& window, bool enabled)
    {
        if (Selected == Backend::Vulkan)
            SetVulkanWindowVSync(*GetSharedDevice(), window, enabled);
    }
} // namespace Kans::RenderHardware
