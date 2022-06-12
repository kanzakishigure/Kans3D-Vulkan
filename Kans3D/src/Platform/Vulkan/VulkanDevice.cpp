#include "hzpch.h"
#include "VulkanDevice.h"
#include "Platform/Vulkan/VulkanContext.h"

#ifdef HZ_DEBUG
static bool s_Validation = true;
#else
static bool s_Validation = false; // Let's leave this on for now...
#endif
static std::vector<const char*> validationLayers =
{
	"VK_LAYER_KHRONOS_validation"
};
namespace Kans
{
	
	VulkanPhysicalDevice::VulkanPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		auto s_instance = VulkanContext::GetInstance();

		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		vkEnumeratePhysicalDevices(s_instance, &deviceCount, nullptr);

		HZ_CORE_ASSERT(deviceCount > 0, "failed to find GPUs with Vulkan support!");

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(s_instance, &deviceCount, devices.data());

		for (const auto& device : devices) 
		{
			if (Utils::isDeviceSuitable(device)) 
			{
				PhysicalDevice = device;
				break;
				
			}
		}
		HZ_CORE_ASSERT(PhysicalDevice != VK_NULL_HANDLE, "failed to find a suitable GPU!");
		m_QueueFamilyIndices = Utils::findGraphicQueueFamilies(PhysicalDevice);
		m_PhysicalDevice = PhysicalDevice;

	}

	VulkanPhysicalDevice::~VulkanPhysicalDevice()
	{

	}

	Ref<VulkanPhysicalDevice> VulkanPhysicalDevice::Select()
	{
		return CreateRef<VulkanPhysicalDevice>();
	}

	VulkanDevice::VulkanDevice(const Ref<VulkanPhysicalDevice>& physicalDevice)
		:m_PhysicalDevice(physicalDevice)
	{
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Vulkan DeviceQueue createInfo
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		const QueueFamilyIndices& indices = m_PhysicalDevice->GetQueueFamilyIndices();
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
		//一个队列已经足够了，我们可以在不同的线程上创建指令缓冲区，然后再主线程上一次提交
		queueCreateInfo.queueCount = 1;
		//队列优先级
		float queuePriority = 1.0f;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//  Specifying Vulkan DeviceFeatures createInfo
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		VkPhysicalDeviceFeatures deviceFeatures{};
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//  Create Vulkan Device createInfo
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.queueCreateInfoCount = 1;

		createInfo.pEnabledFeatures = &deviceFeatures;

		createInfo.enabledExtensionCount = 0;
		if (s_Validation)
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();			
		}
		else 
		{
			createInfo.enabledLayerCount = 0;
		}

		VK_CHECK_RESULT(vkCreateDevice(physicalDevice->GetVulkanPhysicalDevice(), &createInfo, nullptr, &m_Device));

		vkGetDeviceQueue(m_Device, indices.graphicsFamily.value(), 0, &m_GraphicsQueue);
	}

	VulkanDevice::~VulkanDevice()
	{
		vkDestroyDevice(m_Device, nullptr);
	}

}