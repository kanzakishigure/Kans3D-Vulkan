#include "kspch.h"
#include "OpenGLFrameBuffer.h"
#include "glad/glad.h"
namespace Kans {

	static const uint32_t MaxFramebufferSize = 15360;
	namespace Utils 
	{
		static GLenum TextureTarget(bool multisampled)
		{
			return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
		}
		static void CreateTextures(bool multisampled,uint32_t count ,uint32_t* outID)
		{	
			glCreateTextures(TextureTarget(multisampled), count, outID);
			//glBindTextures(TextureTarget(multisampled), outID,count);
		}
		static void BindTexture(bool multisampled,uint32_t id)
		{
			glBindTexture(TextureTarget(multisampled), id);

		}
		static void AttachColorTexture(uint32_t id, int samples, GLenum format, uint32_t width, uint32_t height, int index)
		{
			bool multisampled = samples > 1;
			if (multisampled)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples , format, width, height,GL_FALSE);
				
			}
			else
			{
				switch (format)
				{
				case GL_RGBA8:
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
					break;
				case GL_RGBA16F:
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
					break;
				}
				// create a Render object for depth and stencil attachment (we don't be sampling these)
				

				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				
			}
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, TextureTarget(multisampled), id, 0);	
			
		}
		static void AttachDepthTexture(uint32_t id, int samples, GLenum format, GLenum attachmentType, uint32_t width, uint32_t height)
		{
			bool multisampled = samples > 1;
			if (multisampled)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, width, height, GL_FALSE);

			}
			else
			{
				// create a Render object for depth and stencil attachment (we don't be sampling these)
				glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);

				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

			}
			glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentType, TextureTarget(multisampled), id, 0);
		}
		
		static bool IsDepthAttachment(FrameBufferTextureFormat format)
		{
			switch (format)
			{
				case FrameBufferTextureFormat::DEPTH24STENCIL8 :  return true;
			}
			return false;
		}
	}
	OpenGLFrameBuffer::OpenGLFrameBuffer(FrameBufferSpecification spec)
		:m_Specification(spec)
	{
		for (auto& format : spec.AttachmentSpecification.Attachments)
		{
			if (!Utils::IsDepthAttachment(format.TextureFormat))
			{
				m_ColorAttachmentSpecifications.emplace_back(format);
			}
			else
			{
				m_DepthAttachmentpecification = format;
			}
		}
		Invalidata();
	}


	OpenGLFrameBuffer::~OpenGLFrameBuffer()
	{
		glDeleteFramebuffers(1, &m_RendererID);
		glDeleteTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
		glDeleteTextures(1, &m_DepthAttachment);
		m_ColorAttachments.clear();
		m_DepthAttachment = 0;
	}

	void OpenGLFrameBuffer::Invalidata()
	{

		HZ_CORE_TRACE("FrameBuffer invalidata");
		if (m_RendererID != 0)
		{
			glDeleteFramebuffers(1, &m_RendererID);
			glDeleteTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
			glDeleteTextures(1, &m_DepthAttachment);
			m_ColorAttachments.clear();
			m_DepthAttachment = 0;
			
		}
		
		
		
		//生成绑定FrameBuffer
		glCreateFramebuffers(1, &m_RendererID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

		bool multisampled = m_Specification.Samples > 1;
		//Attachments
		if (m_ColorAttachmentSpecifications.size())
		{
			m_ColorAttachments.resize(m_ColorAttachmentSpecifications.size());
			Utils::CreateTextures(multisampled,m_ColorAttachments.size(), m_ColorAttachments.data());
			for (uint32_t i =0; i<m_ColorAttachmentSpecifications.size();i++)
			{
				Utils::BindTexture(multisampled, m_ColorAttachments[i]);
				switch (m_ColorAttachmentSpecifications[i].TextureFormat)
				{
				case FrameBufferTextureFormat::RGBA8: Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples, GL_RGBA8, m_Specification.Width, m_Specification.Height, i);
					break;
				case FrameBufferTextureFormat::RGBA16F: Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples, GL_RGBA16F, m_Specification.Width, m_Specification.Height, i);
					break;
				}
			}
		}
		if (m_DepthAttachmentpecification.TextureFormat != FrameBufferTextureFormat::None)
		{
			Utils::CreateTextures(multisampled, 1, &m_DepthAttachment);
			Utils::BindTexture(multisampled, m_DepthAttachment);

			switch (m_DepthAttachmentpecification.TextureFormat)
			{
			case FrameBufferTextureFormat::Depth: Utils::AttachDepthTexture(m_DepthAttachment, m_Specification.Samples, GL_DEPTH24_STENCIL8,GL_DEPTH_STENCIL_ATTACHMENT, m_Specification.Width, m_Specification.Height);
				break;
			}
		}
		
		if (m_ColorAttachments.size() > 1)
		{
			HZ_ASSERT(m_ColorAttachments.size() <= 4, "We only supprot 4 renderer target");
			//opengl only surport 4 renderer target
			GLenum buffer[4] {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
			glDrawBuffers(m_ColorAttachments.size(), buffer);

		}
		else if(m_ColorAttachments.size()==0)
		{
			glDrawBuffer(GL_NONE);
		}

		//检查状态是否完成FrameBuffer的生成
		HZ_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "FrameBuffer Incomplete!");

		//解除bind
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFrameBuffer::Bind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
		glViewport(0, 0, m_Specification.Width, m_Specification.Height);
	}

	void OpenGLFrameBuffer::Unbind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	uint32_t OpenGLFrameBuffer::GetColorAttachmentRendererID(uint32_t index /*= 0*/) const
	{
		 HZ_ASSERT(index < m_ColorAttachments.size(), "index out of the range");
		return m_ColorAttachments[index]; 
	}

	void OpenGLFrameBuffer::Resize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0||width>MaxFramebufferSize||height>MaxFramebufferSize)
		{
			HZ_WARN("try to resize framebuffer size To {0}x{1} is not allowed !", width, height);
			return;
		}
		m_Specification.Width = width;
		m_Specification.Height =height;
		Invalidata();
	}

	void OpenGLFrameBuffer::SwapColorAttachment(uint32_t index, uint32_t attachment) const
	{
		
		HZ_ASSERT(index < m_ColorAttachments.size(), "index out of the range");
		
		//We can't swapAttachment now
		
		//glDeleteTextures(1, &m_ColorAttachments[index]);
		//m_ColorAttachments.insert(m_ColorAttachments.begin()+index,attachment);
	}

}