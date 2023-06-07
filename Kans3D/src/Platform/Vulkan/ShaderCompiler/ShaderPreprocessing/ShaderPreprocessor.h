#pragma once
#include "Platform/Vulkan/VulkanShaderUtils.h"
#include "Kans3D/Utilities/StringUtils.h"

#include <vulkan/vulkan.h>

#include <unordered_set>
#include <filesystem>
namespace Kans
{
	namespace PreprocessUtils
	{
		// From https://wandbox.org/permlink/iXC7DWaU8Tk8jrf3 and is modified.
		enum class State : char { SlashOC, StarIC, SingleLineComment, MultiLineComment, NotAComment };

		template <typename InputIt, typename OutputIt>
		void CopyWithoutComments(InputIt first, InputIt last, OutputIt out)
		{
			State state = State::NotAComment;

			while (first != last)
			{
				switch (state)
				{
				case State::SlashOC:
					if (*first == '/') state = State::SingleLineComment;
					else if (*first == '*') state = State::MultiLineComment;
					else
					{
						state = State::NotAComment;
						*out++ = '/';
						*out++ = *first;
					}
					break;
				case State::StarIC:
					if (*first == '/') state = State::NotAComment;
					else state = State::MultiLineComment;
					break;
				case State::NotAComment:
					if (*first == '/') state = State::SlashOC;
					else *out++ = *first;
					break;
				case State::SingleLineComment:
					if (*first == '\n')
					{
						state = State::NotAComment;
						*out++ = '\n';
					}
					break;
				case State::MultiLineComment:
					if (*first == '*') state = State::StarIC;
					else if (*first == '\n') *out++ = '\n';
					break;
				}
				++first;
			}
		}

		template <bool RemoveHeaderGuard = false>
		bool ContainsHeaderGuard(std::string& headerSource)
		{
			size_t pos = headerSource.find('#');
			while (pos != std::string::npos)
			{
				const size_t endOfLine = headerSource.find_first_of("\r\n", pos);
				auto tokens = Utils::SplitString(headerSource.substr(pos, endOfLine - pos));
				auto it = tokens.begin();
				it++;//skip #
				if (*it == "pragma")
				{
					if (*(++it) == "once")
					{
						if constexpr (RemoveHeaderGuard)
						{
							headerSource.erase(pos, endOfLine-pos);
							return true;
						}
					}
				}
				pos = headerSource.find('#',pos+1);
			}
			return false;
		}

	};

	struct IncludeHeadData
	{
		std::filesystem::path IncludedFilePath{};
		size_t IncludeDepth{};
		bool IsRelative{ false };
		std::string HashValue{};

		inline bool operator==(const IncludeHeadData& other) const
		{
			return this->IncludedFilePath == other.IncludedFilePath && this->HashValue == other.HashValue;
		}

	};

	struct HeaderSourceCache
	{
		std::string Source;
		std::string SourceHash;
		bool IsGuarded;
	};
}




namespace std
{
	template<>
	struct hash<Kans::IncludeHeadData>
	{
		size_t operator()(const Kans::IncludeHeadData& data) const
		{
			return ((*(uint32_t*)data.HashValue.data())^std::filesystem::hash_value(data.IncludedFilePath));
		}
	};

	
}

namespace Kans
{
	class ShaderPreprocessor
	{

	public:
		template<typename ShaderUtils::ShaderLang Lang>
		static std::map<VkShaderStageFlagBits, std::string> PreprocessShader(const std::string& source);

		template<typename ShaderUtils::ShaderLang Lang>
		static void PreprocessHeader(std::string& contents, bool& isGuarded, const std::filesystem::path& fullPath);
	};

	template<typename ShaderUtils::ShaderLang Lang>
	static std::map<VkShaderStageFlagBits, std::string>
		Kans::ShaderPreprocessor::PreprocessShader(const std::string& source)
	{
		//get the source code without explanatory 
		std::stringstream preFilterSource;
		PreprocessUtils::CopyWithoutComments(source.begin(), source.end(), std::ostream_iterator<char>(preFilterSource));	
		std::string newSource = preFilterSource.str();

		std::map<VkShaderStageFlagBits, std::string> shaderSources;
		std::vector<std::tuple<VkShaderStageFlagBits,size_t>> shaderStagePositions;

		size_t startOfStage = 0;
		size_t pos = newSource.find('#');


		if constexpr (Lang == ShaderUtils::ShaderLang::GLSL)
		{
			const size_t endofline = newSource.find_first_of("\r\n", pos);
			const std::vector<std::string>& tokens = Utils::SplitString(newSource.substr(pos, endofline - pos));
			CORE_ASSERT(tokens.size() >3,"illigal version");
			CORE_INFO("GLSL sahder request Version : {0}", tokens[2]);
		}

		while (pos!=std::string::npos)
		{
			const size_t endofline = newSource.find_first_of("\r\n", pos);
			const std::vector<std::string>& tokens = Utils::SplitString(newSource.substr(pos, endofline - pos));

			size_t index = 1; //skip #
			if (tokens[index] == "pragma")
			{
				++index;
				if (tokens[index] == "stag")
				{
					++index;
					CORE_ASSERT(tokens[index] == ":", "Stage pragma is invalid");
					++index;
					const std::string_view stage = tokens[index];
					CORE_ASSERT(stage == "vert"|| stage == "comp"|| stage == "frag"|| stage == "geo", "invalid shader type : {0} ", tokens[index]);
					auto shaderStage = ShaderUtils::ShaderTypeFromString(stage);
					shaderStagePositions.push_back({ shaderStage,startOfStage });
				}
			}
			else if constexpr (Lang == ShaderUtils::ShaderLang::GLSL)
			{
				if (tokens[index] == "version")
				{
					startOfStage = pos;
					++index;
				}
			}
			pos = newSource.find('#', pos + 1);
		}
		
		//split shader source
		CORE_ASSERT(shaderStagePositions.size(), "preprocesse shader fail,There are no known stages defined in file ");
		
		
		if (shaderStagePositions.size() > 1)
		{

			//Get stages in the middle
			for (size_t stageIndex = 0; stageIndex < shaderStagePositions.size()-1; stageIndex++)
			{
				auto& [Pshaderstage, Pstagepos] = shaderStagePositions[stageIndex];
				auto& [Nshaderstage, Nstagepos] = shaderStagePositions[stageIndex+1];

				const std::string stageStr = newSource.substr(Pstagepos, Nstagepos- Pstagepos);
				shaderSources[Pshaderstage] = stageStr;
			}

			//Get last stage
			auto& [shaderstage, stagepos] = shaderStagePositions[shaderStagePositions.size() - 1];
			shaderSources[shaderstage] = newSource.substr(stagepos, newSource.size() - stagepos);
		}
		else
		{
			auto& [shaderstage, stagepos] = shaderStagePositions[0];
			shaderSources[shaderstage] = newSource;
		}
		return shaderSources;

	}

	template<typename ShaderUtils::ShaderLang Lang>
	void ShaderPreprocessor::PreprocessHeader(std::string& contents, bool& isGuarded, const std::filesystem::path& fullPath)
	{
		std::stringstream sourceStream;
		PreprocessUtils::CopyWithoutComments(contents.begin(), contents.end(), std::ostream_iterator<char>(sourceStream));
		contents = sourceStream.str();

		isGuarded = PreprocessUtils::ContainsHeaderGuard<Lang==ShaderUtils::ShaderLang::GLSL>(contents);
				
	}


}