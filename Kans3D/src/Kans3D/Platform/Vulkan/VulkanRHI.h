#pragma once
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/RHI/RHI.h"
#include "Kans3D/Platform/Vulkan/VulkanDevice.h"
#include "Kans3D/Platform/Vulkan/VulkanSwapChain.h"
#include <vulkan/vulkan.h>

namespace Kans
{
	class VulkanRHI : public RHI
	{
	public:
		static VkInstance GetInstance() { return s_VulkanInstance; }
		static Ref<VulkanRHI> Get() { return std::dynamic_pointer_cast<VulkanRHI>(Renderer::GetRHI()); }
		static Ref<VulkanDevice> GetCurrentDevice() { return VulkanRHI::Get()->GetDevice(); }
		
		virtual void Init() override;
		virtual void Shutdown() override;
		virtual void CreateSwapChain() override;
		virtual void RecreateSwapchain() override;

		VulkanRHI();
		VulkanRHI(const Scope<Window>& window);
		~VulkanRHI();
		Ref<VulkanDevice> GetDevice() { return m_Device; }
		const Ref<VulkanDevice>& GetVulkanDevice() { return m_Device; }
	private:
		void CreateInstance();
		void SelectPhysicalDevice();
		void CreateLogicalDevice();
		
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateDescriptorPool();
	private:
		// Vulkan instance
		inline static VkInstance	s_VulkanInstance;
		// Devices
		Ref<VulkanPhysicalDevice>	m_PhysicalDevice;
		Ref<VulkanDevice>			m_Device;
		// debug Messenger
		VkDebugUtilsMessengerEXT	m_DebugUtilsMessenger = VK_NULL_HANDLE;
		// Vulkan swapchain
		Window*						m_WindowHandle;
		VulkanSwapChain				m_SwapChain;
		

		//CommandPool
		VkCommandPool m_GraphicCommandPool;
		VkCommandPool m_ComputeCommandPool;
		
		//DescriptorPoll
		VkDescriptorPool m_DescriptorPool;

		//Set of physical device features to be enabled
		VkPhysicalDeviceFeatures m_EnabledFeatures{};
		//Set of device extensions to be enabled
		std::vector<const char*> m_EnabledDeviceExtensions;
		//Set of Instance extensions to be enabled
		std::vector<const char*> m_EnabledInstanceExtensions;
		std::vector<std::string> m_SupportedInstanceExtensions;
	};
}