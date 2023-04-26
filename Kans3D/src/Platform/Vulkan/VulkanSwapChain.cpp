#include "kspch.h"
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
		CORE_ASSERT(presentModeCount>0);
		std::vector<VkPresentModeKHR> presentModes(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, presentModes.data());
		VkPresentModeKHR presentMode =  VK_PRESENT_MODE_FIFO_KHR ;
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

		uint32_t imageCount = capabilities.minImageCount+1;
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
		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = m_Surface;
		createInfo.pNext = nullptr;

		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = m_ImageFormat;
		createInfo.imageColorSpace = m_ColorSpace;
		createInfo.imageExtent = swapchainExtent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = oldSwapchain;

		QueueFamilyIndices indices = m_Device->GetPhysicalDevice()->GetQueueFamilyIndices();
		uint32_t queueFamilyIndexCount = 0;
		uint32_t queueFamilyIndices[] = { indices.Graphics.value(), indices.Present.value() };
		if (indices.Present.value() != indices.Graphics.value())
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else 
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0; // Optional
			createInfo.pQueueFamilyIndices = nullptr; // Optional
		}

		VK_CHECK_RESULT(vkCreateSwapchainKHR(device, &createInfo, nullptr,&m_SwapChain));
		if (oldSwapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
		}


		//Retrieving swapchain images
		
		uint32_t imagecount = 0;
		vkGetSwapchainImagesKHR(device, m_SwapChain, &imagecount, nullptr);
		CORE_ASSERT(imagecount > 0);
		std::vector<VkImage> Images(imagecount);
		vkGetSwapchainImagesKHR(device, m_SwapChain, &imagecount, Images.data());

		//create ImageView
		m_SwapChainImages.resize(imageCount);
		for (size_t i = 0; i < imagecount; i++)
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

}