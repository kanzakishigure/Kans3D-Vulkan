#include "hzpch.h"
#include "VulkanSwapChain.h"

#define GLFW_INCLUDE_VULKAN


namespace Kans 
{

	VulkanSwapChain::VulkanSwapChain()
	{

	}

	void VulkanSwapChain::Init(VkInstance instance, const Ref<VulkanDevice>& device)
	{
		m_Instance = instance;
		m_Device = device;
	}

	void VulkanSwapChain::InitSurface(GLFWwindow* windows)
	{
		VK_CHECK_RESULT(glfwCreateWindowSurface(m_Instance, windows, nullptr, &m_Surface));
		
		auto device = m_Device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
		auto& queueFamilyIndices = m_Device->GetPhysicalDevice()->GetQueueFamilyIndices();
		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, queueFamilyIndices.graphicsFamily.value(), m_Surface, &presentSupport);
		if (presentSupport)
		{
			uint32_t i = queueFamilyIndices.graphicsFamily.value();
			queueFamilyIndices.presentFamily = i;
		}
	}

	void VulkanSwapChain::Cleanup()
	{
		if(m_Surface)
		vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
	}

}