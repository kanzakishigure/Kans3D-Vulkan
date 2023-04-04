#include "kspch.h"
#include "OpenGLContext.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>
namespace Kans {

	OpenGLContext::OpenGLContext(const Scope<Window>& window)
		:m_WindowHandle(window.get())
	{
		HZ_CORE_ASSERT(m_WindowHandle,"���ھ��Ϊ�գ��޷�����Ⱦ�����ĵ�����")
	}

	OpenGLContext::OpenGLContext()
		:m_WindowHandle(nullptr)
	{
		CORE_INFO_TAG("Rnederer", "current Context set to offscreen rendering");
	}

	void OpenGLContext::Init()
	{
		HZ_PROFILE_FUCTION();

		if (m_WindowHandle != nullptr)
		{
			glfwMakeContextCurrent((GLFWwindow*)m_WindowHandle->GetNativeWindow());
			m_WindowHandle->SetVSync(m_WindowHandle->IsVSync());
		}
		
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		HZ_CORE_ASSERT(status, "�޷���ʼ��Gald");
		HZ_CORE_INFO("opengl info:");
		HZ_CORE_INFO("opengl vendor:{0}", glGetString(GL_VENDOR));
		HZ_CORE_INFO("opengl render:{0}", glGetString(GL_RENDERER));
		HZ_CORE_INFO("opengl version:{0}", glGetString(GL_VERSION));
	}
	
	void OpenGLContext::Shutdown()
	{

	}

}