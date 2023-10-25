#include "kspch.h"
#include "RenderPipeline.h"
#include "RenderPipelineBase.h"

#include "RenderPass/CameraRenderPass.h"
#include "RenderPass/BloomPass.h"
#include "RenderPass/ToneShadingPass.h"
#include "RenderPass/ToneMappingPass.h"

namespace Kans
{

	void RenderPipeline::Init(RenderPipelineSpecification spec)
	{

		m_Specification = spec;

		m_MainCameraPass = CreateRef<MainCameraPass>();
		m_BloomPass = CreateRef<BloomPass>();
		m_ToneShadingPass = CreateRef<ToneShadingPass>();
		m_ColorGradingPass = CreateRef<ToneMappingPass>();
		
		
	}

}

