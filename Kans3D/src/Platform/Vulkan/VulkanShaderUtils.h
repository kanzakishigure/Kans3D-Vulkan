#pragma once
#include "Kans3D/Renderer/Shader.h"
#include <shaderc/shaderc.h>
#include "vulkan/vulkan.h"
namespace Kans {
	namespace ShaderUtils
	{
		
		inline static ShaderLang ShaderLangFromExtension(const std::string_view type)
		{
			if (type == ".glsl")	return ShaderLang::GLSL;
			if (type == ".hlsl")	return ShaderLang::HLSL;

			CORE_ASSERT(false);

			return ShaderLang::NONE;
		}

		inline static VkShaderStageFlagBits ShaderTypeFromString(const std::string_view type)
		{
			if (type == "vert")	return VK_SHADER_STAGE_VERTEX_BIT;
			if (type == "frag")	return VK_SHADER_STAGE_FRAGMENT_BIT;
			if (type == "comp")	return VK_SHADER_STAGE_COMPUTE_BIT;
			if (type == "geo")	return VK_SHADER_STAGE_GEOMETRY_BIT;

			return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		}
		

		inline static shaderc_shader_kind ShaderStageToShaderc(const VkShaderStageFlagBits stage)
		{
				switch (stage)
				{
				case VK_SHADER_STAGE_VERTEX_BIT:		return shaderc_shader_kind::shaderc_vertex_shader;
				case VK_SHADER_STAGE_FRAGMENT_BIT:		return shaderc_shader_kind::shaderc_fragment_shader;
				case VK_SHADER_STAGE_COMPUTE_BIT:		return shaderc_shader_kind::shaderc_compute_shader;
				case VK_SHADER_STAGE_GEOMETRY_BIT:		return shaderc_shader_kind::shaderc_geometry_shader;
				}

				CORE_ASSERT(false);
				return {};
		}
		inline static std::string ShaderStageToString(const VkShaderStageFlagBits stage)
		{
			switch (stage)
			{
			case VK_SHADER_STAGE_VERTEX_BIT:		return "vertex_shader";
			case VK_SHADER_STAGE_FRAGMENT_BIT:		return "fragment_shader";
			case VK_SHADER_STAGE_COMPUTE_BIT:		return "compute_shader";
			case VK_SHADER_STAGE_GEOMETRY_BIT:		return "geometry_shader";
			}

			CORE_ASSERT(false);
			return {};
		}
		inline static VkShaderStageFlagBits ShaderStageFromString(const std::string_view type)
		{
			
			if(type== "vertex_shader"	)		return  VK_SHADER_STAGE_VERTEX_BIT;
			if(type== "fragment_shader"	)		return VK_SHADER_STAGE_FRAGMENT_BIT;
			if(type== "compute_shader"	)		return VK_SHADER_STAGE_COMPUTE_BIT;
			if(type== "geometry_shader"	)		return VK_SHADER_STAGE_GEOMETRY_BIT;

			CORE_ASSERT(false);
			return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		}

		inline static const char* ShaderStageCachedFileExtension(const VkShaderStageFlagBits stage, bool debug)
		{
			if (debug)
			{
				switch (stage)
				{
					case VK_SHADER_STAGE_VERTEX_BIT:		return ".cache-debug.vert";
					case VK_SHADER_STAGE_FRAGMENT_BIT:		return ".cache-debug.frag";
					case VK_SHADER_STAGE_COMPUTE_BIT:		return ".cache-debug.comp";
					case VK_SHADER_STAGE_GEOMETRY_BIT:		return ".cache-debug.geo";
				}
			}
			else
			{
				switch (stage)
				{
					case VK_SHADER_STAGE_VERTEX_BIT:		return ".cache.vert";
					case VK_SHADER_STAGE_FRAGMENT_BIT:		return ".cache.frag";
					case VK_SHADER_STAGE_COMPUTE_BIT:		return ".cache.comp";
					case VK_SHADER_STAGE_GEOMETRY_BIT:		return ".cache.geo";
				}
			}
			CORE_ASSERT(false);
			return "";
		}


	}
}