#pragma once
#include "vulkan/vulkan.h"
#include "VulkanBase.h"
#include "VulkanDevice.h"
#include <GLFW/glfw3.h>
namespace Kans
{
	class VulkanSwapChain
	{
	public:
		VulkanSwapChain();
		void Init(VkInstance instance,const Ref<VulkanDevice>& device);
		void InitSurface(GLFWwindow* windows);
		void Cleanup();
	private:
		//
		VkSurfaceKHR m_Surface;
		Ref<VulkanDevice> m_Device;
		VkInstance m_Instance = VK_NULL_HANDLE;


	};
}