#pragma once
#include "RenderPass.h"
#include "RHI/RHI.h"
namespace Kans
{
	class RenderPipline
	{
	public:
		static  Ref<RenderPipline> Create();


		virtual void Init() = 0;
		virtual void clear() = 0;

	protected:
		Ref<RHI> m_RHI;

		Ref<RenderPass> m_DebugNormalPass;
		Ref<RenderPass> m_StencilPass;
		Ref<RenderPass> m_StrokePass;
		Ref<RenderPass> m_ToneShadingPass;
	};
}