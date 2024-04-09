#include "kspch.h"
#include "RenderPipeline.h"
#include "RenderPipelineBase.h"

#include "RenderPass/DirectionLightPass.h"
#include "RenderPass/PointLightPass.h"
#include "RenderPass/CameraRenderPass.h"
#include "RenderPass/BloomPass.h"
#include "RenderPass/ToneShadingPass.h"
#include "RenderPass/ToneMappingPass.h"

namespace Kans
{

	RenderPipeline::RenderPipeline()
	{

	}

	void RenderPipeline::Init(RenderPipelineSpecification spec)
	{

		
		m_DirectionalLightPass		= CreateRef<DirectionalLightShadowPass>();
		m_PointLightShadowPass		= CreateRef<PointLightShadowPass>();
		m_MainCameraPass			= CreateRef<MainCameraPass>();
		m_BloomPass					= CreateRef<BloomPass>();
		m_ToneShadingPass			= CreateRef<ToneShadingPass>();
		m_ColorGradingPass			= CreateRef<ToneMappingPass>();
		

		RenderPassInfo renderpass_common_info;
		renderpass_common_info.rhi = m_RHI;
		renderpass_common_info.renderScene = spec.render_scene;

		m_MainCameraPass->SetCommonInfo(renderpass_common_info);
		m_BloomPass->SetCommonInfo(renderpass_common_info);
		m_ToneShadingPass->SetCommonInfo(renderpass_common_info);
		m_ColorGradingPass->SetCommonInfo(renderpass_common_info);
		
		m_DirectionalLightPass->Init(nullptr);

		Ref<MainCameraPass>	 mainCameraPass			= std::static_pointer_cast<MainCameraPass>(m_MainCameraPass);
		Ref<BloomPass>		 bloomPass				= std::static_pointer_cast<BloomPass>(m_BloomPass);
		Ref<ToneShadingPass> toneShadingPass		= std::static_pointer_cast<ToneShadingPass>(m_ToneShadingPass);
		Ref<ToneMappingPass> colorGradingPass		= std::static_pointer_cast<ToneMappingPass>(m_ColorGradingPass);




	}

}

