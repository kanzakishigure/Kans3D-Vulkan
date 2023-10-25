#pragma once
#include "RHI/RHI.h"
#include "RenderResourceBase.h"
namespace Kans
{
	
	struct RenderPassSpecification
	{

	};
	struct RenderPassInfo
	{
		Ref<RHI> rhi;
		Ref<RenderResourceBase> renderResource;

	};

	class RenderPassBase
	{

		virtual void Init(const RenderPassSpecification* spec) = 0;
		virtual void PostInit();
		virtual void UpdateSpecification(const RenderPassInfo& common_info);
		virtual void preparePassData(std::shared_ptr<RenderResourceBase> render_resource);
	protected:
		Ref<RHI> m_RHI;
		Ref<RenderResourceBase> m_RenderResource;
	};

	class RenderPass: public RenderPassBase
	{
	public:
		struct FrameBufferAttachment
		{
			
		};
		struct Framebuffer
		{
			std::vector<FrameBufferAttachment> attachments;
		};

		struct Descriptor
		{
			
		};
		struct RenderPipelineBase
		{
			
		};


		void Init(const RenderPassSpecification* spec) override;
		void PostInit() override;
		void UpdateRenderPassInfo(const RenderPassInfo& init_info);
		
		virtual void Draw();

	private:

	};
}