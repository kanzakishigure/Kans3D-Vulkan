#pragma once
#include "Kans3D/Core/Cache.h"

#include "VulkanShaderCompiler.h"
namespace Kans
{


	class VulkanShaderCache :public Cache
	{
	public:
		static VkShaderStageFlagBits GetChangedStage(const VulkanShaderCompiler& shadercompiler);
	private:
		static void Deserialize(std::map<std::string, std::map<VkShaderStageFlagBits, ShaderCacheData>>& shaderCache);
		static void Serialize(const std::map<std::string, std::map<VkShaderStageFlagBits, ShaderCacheData>>& shaderCache);
	};

	
}