#include "kspch.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Platform/OpenGL/OpenGLTexture.h"
#include "Kans3D/Platform/Vulkan/VulkanTexture.h"
#include "Texture.h"
namespace Kans {


	Ref<Kans::Texture2D> Texture2D::Create(const TextureSpecification& specification,Buffer buffer)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::Vulkan:   return CreateRef<VulkanTexture2D>(specification, buffer);
		case RendererAPIType::OPENGL:   return CreateRef<OpenGLTexture2D>(specification, buffer);
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}




	Ref<Texture2D> Texture2D::Create(const TextureSpecification& specification, const std::filesystem::path& filepath)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::Vulkan:   return CreateRef<VulkanTexture2D>(specification,filepath);
		case RendererAPIType::OPENGL:   return CreateRef<OpenGLTexture2D>(specification, filepath);
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

	Ref<TextureCube> TextureCube::Create(const std::string& path)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLTextureCube>(path);
		case RendererAPIType::Vulkan:  return nullptr;
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

	Ref<TextureCube> TextureCube::Create(uint32_t width, uint32_t height)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLTextureCube>(width, height);
		case RendererAPIType::Vulkan:  return nullptr;
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

}