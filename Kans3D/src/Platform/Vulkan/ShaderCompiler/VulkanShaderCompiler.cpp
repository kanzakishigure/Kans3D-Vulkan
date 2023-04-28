#include "kspch.h"	
#include "VulkanShaderCompiler.h"

#include "ShaderPreprocessing/ShaderHeadIncluder.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Renderer/Renderer.h"
#include "ShaderPreprocessing/ShaderHeadIncluder.h"
#include "VulkanShaderCache.h"

#include "Kans3D/Core/Hash.h"

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <fstream>
namespace Kans
{

	namespace Utils
	{
		template<RendererAPIType API>
		std::filesystem::path GetCacheDirectory()
		{
			if constexpr (API == RendererAPIType::Vulkan)
			{
				return  KansFileSystem::GetShaderCacheFolder() / "Vulkan";
			}
			if constexpr (API == RendererAPIType::OPENGL)
			{
				return  KansFileSystem::GetShaderCacheFolder() / "OpenGL";
			}
		}
		template<RendererAPIType API>
		bool CreateCacheDirectoryIfNeeded()
		{
			std::filesystem::path cachePath = GetCacheDirectory<API>();
			
			if (!KansFileSystem::Exists(cachePath))
			{
				KansFileSystem::CreateDirectories(cachePath);
				return true;
			}
			return false;
		}
	}
	
	VulkanShaderCompiler::VulkanShaderCompiler(const std::filesystem::path& shaderSourcePath, bool disableOptimization /*= false*/)
		:m_ShaderSourcePath(shaderSourcePath),m_DisableOptimization(disableOptimization)
	{
		m_Language = ShaderUtils::ShaderLangFromExtension(shaderSourcePath.extension().string());
	}

	VulkanShaderCompiler::~VulkanShaderCompiler()
	{

	}

	Ref<VulkanShader> VulkanShaderCompiler::Compile(const std::filesystem::path& shaderSourcePath, bool disableOptimization /*= false*/, bool forceCompile /*= false*/)
	{
		
		Ref<VulkanShader> shader = CreateScope<VulkanShader>();

		std::string path = shaderSourcePath.string();
		size_t findPos = path.find_last_of("/\\");
		std::string name = findPos == std::string::npos ? path : path.substr(findPos+1);

		findPos = name.rfind('.');
		name = findPos == std::string::npos ?name : name.substr(0,findPos);
		shader->m_Name = name;
		shader->m_AssetPath = path;
		shader->m_DisableOptimization = disableOptimization;

		VulkanShaderCompiler compiler(shaderSourcePath, disableOptimization);
		compiler.Reload(forceCompile);

		shader->LoadAndCreateShader(compiler.m_SPIRVData);

		
		
		return shader;
	}

	std::string VulkanShaderCompiler::Compile(std::vector<uint32_t>& outputBinary, const VkShaderStageFlagBits stage, CompileOptions options) const
	{

		const std::string& shaderSource = m_ShaderSource.at(stage);

		if (m_Language == ShaderUtils::ShaderLang::GLSL)
		{
			shaderc::Compiler compiler;
			shaderc::CompileOptions compileOptions;
			
			compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
			compileOptions.SetWarningsAsErrors();
			if (options.GenerateDebugInfo)
			{
				compileOptions.SetGenerateDebugInfo();
			}
			if (options.Optimize)
			{
				compileOptions.SetOptimizationLevel(shaderc_optimization_level_performance);
			}

			// Compile shader
			const shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(shaderSource, ShaderUtils::ShaderStageToShaderc(stage), m_ShaderSourcePath.string().c_str(), compileOptions);

			if (module.GetCompilationStatus() != shaderc_compilation_status_success)
			{
				return fmt::format("compile shader:[{}] at stage:[{}] \n error:{}",m_ShaderSourcePath.string(),ShaderUtils::ShaderStageToString(stage), module.GetErrorMessage());
			}

			outputBinary = std::vector<uint32_t>(module.cbegin(), module.cend());
			return {};
			
		}
		return "Unknown Language";
		
	}

	bool VulkanShaderCompiler::TryRecompile(VulkanShader* shader)
	{
		if(shader == nullptr)
		{
			return false;
		}
		VulkanShaderCompiler compiler(shader->m_AssetPath,shader->m_DisableOptimization);
		bool compileSucceeded = compiler.Reload(true);
		if (!compileSucceeded)
		{
			return false;
		}
		shader->Release();
		shader->LoadAndCreateShader(compiler.GetSPIRVData());
		return true;
	}

	bool VulkanShaderCompiler::Reload(bool forceCompile)
	{
		m_ShaderSource.clear();
		m_SPIRVDebugData.clear();
		m_SPIRVData.clear();
	
		Utils::CreateCacheDirectoryIfNeeded<RendererAPIType::Vulkan>();
		std::string source = KansFileSystem::ReadFileSkipBOM(m_ShaderSourcePath);

		m_ShaderSource = Preprocess(source);

		//use cache to check if the shader has changed
		VkShaderStageFlagBits chanedStage = VulkanShaderCache::GetChangedStage(*this);


		//compile shader if shader stage is changed;
		bool compileSuccess = CompileAndUpdateVulkanBinary(chanedStage,forceCompile);
		
		if (!compileSuccess)
		{
 			CORE_ASSERT(false);
			return false;
		}
		//Once we have the SPIR-V byte code we can use for shader reflection
		// use SPIR-V spirv_cross Get the ShaderLayout
		SerializeReflectionMetaData();

		

		return true;
	}

	std::map<VkShaderStageFlagBits, std::string> VulkanShaderCompiler::Preprocess(const std::string& source)
	{
		switch (m_Language)
		{
		case Kans::ShaderUtils::ShaderLang::GLSL: return PreprocessGLSL(source);
			break;
		case Kans::ShaderUtils::ShaderLang::HLSL: return PreprocessHLSL(source);
			break;
		}

		CORE_ASSERT(false,"unknow shader language");
		return {};
	}

	std::map<VkShaderStageFlagBits, std::string> VulkanShaderCompiler::PreprocessGLSL(const std::string& source)
	{
		//--PreprocessShader--//
		// get the include header
		// get the macro
		// add the global macro
		// separate the shader stage
		// catch the shader stage
		// cache the shader hash to file

		std::map<VkShaderStageFlagBits, std::string> shaderSources = ShaderPreprocessor::PreprocessShader<ShaderUtils::ShaderLang::GLSL>(source);

		
			
			
			static shaderc::Compiler compiler;
			shaderc_util::FileFinder fileFinder;
			fileFinder.search_path().emplace_back("Resources/Shaders/Include/"); //Main include directory

			for (auto& [stage, shaderSource] : shaderSources)
			{
				shaderc::CompileOptions options;

				//include shader header
				GLSLHeadIncluder* headIncluder = new GLSLHeadIncluder(&fileFinder);
				options.SetIncluder(std::unique_ptr<GLSLHeadIncluder>(headIncluder));

				//options.SetGenerateDebugInfo();
				const auto& globalMacros = Renderer::GetGlobalShaderMacros();
				for (const auto& [name, value] : globalMacros)
					options.AddMacroDefinition(name, value);
				
				// we can generate shader hash from shaderSource
				shaderc::PreprocessedSourceCompilationResult result = compiler.PreprocessGlsl(shaderSource, ShaderUtils::ShaderStageToShaderc(stage), m_ShaderSourcePath.string().c_str(), options);
				if (result.GetCompilationStatus() != shaderc_compilation_status_success)
				{
					CORE_ERROR_TAG("Renderer", fmt::format("fail to preprocess {} : {}.\nError: {}", m_ShaderSourcePath, ShaderUtils::ShaderStageToString(stage), result.GetErrorMessage()));
				}
				shaderSource = std::string(result.begin(), result.end());

			
				m_ShaderCacheDatas[stage].HashValue = Hash::GenerateMD5Hash(shaderSource);
				m_ShaderCacheDatas[stage].Headers = headIncluder->GetIncludeHeadData();
			}
		

		return shaderSources;
	}

	std::map<VkShaderStageFlagBits, std::string> VulkanShaderCompiler::PreprocessHLSL(const std::string& source)
	{
		//catch the shader stage
		return {};
	}

	bool VulkanShaderCompiler::CompileAndUpdateVulkanBinary(VkShaderStageFlagBits changedStages, const bool forceCompile)
	{
		for (const auto& [stage,source]:m_ShaderSource )
		{
			if (!CompileAndGetVulkanBinary(stage,m_SPIRVData[stage], changedStages, false, forceCompile))
			{
				return false;
			}
			if (!CompileAndGetVulkanBinary(stage, m_SPIRVDebugData[stage], changedStages, true, forceCompile))
			{
				return false;
			}
		}
		return true;
	}

	

	bool VulkanShaderCompiler::CompileAndGetVulkanBinary(const VkShaderStageFlagBits stage, std::vector<uint32_t>& data, const VkShaderStageFlagBits changedStages, bool debug, const bool forceCompile)
	{
		
		PROFILE_SCOPE_LOG("compile")
		std::filesystem::path cacheDir =  Utils::GetCacheDirectory<RendererAPIType::Vulkan>();
		const auto extention = ShaderUtils::ShaderStageCachedFileExtension(stage, debug);

		bool needCompile = stage & changedStages;
		if (!needCompile&&!forceCompile)
		{
			TryGetVulkanCachedBinary(cacheDir, extention, data);
		}

		if (data.empty())
		{
			CompileOptions options;
			if (debug)
			{
				options.Optimize=false;
				options.GenerateDebugInfo = true;
			}
			else
			{
				options.Optimize = !m_DisableOptimization && stage != VK_SHADER_STAGE_COMPUTE_BIT;
				options.GenerateDebugInfo = false;
			}
			std::string errorMsg = Compile(data, stage, options);
			if (!errorMsg.empty())
			{
				CORE_ERROR_TAG("Renderer", "compile error : {}", errorMsg);
				//try load file from cache
				TryGetVulkanCachedBinary(cacheDir, extention, data);
				if (data.empty())
				{
					CORE_ERROR("compile fail,shader:[{}] stage:[{}] couldn't find a cached version", m_ShaderSourcePath, ShaderUtils::ShaderStageToString(stage));
				}
				else
				{
					CORE_WARN("compile fail,load shader:[{}] stage:[{}] from cache", m_ShaderSourcePath, ShaderUtils::ShaderStageToString(stage));
				}
				return false;
			}
			else
			{
				//cache shader to cachePath
				std::filesystem::path cacheFilePath = cacheDir / (m_ShaderSourcePath.filename().string() + extention);
				std::ofstream ostream(cacheFilePath,std::ios::binary);
				if (!ostream)
				{
					CORE_ERROR("Failed to cache shader binary!");
					ostream.close();
					return false;
				}
				ostream.write((char*)data.data(), sizeof(uint32_t) * data.size());
				ostream.close();
			}
		}
		return true;
	}

	void VulkanShaderCompiler::TryGetVulkanCachedBinary(const std::filesystem::path& cacheDirectory, const char* extention, std::vector<uint32_t>& outData)
	{
		std::filesystem::path cacheFilePath = cacheDirectory /(m_ShaderSourcePath.filename().string() + extention);
		
		std::ifstream stream(cacheFilePath,std::ios::ate|std::ios::binary);
		if (!stream.good())
		{
			return;
		}
		std::streampos end = stream.tellg();
		stream.seekg(0, std::ios::beg);
		size_t size = end - stream.tellg();
		
		outData.resize(size/sizeof(uint32_t));
		stream.read((char*)outData.data(), size);
		stream.close();
	}

	void VulkanShaderCompiler::SerializeReflectionMetaData()
	{
		for (const auto& [Stage, data] : m_SPIRVDebugData)
		{
			ReflectMetaData(Stage, data);
		}
	}

	void VulkanShaderCompiler::ReflectMetaData(VkShaderStageFlagBits stage, const std::vector<uint32_t>& data)
	{
		spirv_cross::Compiler compiler(data);

		spirv_cross::ShaderResources resource = compiler.get_shader_resources();
		//use the vulkanshaderResource store metadata

	}

}