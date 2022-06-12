#include "hzpch.h"
#include "RendererAPI.h"
namespace Kans
{

	void RendererAPI::SetAPI(RendererAPIType api)
	{
		switch (api)
		{
		case Kans::RendererAPIType::OPENGL:
		case Kans::RendererAPIType::Vulkan:
		case Kans::RendererAPIType::NONE:
			s_API = api; return;
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
	}

}