#include "kspch.h"
#include "VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"

#ifdef KS_DEBUG
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
////////////////////////////////////////////////////////////////////////////////////
//  Vulkan Physical Device
////////////////////////////////////////////////////////////////////////////////////
	VulkanPhysicalDevice::VulkanPhysicalDevice()
	{
		uint32_t gpuCount = 0;
		VkInstance s_instance = VulkanRHI::GetInstance();

		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		vkEnumeratePhysicalDevices(s_instance, &gpuCount, nullptr);

		CORE_ASSERT(gpuCount > 0, "failed to find GPUs with Vulkan support!");

		std::vector<VkPhysicalDevice> devices(gpuCount);
		vkEnumeratePhysicalDevices(s_instance, &gpuCount, devices.data());
		

		std::vector<std::pair<VkPhysicalDevice,int>> rankDevices;
		for (VkPhysicalDevice& device : devices)
		{
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(device, &properties);
			std::pair<VkPhysicalDevice, int> rankedDevice(device,0);
			rankedDevice.first = device;
			if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				rankedDevice.second += 1000;
			}
			if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
			{
				rankedDevice.second += 100;
			}
			
			rankDevices.push_back(rankedDevice);
		}
		auto sortfunc = [](std::pair<VkPhysicalDevice, int>p1, std::pair<VkPhysicalDevice, int>p2)
		{
			return p1.second > p2.second;
		};
		std::sort(rankDevices.begin(), rankDevices.end(), sortfunc);

		CORE_ASSERT(rankDevices.size() > 0);
		PhysicalDevice = rankDevices[0].first;
		

		CORE_ASSERT(PhysicalDevice != VK_NULL_HANDLE, "Can't find physical device");
		
		//we will check the support extensions later
		m_PhysicalDevice = PhysicalDevice;
		vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &m_Features);
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_Properties);

		//Get the QueueFamilyProperties
		uint32_t queuefamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queuefamilyCount, nullptr);
		CORE_ASSERT(queuefamilyCount > 0, "vulkan QueueFamily don't find");
		m_QueueFamilyProperties.resize(queuefamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queuefamilyCount, m_QueueFamilyProperties.data());

		//Get the support extension of current PhysicalDevice
		uint32_t count = 0;
		vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &count, nullptr);
		if (count > 0)
		{
			std::vector<VkExtensionProperties>availableExtensions(count);
			vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &count, availableExtensions.data());
			for (const auto& extension : availableExtensions)
			{
				m_SupportedExtensions.emplace(extension.extensionName);
			}
		}

		CORE_ASSERT(IsExtensionSupported(VK_KHR_SWAPCHAIN_EXTENSION_NAME),"PhysicalDevice don't support VK_KHR_SWAPCHAIN_EXTENSION");


		//Get the QueueFamilyIndices
		int requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
		m_QueueFamilyIndices = FindQueueFamilies(requestedQueueTypes);
		CORE_ASSERT(m_QueueFamilyIndices.isComplete(requestedQueueTypes),"queueFamilyIndices doesn't support all request");

		//push the device createInfo
		std::vector<uint32_t> queue_families;
		float queue_priority = 1.0f;
		
		if (VK_QUEUE_GRAPHICS_BIT & requestedQueueTypes)
		{
			VkDeviceQueueCreateInfo queue_create_info{};
			queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_create_info.queueFamilyIndex = m_QueueFamilyIndices.Graphics.value();
			queue_create_info.queueCount = 1;
			queue_create_info.pQueuePriorities = &queue_priority;
			m_QueueCreateInfos.push_back(queue_create_info);
			
		}
		if (VK_QUEUE_COMPUTE_BIT & requestedQueueTypes)
		{			
			if(m_QueueFamilyIndices.Compute.value() != m_QueueFamilyIndices.Graphics.value())
			{
				// queue create info
				VkDeviceQueueCreateInfo queue_create_info{};
				queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queue_create_info.queueFamilyIndex = m_QueueFamilyIndices.Compute.value();
				queue_create_info.queueCount = 1;
				queue_create_info.pQueuePriorities = &queue_priority;
				m_QueueCreateInfos.push_back(queue_create_info);
			}
		}


	}

	VulkanPhysicalDevice::~VulkanPhysicalDevice()
	{

	}

	bool VulkanPhysicalDevice::IsExtensionSupported(const std::string& extensionName) const
	{
		return m_SupportedExtensions.find(extensionName) != m_SupportedExtensions.end();
	}

	Kans::QueueFamilyIndices VulkanPhysicalDevice::FindQueueFamilies(int flags)
	{
		QueueFamilyIndices indices;
		uint32_t i = 0;
		for (const VkQueueFamilyProperties& queuefamily : m_QueueFamilyProperties)
		{
			if (VK_QUEUE_GRAPHICS_BIT & flags)
			{
				if (queuefamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					indices.Graphics = i;
				}
			}
			
			if (VK_QUEUE_COMPUTE_BIT & flags)
			{
				if (queuefamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
				{
					indices.Compute = i;
				}
			}

			if (indices.isComplete(flags))
			{
				break;
			}
			i++;
		}
		return indices;
	}

	Ref<VulkanPhysicalDevice> VulkanPhysicalDevice::Select()
	{
		return CreateRef<VulkanPhysicalDevice>();
	}
////////////////////////////////////////////////////////////////////////////////////
//  Vulkan  Device
////////////////////////////////////////////////////////////////////////////////////
	VulkanDevice::VulkanDevice(const Ref<VulkanPhysicalDevice>& physicalDevice, VkPhysicalDeviceFeatures enabledFeatures)
		:m_PhysicalDevice(physicalDevice),m_EnabledFeatures(enabledFeatures)
	{
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Vulkan DeviceQueue createInfo
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		const QueueFamilyIndices& indices = m_PhysicalDevice->GetQueueFamilyIndices();
		
		std::vector<VkDeviceQueueCreateInfo> queue_create_infos = physicalDevice->m_QueueCreateInfos; // all queues that need to be created


		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//  Vulkan Device createInfo
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		std::vector<const char*> deviceExtensions;
		if (physicalDevice->IsExtensionSupported(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		
		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = queue_create_infos.data();
		createInfo.queueCreateInfoCount = queue_create_infos.size();
		createInfo.pEnabledFeatures = &m_EnabledFeatures;
		createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();

		if (s_Validation)
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();			
		}
		else 
		{
			createInfo.enabledLayerCount = 0;
		}

		VK_CHECK_RESULT(vkCreateDevice(physicalDevice->GetVulkanPhysicalDevice(), &createInfo, nullptr, &m_LogicalDevice));

		vkGetDeviceQueue(m_LogicalDevice, indices.Graphics.value(), 0, &m_GraphicsQueue);
		vkGetDeviceQueue(m_LogicalDevice, indices.Compute.value(), 0, &m_ComputeQueue);
		
		//TODO: in fact presentqueue should handle by the swapchain ,so may not ready in vkcreatedevice
		//vkGetDeviceQueue(m_Device, indices.Present.value(), 0, &m_PresentQueue);
	}

	VulkanDevice::~VulkanDevice()
	{
		
	}

	void VulkanDevice::Destroy()
	{
		vkDestroyDevice(m_LogicalDevice, nullptr);
	}

}