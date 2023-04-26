#include "kspch.h"
#include "Image.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanImage.h"


namespace Kans
{
	Ref<Image2D> Image2D::Create(ImageSpecification spec)
	{
		switch (RendererAPI::Current())
		{
			case RendererAPIType::Vulkan :  return CreateScope<VulkanImage2D>(spec);
		}

		CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}
}
