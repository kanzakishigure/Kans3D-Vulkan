#include "kspch.h"
#include "RendererAPI.h"
namespace Kans
{

	void RendererAPI::SetBackEnd(RendererAPIType api)
	{
		switch (api)
		{
		case Kans::RendererAPIType::OPENGL:
		case Kans::RendererAPIType::Vulkan:
		case Kans::RendererAPIType::NONE:
			s_API = api; return;
		}
		CORE_ASSERT(false, "unknow RendererAPI");
	}

}