#include "kspch.h"
#include "RenderContext.h"
#include "Renderer.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/Vulkan/VulkanContext.h"
namespace Kans
{

	Kans::Ref<Kans::RenderContext> RenderContext::Create(GLFWwindow* windowHandle)
	{
		//return CreateRef<VulkanContext>();
		switch (Renderer::GetAPI())
		{
		case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLContext>(windowHandle);
		case RendererAPIType::Vulkan:  return CreateRef<VulkanContext>();
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

}