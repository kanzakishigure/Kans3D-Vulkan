#pragma once
#include "Kans3D/Renderer/Shader.h"
#include <vulkan/vulkan.h>

namespace Kans
{
	class VulkanShader :public Shader
	{
	public:
		VulkanShader() = default;
		VulkanShader(const std::string& path, bool forceCompile, bool disableOptimization);

		void Release();

		virtual ~VulkanShader();

		virtual void Reload(bool forceCompile = false) override;
		virtual void RT_Reload(bool forceCompile) override;

		virtual const std::string& GetName() const override { return m_Name; }

		virtual const ShaderBuffer& GetShaderBuffer() const override { return m_Shaderbuffer; }
		virtual void SetShaderBuffer(ShaderBufferLayout layout) override;

	private:
		void LoadAndCreateShader(const std::map<VkShaderStageFlagBits,std::vector<uint32_t>>&shaderData);
	private:
		std::string m_Name;
		std::string m_AssetPath;

		bool m_DisableOptimization = false;

		ShaderBuffer m_Shaderbuffer;
		std::map<VkShaderStageFlagBits, std::vector<uint32_t>> m_ShaderData;

		std::vector<VkPipelineShaderStageCreateInfo> m_PipelineShaderStageCreateInfos;
	private:
		friend class VulkanShaderCompiler;
	};



	
}