#include "kspch.h"
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLVertexArray.h"
namespace Kans {


	Ref<VertexArray> VertexArray::Create()
	{
		
		switch (RendererAPI::Current())
		{
		case RendererAPIType::NONE:    CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return  CreateRef<OpenGLVertexArray>();
		}
		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}

}