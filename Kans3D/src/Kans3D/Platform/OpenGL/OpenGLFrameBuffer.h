#pragma once
#include "Kans3D/Renderer/RHI/FrameBuffer.h"

namespace Kans
{
	class OpenGLFrameBuffer :public FrameBuffer
	{
	public: 
		OpenGLFrameBuffer(FrameBufferSpecification spec);
		virtual ~OpenGLFrameBuffer();

		void Invalidata() ;

		virtual void Bind()override;
		virtual void Unbind()override;

		virtual const FrameBufferSpecification& GetSpecification() const override { return m_Specification; }

		virtual uint32_t GetColorAttachmentCount() const override;
		virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const override;
		virtual uint32_t GetDepthAttachmentRendererID() const override { return m_DepthAttachment; }

		virtual void BindTextureToColorAttachMent(Ref<TextureCube> texture, uint32_t face,uint32_t mip = 0 ,uint32_t index = 0) override;
		virtual void BindTextureToColorAttachMent(Ref<Texture2D> texture, uint32_t mip = 0, uint32_t index = 0) override;

		virtual void Resize(uint32_t width, uint32_t height) override;\
	public:
		virtual void SwapColorAttachment(uint32_t index, uint32_t attachment) const override;
	private:
		FrameBufferSpecification m_Specification;
		uint32_t m_RendererID = 0;;
		
		//FrameBufferFormat
		std::vector<FrameBufferTextureSpecification> m_ColorAttachmentSpecifications;
		FrameBufferTextureSpecification m_DepthAttachmentpecification;

		//Real FrameBuffer Attachment
		std::vector<uint32_t> m_ColorAttachments;
		std::vector<uint32_t> m_Renderbuffers;
		uint32_t m_DepthAttachment = 0;
	};
}