#pragma once
#include "RenderPass.h"
#include "RenderResourceBase.h"
#include "Resource/RenderScene.h"
namespace Kans
{
	/// <summary>
	/// RenderPipline Should be handle by SceneRenderer 
	/// RenderPipline contain a render flow ,use Resources to render scene
	/// RenderResource all Render-able Entities and some relative resource
	/// </summary>
	class RHI;
	struct RenderPipelineSpecification
	{
		Ref<RenderResourceBase> render_resource;
		Ref<RenderScene> render_scene;
	};
	
	class RenderPipelineBase
	{
		friend class Renderer;
	public:
		virtual void Init(RenderPipelineSpecification spec) = 0;
		virtual void clear() {}

	protected:
		Ref<RHI> m_RHI;

		
		Ref<RenderPassBase>	m_DirectionalLightPass;
		Ref<RenderPassBase> m_PointLightShadowPass;
		Ref<RenderPassBase> m_MainCameraPass;

		Ref<RenderPassBase> m_ToneShadingPass;
		//Visual Effective
		Ref<RenderPassBase> m_BloomPass;
		Ref<RenderPassBase> m_ColorGradingPass;


		

	};
}