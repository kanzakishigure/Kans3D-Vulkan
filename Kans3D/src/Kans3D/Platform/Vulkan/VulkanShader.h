#pragma once
#include <vulkan/vulkan.h>
#include "Kans3D/Platform/Vulkan/VulkanShaderTypes.h"

namespace Kans
{
    class VulkanShader : public RefCounter
    {
    public:
        VulkanShader() = default;
        VulkanShader(const std::string& path, bool forceCompile, bool disableOptimization);

        void Release();

        virtual ~VulkanShader();

        virtual void Reload(bool forceCompile = false);
        virtual void RT_Reload(bool forceCompile);

        virtual const std::string& GetName() const { return m_Name; }

        virtual const ShaderBuffer& GetShaderBuffer() const { return m_Shaderbuffer; }
        virtual void                SetShaderBuffer(ShaderBufferLayout layout);

    private:
        void LoadAndCreateShader(const std::map<VkShaderStageFlagBits, std::vector<uint32_t>>& shaderData);

    private:
        std::string m_Name;
        std::string m_AssetPath;

        bool m_DisableOptimization = false;

        ShaderBuffer                                           m_Shaderbuffer;
        std::map<VkShaderStageFlagBits, std::vector<uint32_t>> m_ShaderData;

        std::vector<VkPipelineShaderStageCreateInfo> m_PipelineShaderStageCreateInfos;

    private:
        friend class VulkanShaderCompiler;
    };

} // namespace Kans