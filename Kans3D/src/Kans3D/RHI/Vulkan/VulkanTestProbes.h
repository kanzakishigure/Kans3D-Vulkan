#pragma once
#include <vulkan/vulkan.h>
#include "Kans3D/RHI/RHI.h"
namespace Kans::RenderHardware::VulkanTestProbes
{
    struct Diagnostics
    {
        VkPhysicalDeviceType Type;
        uint32_t             ApiVersion;
        bool                 Validation, SynchronizationValidation;
        uint64_t             PipelineCreations, DescriptorAllocations, DescriptorPools, GroupCreationNanoseconds;
    };
    Diagnostics Inspect(const Device& device);
    void        EmitValidationError(const Device& device, const char* message);
} // namespace Kans::RenderHardware::VulkanTestProbes
