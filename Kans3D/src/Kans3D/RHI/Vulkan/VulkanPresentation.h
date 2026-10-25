#pragma once
#include "Kans3D/RHI/ImGuiRenderer.h"
#include "VulkanInternal.h"
namespace Kans::RenderHardware
{
    void*                          CreateVulkanWindow(const WindowSpecification& specification);
    std::unique_ptr<Device>        CreateVulkanWindowDevice(Window& window, const DeviceDesc& desc);
    std::unique_ptr<ImGuiRenderer> CreateVulkanImGuiRenderer(Window* window);
    void                           BeginVulkanWindowFrame(Device& device, Window& window);
    void                           PresentVulkanWindow(Device& device, Window& window);
    void                           SetVulkanWindowVSync(Device& device, Window& window, bool enabled);
    void                           RequestVulkanFrameCapture(Device& device, Window& window, const std::string& path);
    // Backend-private bridge. UI sampling uses the same inferred access tracker as RHI draws.
    const VulkanDetail::Context& GetVulkanContext(Device& device);
    void                         BeginVulkanDeviceWindowFrame(Device& device);
    void                         FinishVulkanDeviceWindowFrame(Device& device);
    Handle<Texture>              RenderVulkanImGuiOffscreen(Device& device, ImDrawData* data, VkRenderPass pass);
    Handle<Texture>              GetVulkanImGuiOffscreenTarget(ImGuiRenderer& renderer);
    void                         FlipVulkanImGuiRenderTargetUVs(Device& device, ImDrawData* data);
    void                         PrepareVulkanImGuiTextures(Device& device, ImDrawData* data);
} // namespace Kans::RenderHardware
