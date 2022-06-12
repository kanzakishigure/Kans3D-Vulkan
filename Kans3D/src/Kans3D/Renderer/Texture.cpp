#include "hzpch.h"
#include "Texture.h"
#include "Renderer.h"
#include "Platform/OpenGL/OpenGLTexture.h"
namespace Kans {

	Ref<Texture2D> Texture2D::Create(const std::string& path)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLTexture2D>(path);
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
		
	}
	Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLTexture2D>(width, height);
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

}