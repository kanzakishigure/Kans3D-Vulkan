#pragma once
#include "Kans3D/Renderer/RenderContext.h"
#include "vulkan/vulkan.h"
#include "Platform/Vulkan/VulkanDevice.h"
namespace Kans
{
	class VulkanContext : public RenderContext
	{
	public:
		virtual void Init() override;
		virtual void SwapBuffers() override;
		VulkanContext();
		~VulkanContext();
		static VkInstance GetInstance() { return s_Instance; }
		const Ref<VulkanDevice>& GetVulkanDevice() { return m_Device; }
	private:
		// Vulkan instance
		inline static VkInstance s_Instance ;
		// Devices
		Ref<VulkanPhysicalDevice> m_PhysicalDevice;
		Ref<VulkanDevice> m_Device;
		//debug Messenger
		VkDebugUtilsMessengerEXT m_DebugUtilsMessenger = VK_NULL_HANDLE;
	};
}