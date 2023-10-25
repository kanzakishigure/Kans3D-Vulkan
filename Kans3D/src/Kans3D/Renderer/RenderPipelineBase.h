#pragma once
#include "RenderPass.h"
#include "RenderResourceBase.h"

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
		std::shared_ptr<RenderResourceBase> render_resource;
	};
	
	class RenderPipelineBase
	{
		friend class Renderer;
	public:
		 static  Ref<RenderPipelineBase> Create() ;

		virtual void Init(RenderPipelineSpecification spec) = 0;
		virtual void clear() {}

	protected:
		Ref<RHI> m_RHI;
		Ref<RenderPipelineSpecification> m_RenderPipelineSpecification;

		Ref<RenderPass> m_MainCameraPass;
		Ref<RenderPass> m_ToneShadingPass;

		//Visual Effective
		Ref<RenderPass> m_BloomPass;
		Ref<RenderPass> m_ColorGradingPass;


		

	};
}