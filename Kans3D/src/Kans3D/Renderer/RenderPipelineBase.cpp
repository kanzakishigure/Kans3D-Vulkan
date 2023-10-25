#include "kspch.h"
#include "RenderPipelineBase.h"
#include "RenderPipeline.h"

namespace Kans
{

	Kans::Ref<Kans::RenderPipelineBase> RenderPipelineBase::Create()
	{

		return CreateRef<RenderPipeline>();
	}

}