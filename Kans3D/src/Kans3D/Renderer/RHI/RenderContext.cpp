#include "kspch.h"
#include "RenderContext.h"
#include "Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/Vulkan/VulkanContext.h"
namespace Kans
{

	Ref<RenderContext> RenderContext::Create(const Scope<Window>& window)
	{
		//return CreateRef<VulkanContext>();
		switch (Renderer::GetAPI())
		{
		case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLContext>(window);
		case RendererAPIType::Vulkan:  return CreateRef<VulkanContext>(window);
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

	Kans::Ref<Kans::RenderContext> RenderContext::Create()
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLContext>();
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

}