#include "kspch.h"
#include "Texture.h"
#include "Renderer.h"
#include "Platform/OpenGL/OpenGLTexture.h"
namespace Kans {

	Ref<Texture2D> Texture2D::Create(const std::string& path)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLTexture2D>(path);
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
		
	}
	Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLTexture2D>(width, height);
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

	Kans::Ref<Kans::TextureCube> TextureCube::Create(const std::string& path)
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

	Kans::Ref<Kans::TextureCube> TextureCube::Create(uint32_t width, uint32_t height)
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