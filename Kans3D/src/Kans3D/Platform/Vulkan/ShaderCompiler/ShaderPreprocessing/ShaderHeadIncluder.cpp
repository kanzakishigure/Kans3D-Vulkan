#include "kspch.h"
#include "ShaderHeadIncluder.h"

#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Core/Hash.h"
#include "Kans3D/Platform/Vulkan/VulkanShader.h"

namespace Kans
{

	GLSLHeadIncluder::GLSLHeadIncluder(const shaderc_util::FileFinder* file_finder)
		: m_FileFinder(*file_finder)
	{

	}
	GLSLHeadIncluder::~GLSLHeadIncluder()
	{
		CORE_INFO("call ~GLSLHeadIncluder");
	}

	shaderc_include_result* GLSLHeadIncluder::GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth)
	{
		std::string fullpath = (type== shaderc_include_type_relative) 
			? m_FileFinder.FindRelativeReadableFilepath(requesting_source, requested_source)
			: m_FileFinder.FindReadableFilepath(requested_source);

		CORE_ASSERT(!fullpath.empty(), "can't find include path");
		
		const bool alreadyIncluded = std::find_if(m_IncludeHeadData.begin(), m_IncludeHeadData.end(), [fullpath](const IncludeHeadData& data) {
			return data.IncludedFilePath == fullpath;
			}) != m_IncludeHeadData.end();
		auto& [source, sourceHash, isGuarded] = m_HeaderSourceCaches[fullpath];

		if (source.empty()&& sourceHash.empty())
		{
			source = KansFileSystem::ReadFileSkipBOM(fullpath);
			if (source.empty())
			{
				CORE_ERROR("can't include headfile {} : in {}", fullpath, requesting_source);
			}
			sourceHash = Hash::GenerateMD5Hash(source);
			ShaderPreprocessor::PreprocessHeader<ShaderUtils::ShaderLang::GLSL>(source, isGuarded, fullpath);
		}
		else if (isGuarded&& alreadyIncluded)
		{
			source.clear();
			CORE_WARN("\"{}\"Header already included,remove include", fullpath);
		}
		else if (!isGuarded && alreadyIncluded)
		{
			CORE_WARN("\"{}\" Header does not contain a header guard (#pragma once)", fullpath);
		}
		
		
		m_IncludeHeadData.emplace(IncludeHeadData{ fullpath, include_depth, type == shaderc_include_type_relative, sourceHash});
		//preprocess the head file and generate the shader hash code 

		shaderc_include_result* data = new shaderc_include_result;

		FileInfo* fileinfo = new FileInfo;
		fileinfo->full_path = fullpath;
		fileinfo->contents = source;

		data->source_name = fileinfo->full_path.data();
		data->source_name_length = fileinfo->full_path.size();
		data->content = fileinfo->contents.data();
		data->content_length = fileinfo->contents.size();
		data->user_data = fileinfo;
		return data;
	}

	void GLSLHeadIncluder::ReleaseInclude(shaderc_include_result* include_result)
	{
		FileInfo* fileinfo = (FileInfo*)include_result->user_data;
		delete fileinfo;
		delete include_result;
		CORE_TRACE("release includer");
	}

}