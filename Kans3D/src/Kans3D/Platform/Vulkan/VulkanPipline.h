#pragma once
#include <vulkan/vulkan.h>
#include "VulkanImage.h"
namespace Kans
{
	class VulkanPipline
	{
	public: 
		VulkanPipline();
		~VulkanPipline() = default;

		void Create();
		void Reset();
		

	private:
		
	};
}