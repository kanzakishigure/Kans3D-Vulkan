#pragma once
#include "VulkanBase.h"
#include "VulkanDevice.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
namespace Kans
{
	struct SwapChainImage
	{
		VkImage Image;
		VkImageView ImageView;
	};

	class VulkanSwapChain
	{
	public:
		VulkanSwapChain();
		void Connect(VkInstance instance,const Ref<VulkanDevice>& device);
		void InitSurface(GLFWwindow* windows);
		void Create(uint32_t* width, uint32_t* height, bool vsync);

		void Present();
		void Cleanup();
	private:
		void ChooseSurfaceFormatAndColorSpace();


	private:
		//swapchain
		Ref<VulkanDevice>	m_Device = nullptr;
		VkInstance			m_Instance = VK_NULL_HANDLE;
		VkSwapchainKHR		m_SwapChain = VK_NULL_HANDLE;
		uint32_t			m_Width;
		uint32_t			m_Height;
		//surface 
		VkSurfaceKHR		m_Surface = VK_NULL_HANDLE;
		VkFormat            m_ImageFormat = VK_FORMAT_MAX_ENUM;
		VkColorSpaceKHR		m_ColorSpace = VK_COLOR_SPACE_MAX_ENUM_KHR;
		//ImageViews
		std::vector<SwapChainImage> m_SwapChainImages;
		bool m_VSync = false;
		// Function pointers
		PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
		PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
		PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
		PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
		PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
		PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
		PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
		PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
		PFN_vkQueuePresentKHR fpQueuePresentKHR;

	};
}