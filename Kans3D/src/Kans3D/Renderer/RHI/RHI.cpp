#include "kspch.h"
#include "RHI.h"

#include "Kans3D/Renderer/Renderer.h"

#include "Platform/OpenGL/OpenGLRHI.h"
#include "Platform/Vulkan/VulkanRHI.h"
namespace Kans
{

	Ref<RHI> RHI::Create(const Scope<Window>& window)
	{
		//return CreateRef<VulkanContext>();
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLRHI>(window);
		case RendererAPIType::Vulkan:  return CreateRef<VulkanRHI>(window);
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

	Kans::Ref<RHI> RHI::Create()
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLRHI>();
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

}