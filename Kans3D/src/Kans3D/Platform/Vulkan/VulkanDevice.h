#pragma once
#include <vulkan/vulkan.h>
#include "VulkanBase.h"
namespace Kans
{
	struct QueueFamilyIndices
	{
		std::optional<uint32_t> Graphics;
		std::optional<uint32_t> Present;
		std::optional<uint32_t> Compute;
		bool isComplete(int flags) 
		{
			bool iscomplete = true;
			if (VK_QUEUE_COMPUTE_BIT & flags)
			{
				iscomplete = iscomplete && Compute.has_value();
			}
			if (VK_QUEUE_GRAPHICS_BIT & flags)
			{
				iscomplete = iscomplete && Graphics.has_value();
			}
			return iscomplete;
		}
	};
	class VulkanPhysicalDevice
	{
	public:
		static Ref<VulkanPhysicalDevice> Select();	
	public:
		VulkanPhysicalDevice();
		~VulkanPhysicalDevice();

		VkPhysicalDevice GetVulkanPhysicalDevice() { return m_PhysicalDevice; }

		const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }
		QueueFamilyIndices& GetQueueFamilyIndices()  { return m_QueueFamilyIndices; }
		
		bool IsExtensionSupported(const std::string& extensionName) const;
	private:
		QueueFamilyIndices FindQueueFamilies(int flags);
	private:
		//--PhysicalDevice--// 
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkPhysicalDeviceProperties m_Properties;
		VkPhysicalDeviceFeatures m_Features;
		VkPhysicalDeviceMemoryProperties m_MemoryProperties;
		
		//--QueueFamily--//
		QueueFamilyIndices m_QueueFamilyIndices;
		std::vector<VkQueueFamilyProperties> m_QueueFamilyProperties;
		std::vector<VkDeviceQueueCreateInfo> m_QueueCreateInfos;
		//support extensions
		std::unordered_set<std::string> m_SupportedExtensions;

		friend class VulkanDevice;
	};

	class VulkanDevice
	{
	public:
		VulkanDevice(const Ref<VulkanPhysicalDevice>& physicalDevice);
		~VulkanDevice();

		void Create(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledDeviceExtensions);
		void Destroy();

		const Ref<VulkanPhysicalDevice>& GetPhysicalDevice() const { return m_PhysicalDevice; }
		const VkDevice GetVulkanDevice() const { return m_LogicalDevice; }

		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		VkQueue GetComputeQueue() const { return m_ComputeQueue; }
	private:
		VkDevice				  m_LogicalDevice = VK_NULL_HANDLE;
		Ref<VulkanPhysicalDevice> m_PhysicalDevice;
		VkPhysicalDeviceFeatures  m_EnabledFeatures;

		VkQueue			m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue			m_ComputeQueue = VK_NULL_HANDLE;

		

	};
}