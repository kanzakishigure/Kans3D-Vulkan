#include "kspch.h"
#include "VulkanShaderCache.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Platform/Vulkan/VulkanShaderUtils.h"

#include <yaml-cpp/yaml.h>
#define YAML_SERIALIZE_PROPERTY(emitter,key,value) emitter<< YAML::Key << key ; emitter << YAML::Value<< value;

#define YAML_DESERIALIZE_PROPERTY(iterator,key,value,type,defaultValue)  \
if(iterator.IsMap()){												\
	if (const auto& node = iterator[key]){							\
		try{														\
			value = node.as<type>();				\
		}															\
		catch (const std::exception& e){							\
			CORE_ERROR(e.what());									\
			value = defaultValue;									\
		}															\
	}																\
	else{ value = defaultValue; }									\
}																	\
else { value = defaultValue; }										
																	
															
namespace Kans
{

	VkShaderStageFlagBits VulkanShaderCache::GetChangedStage(const VulkanShaderCompiler& shaderCompiler)
	{
		std::string cachedShaderPath = shaderCompiler.m_ShaderSourcePath.string();
		std::map<std::string, std::map<VkShaderStageFlagBits, ShaderCacheData>> shaderCache;

		Deserialize(shaderCache);
		VkShaderStageFlagBits changedStage= {};
		//check if the shader has cached
		const bool hasCached = shaderCache.find(cachedShaderPath) != shaderCache.end();
		for (const auto& [stage, stageSource] : shaderCompiler.m_ShaderSource)
		{
			//because the shader may not been cached so we need use [] operator to create object
			if (!hasCached || shaderCompiler.m_ShaderCacheDatas.at(stage) != shaderCache[cachedShaderPath][stage])
			{
				shaderCache[cachedShaderPath][stage] = shaderCompiler.m_ShaderCacheDatas.at(stage);
				changedStage = (VkShaderStageFlagBits)((*(int*) & changedStage) | stage);
			}
		}
		
		//because we use the  [] operator ,we need to clear void object;
		shaderCache.at(cachedShaderPath) = shaderCompiler.m_ShaderCacheDatas;

		if (changedStage)
		{
			Serialize(shaderCache);
		}
		
		return changedStage;
	}

	void VulkanShaderCache::Deserialize(std::map<std::string, std::map<VkShaderStageFlagBits, ShaderCacheData>>& shaderCache)
	{
		std::ifstream stream(KansFileSystem::GetShaderCacheFolder() / "ShaderRegistry.yaml");
		if (!stream.good())
			return;

		std::stringstream strStream;
		strStream << stream.rdbuf();

		YAML::Node data = YAML::Load(strStream.str());
		auto nodes = data["ShaderRegistry"];
		if (nodes.IsNull())
		{
			CORE_ERROR("[ShaderCache] Shader Registry is invalid.");
			return;
		}

		for (const auto& shader : nodes)
		{
			std::string path;
			YAML_DESERIALIZE_PROPERTY(shader, "ShaderPath", path, std::string,std::string());
			for (const auto& stage : shader["Stages"])
			{
				std::string stageHash;
				std::string stagetypestr;
				YAML_DESERIALIZE_PROPERTY(stage, "StageHash", stageHash, std::string, std::string());
				YAML_DESERIALIZE_PROPERTY(stage, "Stage", stagetypestr, std::string, std::string());
				auto& stageCache = shaderCache[path][ShaderUtils::ShaderStageFromString(stagetypestr)];
				stageCache.HashValue = stageHash;

				for (const auto& head : stage["IncludeHeads"])
				{
					std::filesystem::path IncludedFilePath;
					size_t IncludeDepth;
					bool IsRelative;
					std::string HashValue;
					YAML_DESERIALIZE_PROPERTY(head, "IncludedFilePath", IncludedFilePath, std::string, std::string());
					YAML_DESERIALIZE_PROPERTY(head, "IncludeDepth", IncludeDepth,size_t, size_t(-1));
					YAML_DESERIALIZE_PROPERTY(head, "IsRelative", IsRelative,bool, false);
					YAML_DESERIALIZE_PROPERTY(head, "HashValue", HashValue, std::string,std::string());
					stageCache.Headers.emplace(IncludeHeadData({ IncludedFilePath ,IncludeDepth,IsRelative,HashValue }));
				}
			}
		}
		
	}

	void VulkanShaderCache::Serialize(const std::map<std::string, std::map<VkShaderStageFlagBits, ShaderCacheData>>& shaderCache)
	{
		

		YAML::Emitter out;
		out << YAML::BeginMap << YAML::Key << "ShaderRegistry" << YAML::BeginSeq;
		for (const auto& [filePath, cache] : shaderCache)
		{
			out << YAML::BeginMap;
			out << YAML::Key << "ShaderPath" << YAML::Value << filePath;
			out << YAML::Key << "Stages" << YAML::BeginSeq;
			for (const auto& [stage, cacheData] : cache)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "Stage" << YAML::Value << ShaderUtils::ShaderStageToString(stage);
				out << YAML::Key << "StageHash" << YAML::Value << cacheData.HashValue;
				out << YAML::Comment("Hash value stored as char");

				out << YAML::Key << "IncludeHeads" << YAML::BeginSeq;
				for (const auto& header : cacheData.Headers)
				{
					out << YAML::BeginMap;
					YAML_SERIALIZE_PROPERTY(out, "IncludedFilePath", header.IncludedFilePath.string());
					YAML_SERIALIZE_PROPERTY(out, "HashValue", header.HashValue);
					YAML_SERIALIZE_PROPERTY(out, "IsRelative", header.IsRelative);
					YAML_SERIALIZE_PROPERTY(out, "IncludeDepth", header.IncludeDepth);
					out << YAML::EndMap;//end head
				}
				out << YAML::EndSeq;;// end heads
				out << YAML::EndMap;
			}
			out << YAML::EndSeq;//end stage vector
			out << YAML::EndMap; //end shader

		}
		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream ofs(KansFileSystem::GetShaderCacheFolder() / "ShaderRegistry.yaml");
		ofs << out.c_str();

	}

}