#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <functional>
#include <stdexcept>
#include <thread>
#include "VulkanDevice.h"

namespace Kans::RenderHardware::VulkanDetail
{
    inline void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::invalid_argument(message);
    }
    inline void Check(VkResult result)
    {
        if (result != VK_SUCCESS)
            throw std::runtime_error("Vulkan operation failed: " + std::to_string(result));
    }
    template<class E>
    bool Has(E value, E flag)
    {
        return (uint32_t(value) & uint32_t(flag)) == uint32_t(flag);
    }
    inline void Range(size_t capacity, size_t offset, size_t bytes)
    {
        Require(offset <= capacity && bytes <= capacity - offset, "Buffer range exceeds allocation");
    }
    struct Context
    {
        VkInstance                 Instance  = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT   Messenger = VK_NULL_HANDLE;
        VkPhysicalDevice           Physical  = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties Properties {};
        VkDevice                   Logical     = VK_NULL_HANDLE;
        VkQueue                    Queue       = VK_NULL_HANDLE;
        uint32_t                   QueueFamily = 0;
        VmaAllocator               Allocator   = VK_NULL_HANDLE;
        VulkanDeviceOptions        Options;
        explicit Context(const VulkanDeviceOptions& options);
        ~Context();
        void Destroy();
    };
    // Kept backend-private so selection can be tested against synthetic properties
    // without enumerating or ever opening a hardware device.
    bool AcceptDeviceType(VkPhysicalDeviceType type, bool requireCpu);
    struct Access
    {
        VkPipelineStageFlags2 Stage  = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2        Mask   = VK_ACCESS_2_NONE;
        VkImageLayout         Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };
    struct FormatInfo
    {
        VkFormat           Native;
        uint32_t           Bytes;
        VkImageAspectFlags Aspects = VK_IMAGE_ASPECT_COLOR_BIT;
        bool               Depth() const { return (Aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0; }
        bool               Stencil() const { return (Aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0; }
    };
    FormatInfo Info(Format format);
} // namespace Kans::RenderHardware::VulkanDetail
