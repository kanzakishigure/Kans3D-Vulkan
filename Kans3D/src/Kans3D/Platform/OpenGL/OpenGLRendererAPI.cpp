#include "kspch.h"
#include "OpenGLRendererAPI.h"

#include <glad/glad.h>
namespace Kans {

	namespace Utils
	{
		
		static uint32_t To_OpenGLStencilOp(StencilOption value)
		{
			switch (value)
			{
				case Kans::StencilOption::KEEP:     return GL_KEEP;
				case Kans::StencilOption::ZERO:     return GL_ZERO;
				case Kans::StencilOption::REPLACE:	 return GL_REPLACE;
				case Kans::StencilOption::INCR:	 return GL_INCR;
				case Kans::StencilOption::INCR_WRAP:return GL_INCR_WRAP;
				case Kans::StencilOption::DECR:	 return GL_DECR;
				case Kans::StencilOption::DECR_WRAP:return GL_DECR_WRAP;
				case Kans::StencilOption::INVERT:	 return GL_INVERT;

			}
			CORE_ASSERT(false, "unknow StencilOption");
			return 0;
		}
		static uint32_t To_OpenGLCullFaceOption(CullFaceOption value)
		{
			switch (value)
			{
			case Kans::CullFaceOption::NONE:return GL_NONE;
			case Kans::CullFaceOption::BACK: return GL_BACK;
			case Kans::CullFaceOption::FRONT:return GL_FRONT;
			case Kans::CullFaceOption::FRONT_AND_BACK:return GL_FRONT_AND_BACK;
			}
			CORE_ASSERT(false, "unknow CullFaceOption");
			return 0;
		}
	}

	
	void OpenGLRendererAPI::Init()
	{
		

			PROFILE_FUCTION();
			//对于其他渲染级别的性能检测，不使用HZ_PROFILE_FUNCTION这个级别的检测，因为每次渲染执行的时候都会进行大量的复杂操作，在执行每次命令的时候，会消耗大量的时间，以及内存开销
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			//深度测试
			glEnable(GL_DEPTH_TEST);
			//模板缓冲
			glEnable(GL_STENCIL_TEST);
			//三角面剔除
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glFrontFace(GL_CCW);

			glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

			{
				//don't update depth buffer
				//glDepthMask(GL_FALSE);

				glDepthFunc(GL_LEQUAL);
				glStencilMask(0x00);
			}
			//启用cubmap插值过滤,防止cubemap面与面之间产生走样
			glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
			//启用MSAA
			glEnable(GL_MULTISAMPLE);

		
	}
	void OpenGLRendererAPI::SetClearColor(const glm::vec4& color)
	{
		glClearColor(color.r, color.b, color.g, color.a);
	}

	void OpenGLRendererAPI::Clear()
	{
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		

	}

	void OpenGLRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount /*= 0*/ )
	{
		uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
		glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	void OpenGLRendererAPI::DrawArray(const Ref<VertexArray>& vertexArray, uint32_t vertexCount /*= 0*/)
	{
		glPointSize(1.0f);
		glDrawArrays(GL_POINTS, 0, vertexCount);
	}

	void OpenGLRendererAPI::SetViewPort(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		glViewport(x, y, width, height);
	}

	void OpenGLRendererAPI::SetStencilMask(uint32_t mask)
	{
		glStencilMask(mask);
	}

	void OpenGLRendererAPI::SetStencilFunc(StencilFunction func, uint32_t value, uint32_t mask)
	{
		GLenum glfunc=GL_NEVER + (uint32_t)func;
		
		glStencilFunc(glfunc, value, mask);
	}

	void OpenGLRendererAPI::StencilOp(StencilOption sfail, StencilOption dpfail, StencilOption dppass)
	{
		glStencilOp(Utils::To_OpenGLStencilOp(sfail), Utils::To_OpenGLStencilOp(dpfail), Utils::To_OpenGLStencilOp(dppass));
	}

	void OpenGLRendererAPI::BindTexture(uint32_t texture, uint32_t slot)
	{
		glBindTextureUnit(slot, texture);
	}
//-------------NativeDraw-----------------------------------------------------------------//
	unsigned int quadVAO = 0;
	unsigned int quadVBO = -1;
	void OpenGLRendererAPI::DrawQuad()
	{
		if (quadVAO == 0)
		{
			float quadVertices[] = {
				// positions        // texture Coords
				-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
				-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
				 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
				 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
			};
			//生成vao
			glGenVertexArrays(1, &quadVAO);
			glGenBuffers(1, &quadVBO);
			glBindVertexArray(quadVAO);
			glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
			glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
		}
		glBindVertexArray(quadVAO);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glBindVertexArray(0);
	}
//-----------------------------------------------------------------------------------------------------//
	void OpenGLRendererAPI::EnableDepthTest(bool enabled)
	{
		enabled ? glEnable(GL_DEPTH_TEST): glDisable(GL_DEPTH_TEST);
	}

	void OpenGLRendererAPI::EnableSetStencil(bool enabled)
	{
		enabled ? glEnable(GL_STENCIL_TEST): glDisable(GL_STENCIL_TEST);
	}
	
		
	void OpenGLRendererAPI::EnableCullFace(bool enabled)
	{
		enabled ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
	}

	void OpenGLRendererAPI::EnableBlend(bool enabled)
	{
		enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
	}

	void OpenGLRendererAPI::CullFace(CullFaceOption option)
	{
		glCullFace(Utils::To_OpenGLCullFaceOption(option));
	}

	

	uint32_t OpenGLRendererAPI::GetError()
	{
		GLenum errorCode;
		std::string error;
		while ((errorCode = glGetError()) != GL_NO_ERROR)
		{
			
			switch (errorCode)
			{
			case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
			case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
			case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
			case GL_STACK_OVERFLOW:                error = "STACK_OVERFLOW"; break;
			case GL_STACK_UNDERFLOW:               error = "STACK_UNDERFLOW"; break;
			case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
			case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
			}
			CORE_ERROR_TAG("API_ERROR","{0}", error);
		}
		
		return errorCode;
	}

}