#pragma once
#include "Kans3D/Core/Base/Base.h"
#include "Kans3D/Renderer/Resource/Texture.h"
namespace Kans {

	enum class FrameBufferTextureFormat 
	{
		None = 0,
		
		//Color
		RGBA8,
		RGB16F,
		RGBA16F,
		RGBA32F,
		//Depth
		DEPTH24STENCIL8,

		//Defaults
		Depth = DEPTH24STENCIL8
	}; 
	enum class FrameBufferTextureWrap
	{
		None = 0,
		REPEAT,
		MIRRORED_REPEAT,
		CLAMP_TO_EDGE,
		CLAMP_TO_BORDER

	};
	enum class FrameBufferTextureFilter
	{
		None = 0,
		NEAREST_MIPMAP_NEAREST,
		LINEAR_MIPMAP_NEAREST,
		NEAREST_MIPMAP_LINEAR,
		LINEAR_MIPMAP_LINEAR
	};
	struct FrameBufferTextureSpecification
	{
		FrameBufferTextureSpecification() = default;
		FrameBufferTextureSpecification(FrameBufferTextureFormat format, FrameBufferTextureWrap wrap = FrameBufferTextureWrap::CLAMP_TO_EDGE, FrameBufferTextureFilter filter = FrameBufferTextureFilter::LINEAR_MIPMAP_NEAREST)
		:TextureFormat(format), TextureWrap(wrap),TextureFilter(filter) {}
		FrameBufferTextureFormat TextureFormat = FrameBufferTextureFormat::None;
		FrameBufferTextureWrap TextureWrap = FrameBufferTextureWrap::CLAMP_TO_EDGE;
		FrameBufferTextureFilter TextureFilter = FrameBufferTextureFilter::LINEAR_MIPMAP_NEAREST;


	};
	struct FrameBufferAttachmentSpecification
	{
		FrameBufferAttachmentSpecification() = default;
		FrameBufferAttachmentSpecification(const std::initializer_list<FrameBufferTextureSpecification> lists)
			:Attachments(lists){}
		std::vector<FrameBufferTextureSpecification> Attachments;
	};

	struct  FrameBufferSpecification
	{
		uint32_t Width, Height;
		uint32_t Samples = 1;
		FrameBufferAttachmentSpecification AttachmentSpecification;

		bool SwapChainTarget = false;
		//glBindFramebuffer(0)
		//是否将该FrameBuffer渲染到屏幕
	};
	class FrameBuffer : public RefCounter
	{
	public:
		static Ref<FrameBuffer> Create(const FrameBufferSpecification& spec);

		virtual ~FrameBuffer() = default;

		virtual const FrameBufferSpecification& GetSpecification() const = 0;
		virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;
		virtual uint32_t GetColorAttachmentCount() const = 0;
		virtual uint32_t GetDepthAttachmentRendererID() const = 0;

		
		virtual void BindTextureToColorAttachMent(Ref<TextureCube> texture,uint32_t face, uint32_t mip = 0,uint32_t index = 0) = 0;
		virtual void BindTextureToColorAttachMent(Ref<Texture2D> texture, uint32_t mip = 0,uint32_t index = 0) = 0;
		
		//Resize will invalidate the custom color attachment bind 
		virtual void Resize(uint32_t width,uint32_t height) = 0;
		
		virtual void Bind() = 0;
		virtual void Unbind() = 0;
	public:
		virtual void SwapColorAttachment(uint32_t index, uint32_t attachment) const = 0;
	private:
	};

}