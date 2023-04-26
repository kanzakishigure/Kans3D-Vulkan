#pragma once

#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanShaderUtils.h"
#include "ShaderPreprocessing/ShaderPreprocessor.h"
#include "Platform/Vulkan/VulkanShaderResource.h"

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#include <vulkan/vulkan.h>
namespace Kans
{
	struct CompileOptions
	{
		bool GenerateDebugInfo = false;
		bool Optimize = true;
	};
	struct ShaderCacheData
	{
		std::string HashValue;
		std::unordered_set<IncludeHeadData> Headers;

		// use for shaderCache compare
		bool operator== (const ShaderCacheData& other) const  { return this->Headers == other.Headers && this->HashValue == other.HashValue; }
		bool operator!= (const ShaderCacheData& other) const  { return !(*this == other); }

	};

	class ShaderRelfectData
	{
		std::vector<VulkanResource::ShaderResource::StorageBuffer> StorageBuffer;
	};
	class VulkanShaderCompiler
	{
	public:
		VulkanShaderCompiler(const std::filesystem::path& shaderSourcePath, bool disableOptimization = false);
		~VulkanShaderCompiler();

		static Ref<VulkanShader> Compile(const std::filesystem::path& shaderSourcePath,  bool disableOptimization = false, bool forceCompile = false);
		static bool TryRecompile(VulkanShader* shader);
		
		bool Reload(bool forceCompile);
		const std::map<VkShaderStageFlagBits, std::vector<uint32_t>>& GetSPIRVData() const { return m_SPIRVData; }

	private:
		std::map<VkShaderStageFlagBits, std::string> Preprocess(const std::string& source);
		std::map<VkShaderStageFlagBits, std::string> PreprocessGLSL(const std::string& source);
		std::map<VkShaderStageFlagBits, std::string> PreprocessHLSL(const std::string& source);

		std::string Compile(std::vector<uint32_t>& outputBinary, const VkShaderStageFlagBits stage, CompileOptions options) const;
		bool CompileAndUpdateVulkanBinary(VkShaderStageFlagBits changedStages,const bool forceCompile);
		bool CompileAndGetVulkanBinary(const VkShaderStageFlagBits ,std::vector<uint32_t>& data,const VkShaderStageFlagBits changedStages, bool debug, const bool forceCompile);
		void TryGetVulkanCachedBinary(const std::filesystem::path& ,const char* extention,std::vector<uint32_t>& outData);

		// I not sure should cache the metadata or just load meta data from file £¿

		bool TryReadReflectionMetaDataFromCahce();
		void SerializeReflectionMetaData();
		void ReflectMetaData(VkShaderStageFlagBits stage,const std::vector<uint32_t>& data);
	private:
		std::filesystem::path m_ShaderSourcePath;
		bool m_DisableOptimization = false;


		std::map<VkShaderStageFlagBits, std::string> m_ShaderSource;
		std::map<VkShaderStageFlagBits, std::vector<uint32_t>> m_SPIRVData;
		std::map<VkShaderStageFlagBits, std::vector<uint32_t>> m_SPIRVDebugData;


		//for every shaderStage we have a Hash Value , we compare the hash value with Serialized data
		std::map<VkShaderStageFlagBits, ShaderCacheData> m_ShaderCacheDatas;

		ShaderUtils::ShaderLang m_Language;

		friend class VulkanShaderCache;
		friend class VulkanShader;
		
	};

	

}