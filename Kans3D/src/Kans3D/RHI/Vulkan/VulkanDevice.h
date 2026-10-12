#pragma once
#include <functional>
#include <string>
#include "Kans3D/RHI/RHI.h"

namespace Kans::RenderHardware
{
    // Backend-private construction policy. Tests always request CPU-only selection,
    // validation and synchronization validation; applications use Debug validation.
    struct VulkanDeviceOptions
    {
        void*                                   NativeWindow              = nullptr;
        bool                                    HeadlessSurface           = false;
        bool                                    AsynchronousWindowFrames  = false;
        bool                                    RequireCpuDevice          = true;
        bool                                    Validation                = false;
        bool                                    SynchronizationValidation = false;
        std::function<void(const std::string&)> ValidationError;
    };
    std::unique_ptr<Device> CreateVulkanDevice(const DeviceDesc& desc = {});
    std::unique_ptr<Device> CreateVulkanDevice(const DeviceDesc& desc, const VulkanDeviceOptions& options);
} // namespace Kans::RenderHardware
