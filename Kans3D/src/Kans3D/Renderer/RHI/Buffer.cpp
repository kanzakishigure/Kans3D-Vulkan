#include "kspch.h"
#include "Buffer.h"
#include "kans3D/Renderer/Renderer.h"
#include "kans3D/Platform/OpenGL/OpenGLBuffer.h"
namespace Kans {


	Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateScope<OpenGLVertexBuffer>(vertices, size);
		case RendererAPIType::Vulkan:  CORE_WARN("Function[VertexBuffer::Create(float* vertices, uint32_t size)] not implemented"); return nullptr;
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}



	Ref<Kans::VertexBuffer> VertexBuffer::Create(uint32_t size)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateScope<OpenGLVertexBuffer>(size);
		case RendererAPIType::Vulkan:  CORE_WARN("Function[VertexBuffer::Create(uint32_t size)] not implemented"); return nullptr;
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

	Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateScope<OpenGLIndexBuffer>(indices, count);
		case RendererAPIType::Vulkan:  CORE_WARN("Function[IndexBuffer::Create(uint32_t* indices, uint32_t count)] not implemented"); return nullptr;
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

}
