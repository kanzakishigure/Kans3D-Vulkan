#pragma once
#include <vulkan/vulkan.h>
#include "VulkanBase.h"
namespace Kans
{
	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;
		bool isComplete() 
		{
			return graphicsFamily.has_value();
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

	private:
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		QueueFamilyIndices m_QueueFamilyIndices;
	};

	class VulkanDevice
	{
	public:
		VulkanDevice(const Ref<VulkanPhysicalDevice>& physicalDevice);
		~VulkanDevice();

		void Destroy();

		const Ref<VulkanPhysicalDevice>& GetPhysicalDevice() { return m_PhysicalDevice; }

		VkQueue GetGraphicsQueue() { return m_GraphicsQueue; }
	private:
		Ref<VulkanPhysicalDevice> m_PhysicalDevice;
		VkDevice m_Device;
		VkQueue m_GraphicsQueue;
		VkQueue m_PresentsQueue;
	};
}