#include "hzpch.h"
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
			HZ_CORE_ASSERT(false, "unknow StencilOption");
			return 0;
		}
		static uint32_t To_OpenGLCullFaceOption(CullFaceOption value)
		{
			switch (value)
			{
			case Kans::CullFaceOption::BACK: return GL_BACK;
			case Kans::CullFaceOption::FRONT:return GL_FRONT;
			case Kans::CullFaceOption::FRONT_AND_BACK:return GL_FRONT_AND_BACK;

			}
			HZ_CORE_ASSERT(false, "unknow CullFaceOption");
			return 0;
		}
	}

	
	void OpenGLRendererAPI::Init()
	{
		HZ_PROFILE_FUCTION();
		//����������Ⱦ��������ܼ�⣬��ʹ��HZ_PROFILE_FUNCTION�������ļ�⣬��Ϊÿ����Ⱦִ�е�ʱ�򶼻���д����ĸ��Ӳ�������ִ��ÿ�������ʱ�򣬻����Ĵ�����ʱ�䣬�Լ��ڴ濪��
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		//��Ȳ���
		glEnable(GL_DEPTH_TEST);
		//ģ�建��
		glEnable(GL_STENCIL_TEST);
		//�������޳�
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glFrontFace(GL_CCW);
		{
			//don't update depth buffer
			//glDepthMask(GL_FALSE);
			glDepthFunc(GL_LESS);
			glStencilMask(0x00);
		}

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
	void OpenGLRendererAPI::CullFace(CullFaceOption option)
	{
		glCullFace(Utils::To_OpenGLCullFaceOption(option));
	}
}