#include "kspch.h"

// if has shader compiler
#include "ShaderCompiler/VulkanShaderCompiler.h"

#include "VulkanShader.h"
namespace Kans
{

    VulkanShader::VulkanShader(const std::string& path, bool forceCompile, bool disableOptimization) :
        m_AssetPath(path), m_DisableOptimization(disableOptimization)
    {
        auto lastSlash = path.find_last_of("/\\");
        lastSlash      = lastSlash == std::string::npos ? 0 : lastSlash + 1;
        auto lastDot   = path.rfind('.');
        auto count     = lastDot == std::string::npos ? path.size() - lastSlash : lastDot - lastSlash;
        m_Name         = path.substr(lastSlash, count);
        Reload(forceCompile);
    }

    VulkanShader::~VulkanShader() { CORE_INFO("call Deconstruction{0}", typeid(VulkanShader).name()); }

    void VulkanShader::Reload(bool forceCompile /*= false*/) { RT_Reload(forceCompile); }

    void VulkanShader::RT_Reload(bool forceCompile)
    {

        if (!VulkanShaderCompiler::TryRecompile(this))
        {
            CORE_ERROR_TAG("ShaderCompiler", "Failed to recompile shader!");
        }
    }

    void VulkanShader::SetShaderBuffer(ShaderBufferLayout layout) {}

    void VulkanShader::Release() {}

    // Retained legacy compiler bridge is CPU-only. RHI shader/module creation
    // will live in RHI/Vulkan in phase 3b, with explicit device ownership.
    void VulkanShader::StoreCompiledData(const std::map<VkShaderStageFlagBits, std::vector<uint32_t>>& shaderData)
    {
        m_ShaderData = shaderData;
    }

} // namespace Kans
