#pragma once
#include <vulkan/vulkan.h>
namespace Kans
{
    class VulkanPipline
    {
    public:
        VulkanPipline();
        ~VulkanPipline() = default;

        void Create();
        void Reset();

    private:
    };
} // namespace Kans