#include "kspch.h"
#include "FrameBuffer.h"
#include "kans3D/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLFrameBuffer.h"
namespace Kans {

	Kans::Ref<Kans::FrameBuffer> FrameBuffer::Create(const FrameBufferSpecification& spec)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
			case RendererAPIType::OPENGL:  return CreateRef<OpenGLFrameBuffer>(spec);
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

}