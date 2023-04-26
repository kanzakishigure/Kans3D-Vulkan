#pragma once
#include <vulkan/vulkan.h>
#include "Kans3D/Core/Base/Base.h"

namespace Kans
{
	struct  QueueFamilyIndices;
}
namespace Kans::Utils
{
	//ValidationLayer
	bool checkValidationLayerSupport(std::vector<const char*> validationLayers);

	//Device
	QueueFamilyIndices findGraphicQueueFamilies(VkPhysicalDevice physicaldevice);
	bool isDeviceSuitable(VkPhysicalDevice device);
	
	//Utils
	const std::string VulkanMessageTypeToString(VkDebugUtilsMessageTypeFlagsEXT vkType);
	const std::string VulkanMessageSeverityToString(VkDebugUtilsMessageSeverityFlagBitsEXT vkSeverity);
	const std::string VulkanResualtToString(VkResult res);
	inline void CheckVulkanResualt(VkResult res) 
	{
		if (res != VK_SUCCESS)
		{
			CORE_ERROR(VulkanResualtToString(res));
			CORE_ASSERT(false);
		}
	}
}
#define VK_CHECK_RESULT(f)\
{\
	VkResult res =(f);\
	::Kans::Utils::CheckVulkanResualt(res);\
}


