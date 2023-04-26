#include "kspch.h"

//if has shader compiler
#include "ShaderCompiler/VulkanShaderCompiler.h"

#include "VulkanShader.h"
#include "VulkanDevice.h"
#include "VulkanRHI.h"
namespace Kans
{

	VulkanShader::VulkanShader(const std::string& path, bool forceCompile, bool disableOptimization)
		:m_AssetPath(path),m_DisableOptimization(disableOptimization)
	{
		auto lastSlash = path.find_last_of("/\\");
		lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
		auto lastDot = path.rfind('.');
		auto count = lastDot == std::string::npos ? path.size() - lastSlash : lastDot - lastSlash;
		m_Name = path.substr(lastSlash, count);
		Reload(forceCompile);

	}

	VulkanShader::~VulkanShader()
	{
		CORE_INFO("call Deconstruction{0}",typeid(VulkanShader).name());
	}

	void VulkanShader::Reload(bool forceCompile /*= false*/)
	{
		RT_Reload(forceCompile);
	}

	void VulkanShader::RT_Reload(bool forceCompile)
	{

		if (!VulkanShaderCompiler::TryRecompile(this))
		{
			CORE_ERROR_TAG("ShaderCompiler", "Failed to recompile shader!");
		}

	}

	void VulkanShader::SetShaderBuffer(ShaderBufferLayout layout)
	{
		
	}



	void VulkanShader::Release()
	{

	}

	void VulkanShader::LoadAndCreateShader(const std::map<VkShaderStageFlagBits, std::vector<uint32_t>>& shaderData)
	{
		m_ShaderData = shaderData;
		VkDevice currentdevice = VulkanRHI::GetCurrentDevice()->GetVulkanDevice();
		m_PipelineShaderStageCreateInfos.clear();
		//vkcreate shader;
		for (const auto&[stage,source] : m_ShaderData)
		{
			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = source.size()*sizeof(uint32_t);
			createInfo.pCode = source.data();

			VkShaderModule shaderModule;
			VK_CHECK_RESULT(vkCreateShaderModule(currentdevice,&createInfo, nullptr, &shaderModule));

			VkPipelineShaderStageCreateInfo shaderStageInfo{};
			shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStageInfo.stage = stage;
			shaderStageInfo.pName = "main";//function entry point
			shaderStageInfo.module = shaderModule;

			m_PipelineShaderStageCreateInfos.push_back(shaderStageInfo);

			
		}
	}

}