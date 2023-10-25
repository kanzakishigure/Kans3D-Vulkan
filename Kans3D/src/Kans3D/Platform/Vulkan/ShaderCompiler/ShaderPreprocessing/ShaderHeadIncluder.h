#pragma once

#include "ShaderPreprocessor.h"

#include <shaderc/shaderc.hpp>
#include <libshaderc_util/file_finder.h>



namespace Kans
{

	struct IncludeHeadData;

	// the code implement from shaderc/glslc/file_includer.hpp
	class GLSLHeadIncluder : public shaderc::CompileOptions::IncluderInterface
	{

	public:
		explicit GLSLHeadIncluder(const shaderc_util::FileFinder* file_finder);
		~GLSLHeadIncluder() override;

		shaderc_include_result* GetInclude(const char* requested_source,
			shaderc_include_type type,
			const char* requesting_source,
			size_t include_depth) override;

		void ReleaseInclude(shaderc_include_result* include_result) override;
		std::unordered_set<IncludeHeadData> GetIncludeHeadData() { return m_IncludeHeadData; }
		

	private:

		struct FileInfo
		{
			std::string full_path;
			std::string  contents;
		};
		// Used by GetInclude() to get the full filepath.
		const shaderc_util::FileFinder& m_FileFinder;
	
		std::unordered_set<IncludeHeadData> m_IncludeHeadData;

		// because we use the headFile preprocessor,we need to avoid preprocessing headFile multi-time
		std::unordered_map<std::string, HeaderSourceCache> m_HeaderSourceCaches;

		
	};
}