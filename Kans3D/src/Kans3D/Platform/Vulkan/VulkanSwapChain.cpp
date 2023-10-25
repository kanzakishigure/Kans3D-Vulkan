#include "kspch.h"
#include "VulkanSwapChain.h"

#define GLFW_INCLUDE_VULKAN


namespace Kans
{

	VulkanSwapChain::VulkanSwapChain()
	{

	}

	void VulkanSwapChain::Init(VkInstance instance, const Ref<VulkanDevice>& device, GLFWwindow* Window)
	{
		m_Instance = instance;
		m_Device = device;

		fpGetPhysicalDeviceSurfaceSupportKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(vkGetInstanceProcAddr(m_Instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
		fpGetPhysicalDeviceSurfaceCapabilitiesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(vkGetInstanceProcAddr(m_Instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
		fpGetPhysicalDeviceSurfaceFormatsKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(vkGetInstanceProcAddr(m_Instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
		fpGetPhysicalDeviceSurfacePresentModesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(vkGetInstanceProcAddr(m_Instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));

		fpCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(vkGetDeviceProcAddr(m_Device->GetVulkanDevice(), "vkCreateSwapchainKHR"));
		fpDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(vkGetDeviceProcAddr(m_Device->GetVulkanDevice(), "vkDestroySwapchainKHR"));
		fpGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(vkGetDeviceProcAddr(m_Device->GetVulkanDevice(), "vkGetSwapchainImagesKHR"));
		fpAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(vkGetDeviceProcAddr(m_Device->GetVulkanDevice(), "vkAcquireNextImageKHR"));
		fpQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(vkGetDeviceProcAddr(m_Device->GetVulkanDevice(), "vkQueuePresentKHR"));

		CreateWindowSurface(Window);
		CreateSyncPrimitives();
		CreateCommandPoolAndCommandBuffers();
	}

	void VulkanSwapChain::CreateWindowSurface(GLFWwindow* windows)
	{
		VK_CHECK_RESULT(glfwCreateWindowSurface(m_Instance, windows, nullptr, &m_Surface));

		VkPhysicalDevice physicalDevice = m_Device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
		QueueFamilyIndices& queueFamilyIndices = m_Device->GetPhysicalDevice()->GetQueueFamilyIndices();


		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndices.Graphics.value(), m_Surface, &presentSupport);

		if (presentSupport)
		{
			uint32_t i = queueFamilyIndices.Graphics.value();
			queueFamilyIndices.Present = i;
		}
		else
		{
			CORE_INFO_TAG("renderer", "GraphicsQueueFamily don't support present,find seperate queue");
			// Get available queue family properties
			uint32_t queueCount;
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, NULL);
			CORE_ASSERT(queueCount >= 1);
			std::vector<VkQueueFamilyProperties> queueProps(queueCount);
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProps.data());
			for (size_t i = 0; i < queueCount; i++)
			{
				vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, m_Surface, &presentSupport);
				if (presentSupport)
				{
					queueFamilyIndices.Present = i;
					break;
				}
			}
		}

		 
		CORE_ASSERT(queueFamilyIndices.Graphics.has_value());
		CORE_ASSERT(queueFamilyIndices.Present.has_value());
		m_QueueNodeIndex = queueFamilyIndices.Graphics.value();
		ChooseSurfaceFormatAndColorSpace();
	}

	void VulkanSwapChain::Create(uint32_t* width, uint32_t* height, bool vsync)
	{
		m_VSync = vsync;
		VkSwapchainKHR oldSwapchain = m_SwapChain;
		VkPhysicalDevice physicalDevice = m_Device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
		VkDevice device = m_Device->GetVulkanDevice();

		//choose the presentMode
		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, nullptr);
		CORE_ASSERT(presentModeCount > 0);
		std::vector<VkPresentModeKHR> presentModes(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, presentModes.data());
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		if (!m_VSync)
		{
			for (VkPresentModeKHR& mode : presentModes)
			{
				if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
					break;
				}
			}
		}

		//check the capabilities
		VkSurfaceCapabilitiesKHR capabilities;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_Surface, &capabilities);

		uint32_t imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		{
			imageCount = capabilities.maxImageCount;
		}

		VkExtent2D swapchainExtent{ (uint32_t)-1 ,(uint32_t)-1 };
		if (capabilities.currentExtent.width == (uint32_t)-1)
		{
			swapchainExtent.width = *width;
			swapchainExtent.height = *height;
		}
		else
		{
			swapchainExtent.width = capabilities.currentExtent.width;
			swapchainExtent.height = capabilities.currentExtent.height;
			*width = capabilities.currentExtent.width;
			*height = capabilities.currentExtent.height;
		}
		m_Width = swapchainExtent.width;
		m_Height = swapchainExtent.height;


		//create swapchain
		VkSwapchainCreateInfoKHR swapchaincreateInfo{};
		swapchaincreateInfo.sType				= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapchaincreateInfo.surface				= m_Surface;
		swapchaincreateInfo.pNext				= nullptr;

		swapchaincreateInfo.minImageCount		= imageCount;
		swapchaincreateInfo.imageFormat			= m_ImageFormat;
		swapchaincreateInfo.imageColorSpace		= m_ColorSpace;
		swapchaincreateInfo.imageExtent			= swapchainExtent;
		swapchaincreateInfo.imageArrayLayers	= 1;
		swapchaincreateInfo.imageUsage			= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		swapchaincreateInfo.preTransform		= capabilities.currentTransform;
		swapchaincreateInfo.compositeAlpha		= VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapchaincreateInfo.presentMode			= presentMode;
		swapchaincreateInfo.clipped				= VK_TRUE;
		swapchaincreateInfo.oldSwapchain		= oldSwapchain;

		QueueFamilyIndices indices = m_Device->GetPhysicalDevice()->GetQueueFamilyIndices();
		uint32_t queueFamilyIndexCount	= 0;
		uint32_t queueFamilyIndices[]	= { indices.Graphics.value(), indices.Present.value() };
		if (indices.Present.value() != indices.Graphics.value())
		{
			swapchaincreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			swapchaincreateInfo.queueFamilyIndexCount = 2;
			swapchaincreateInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else
		{
			swapchaincreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			swapchaincreateInfo.queueFamilyIndexCount = 0; // Optional
			swapchaincreateInfo.pQueueFamilyIndices = nullptr; // Optional
		}

		VK_CHECK_RESULT(vkCreateSwapchainKHR(device, &swapchaincreateInfo, nullptr, &m_SwapChain));
		
		CreateSwapchainImageViews();
		
	}

	

	void VulkanSwapChain::ReCreate(uint32_t width, uint32_t height)
	{
		vkDeviceWaitIdle(m_Device->GetVulkanDevice());
		
		//Cleanup swapchain
		{
			for (auto& image : m_SwapChainImages)
				vkDestroyImageView(m_Device->GetVulkanDevice(), image.ImageView, nullptr);
			m_SwapChainImages.clear();
			
			//clean the framebuffer for swapchain image

			if (m_SwapChain != VK_NULL_HANDLE)
			{
				vkDestroySwapchainKHR(m_Device->GetVulkanDevice(), m_SwapChain, nullptr);
			}
			

		}

		
		Create(&width, &height, m_VSync);
		vkDeviceWaitIdle(m_Device->GetVulkanDevice());
		
	}

	void VulkanSwapChain::Present()
	{

	}

	void VulkanSwapChain::Cleanup()
	{
		VkDevice deviece = m_Device->GetVulkanDevice();


		for (auto imageView : m_SwapChainImages)
		{
			vkDestroyImageView(deviece, imageView.ImageView, nullptr);
		}
		
		if (m_SwapChain)
		{
			vkDestroySwapchainKHR(deviece, m_SwapChain, nullptr);
		}

	}

	void VulkanSwapChain::ChooseSurfaceFormatAndColorSpace()
	{

		VkPhysicalDevice physicalDevice = m_Device->GetPhysicalDevice()->GetVulkanPhysicalDevice();

		//choose the Format and colorSpace
		//we prefer the R8G8B8A8 and COLOR_SPACE_SRGB_NONLINEAR
		//most monitor support the srgb see more information
		//https://stackoverflow.com/questions/12524623/what-are-the-practical-differences-when-working-with-colors-in-a-linear-vs-a-no

		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, nullptr);
		CORE_ASSERT(formatCount > 0);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, formats.data());
		//we use the B8G8R8A8_UNORM£¬so we need add gamma correction in fragment shader 
		for (auto& format : formats)
		{
			if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				if (format.format == VK_FORMAT_B8G8R8A8_UNORM)
				{
					m_ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
					m_ImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
					break;
				}
			}
		}
		if (m_ImageFormat == VK_FORMAT_MAX_ENUM || m_ColorSpace == VK_COLOR_SPACE_MAX_ENUM_KHR)
		{
			m_ColorSpace = formats[0].colorSpace;
			m_ImageFormat = formats[0].format;
		}

	}

	void VulkanSwapChain::CreateSwapchainImageViews()
	{
		auto device = m_Device->GetVulkanDevice();
		//Retrieving swapchain images
		m_ImageCount = 0;
		VK_CHECK_RESULT(vkGetSwapchainImagesKHR(device, m_SwapChain, &m_ImageCount, nullptr));
		CORE_ASSERT(m_ImageCount > 0);
		std::vector<VkImage> Images(m_ImageCount);
		VK_CHECK_RESULT(vkGetSwapchainImagesKHR(device, m_SwapChain, &m_ImageCount, Images.data()));

		//create ImageView
		m_SwapChainImages.resize(m_ImageCount);
		for (size_t i = 0; i < m_ImageCount; i++)
		{
			VkImageViewCreateInfo imageViewCreateInfo{};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.pNext = nullptr;
			imageViewCreateInfo.image = Images[i];
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCreateInfo.format = m_ImageFormat;

			//we can swizzle the color channel
			imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			//specify the imageView usage
			imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imageViewCreateInfo.subresourceRange.levelCount = 1;
			imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
			imageViewCreateInfo.subresourceRange.layerCount = 1;


			VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_SwapChainImages[i].ImageView));
			m_SwapChainImages[i].Image = Images[i];
		}
	}

	void VulkanSwapChain::CreateSyncPrimitives()
	{
		//Set the semaphore object
		{
			m_SwapChainSyncPrimitives.resize(Max_frames_in_flight);


			VkSemaphoreCreateInfo semaphore_create_info{};
			semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VkFenceCreateInfo fence_create_info{};
			fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // the fence is initialized as signaled

			for (auto& syncPrimitive : m_SwapChainSyncPrimitives)
			{
				VK_CHECK_RESULT(vkCreateSemaphore(m_Device->GetVulkanDevice(), &semaphore_create_info, nullptr, &syncPrimitive.RenderComplete));
				VK_CHECK_RESULT(vkCreateSemaphore(m_Device->GetVulkanDevice(), &semaphore_create_info, nullptr, &syncPrimitive.PresentComplete));
				VK_CHECK_RESULT(vkCreateSemaphore(m_Device->GetVulkanDevice(), &semaphore_create_info, nullptr, &syncPrimitive.AvailableForTextrureCopy));

				VK_CHECK_RESULT(vkCreateFence(m_Device->GetVulkanDevice(), &fence_create_info, nullptr, &syncPrimitive.FrameInFlight));
			}
		}
	}

	void VulkanSwapChain::CreateCommandPoolAndCommandBuffers()
	{
		//Create commandBufferPool and commandBuffer for swapChain
		auto device = m_Device->GetVulkanDevice();
		
		for (auto& commandBuffer : m_CommandBuffers)
			vkDestroyCommandPool(device, commandBuffer.CommandPool, nullptr);
		m_CommandBuffers.resize(Max_frames_in_flight);

		VkCommandPoolCreateInfo cmdPoolInfo{};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = m_QueueNodeIndex;
		cmdPoolInfo.pNext = NULL;
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
		commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandBufferAllocateInfo.commandBufferCount = 1;

		for (auto& commandBuffer : m_CommandBuffers)
		{
			VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandBuffer.CommandPool));
			commandBufferAllocateInfo.commandPool = commandBuffer.CommandPool;
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer.CommandBuffer));
		}
			
	}


}