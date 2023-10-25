#pragma once
#include "VulkanBase.h"
#include "VulkanDevice.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
namespace Kans
{
	

	class VulkanSwapChain
	{
		
	public:
		VulkanSwapChain();
		void Init(VkInstance instance,const Ref<VulkanDevice>& device, GLFWwindow* windows);
		void Create(uint32_t* width, uint32_t* height, bool vsync);
		void ReCreate(uint32_t width, uint32_t height);
		void Present();
		void Cleanup();
	private:
		void CreateWindowSurface(GLFWwindow* windows);
		void CreateSyncPrimitives();
		void CreateCommandPoolAndCommandBuffers();
		
		void CreateSwapchainImageViews();
		void ChooseSurfaceFormatAndColorSpace();
	private:

		struct SwapChainImage
		{
			VkImage Image = nullptr;
			VkImageView ImageView = nullptr;
		};

		struct SwapchainCommandBuffer
		{
			VkCommandPool CommandPool = nullptr;
			VkCommandBuffer CommandBuffer = nullptr;
		};
		std::vector<SwapchainCommandBuffer> m_CommandBuffers;

		struct SwapChainSyncPrimitive
		{
			VkSemaphore PresentComplete = nullptr;
			VkSemaphore RenderComplete = nullptr;
			VkSemaphore AvailableForTextrureCopy = nullptr;
			VkFence FrameInFlight = nullptr;
		};
		std::vector<SwapChainSyncPrimitive> m_SwapChainSyncPrimitives;

		const uint32_t Max_frames_in_flight = { 3 };
		//swapchain
		Ref<VulkanDevice>	m_Device = nullptr;
		VkInstance			m_Instance = VK_NULL_HANDLE;
		VkSwapchainKHR		m_SwapChain = VK_NULL_HANDLE;
		uint32_t			m_Width;
		uint32_t			m_Height;

		//use for CreateCommandBufferPool
		uint32_t			m_QueueNodeIndex = UINT32_MAX;
		//surface 
		VkSurfaceKHR		m_Surface = VK_NULL_HANDLE;
		VkFormat            m_ImageFormat = VK_FORMAT_MAX_ENUM;
		uint32_t			m_ImageCount = 0;
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

		friend class VulkanRHI;
	};
}