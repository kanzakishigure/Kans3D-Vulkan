#pragma once
#include "Kans3D/Renderer/RenderPass.h"
namespace Kans
{
	class DirectionalLightShadowPass :public RenderPass 
	{
	public:
		void Init(const RenderPassSpecification* spec) override final;
		void PostInit() override final;
		void PreparePassData(Ref<RenderResourceBase> render_resource) override final;
	};
}