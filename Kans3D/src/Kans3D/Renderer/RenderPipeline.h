#pragma once
#include "RenderPipelineBase.h"
namespace Kans
{
	class RenderPipeline : public RenderPipelineBase
	{

	public:
		RenderPipeline();
		virtual void Init(RenderPipelineSpecification spec) override final;
	private:
		Ref<RenderScene> m_RenderScene;
	};
}