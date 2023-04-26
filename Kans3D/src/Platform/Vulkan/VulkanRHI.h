#pragma once
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/RHI/RHI.h"
#include "vulkan/vulkan.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanSwapChain.h"

namespace Kans
{
	class VulkanRHI : public RHI
	{
	public:
		virtual void Init() override;
		virtual void Shutdown() override;
		VulkanRHI();
		VulkanRHI(const Scope<Window>& window);
		~VulkanRHI();
		Ref<VulkanDevice> GetDevice() { return m_Device; }


		static VkInstance GetInstance() { return s_VulkanInstance; }

		static Ref<VulkanRHI> Get() { return std::dynamic_pointer_cast<VulkanRHI>(Renderer::GetRHI()); }
		static Ref<VulkanDevice> GetCurrentDevice() { return VulkanRHI::Get()->GetDevice();}
		const Ref<VulkanDevice>& GetVulkanDevice() { return m_Device; }
	private:
	private:
		// Vulkan instance
		inline static VkInstance s_VulkanInstance ;
		// Devices
		Ref<VulkanPhysicalDevice>	m_PhysicalDevice;
		Ref<VulkanDevice>			m_Device;
		// debug Messenger
		VkDebugUtilsMessengerEXT	m_DebugUtilsMessenger = VK_NULL_HANDLE;
		// Vulkan swapchain
		Window*						m_WindowHandle;
		VulkanSwapChain				m_SwapChain;
	};
}