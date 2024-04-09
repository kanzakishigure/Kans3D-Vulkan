#pragma once
#include "RHI/RHI.h"
#include "RenderResourceBase.h"
#include "Kans3D/Renderer/Resource/RenderScene.h"
#include "Kans3D/Renderer/RHI/FrameBuffer.h"
namespace Kans
{
	
	struct RenderPassSpecification
	{

	};
	struct RenderPassInfo
	{
		Ref<RHI> rhi;
		Ref<RenderScene> renderScene;

	};

	class RenderPassBase
	{
	public:
		virtual void Init(const RenderPassSpecification* spec) = 0;
		virtual void PostInit();
		virtual void SetCommonInfo(const RenderPassInfo& common_info);
		virtual void PreparePassData(Ref<RenderResourceBase> render_resource);
	protected:
		Ref<RHI> m_RHI;
		Ref<RenderScene> m_RenderScene;
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
		
		virtual void Draw();

		Framebuffer GetFrameBuffer();

	private:
		GlobalRenderResource* m_global_render_resource{ nullptr }; 
	};
}