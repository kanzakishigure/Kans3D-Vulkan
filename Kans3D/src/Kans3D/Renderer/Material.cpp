#include "kspch.h"
#include "Material.h"
#include "kans3D/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLMaterial.h"

namespace Kans
{

	Ref<Material> Material::Create(const Ref<Shader>& shader, const std::string& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLMaterial>(shader, name);
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}


}

