#include "kspch.h"
#include "OpenGLContext.h"


#include <GLFW/glfw3.h>
#include <glad/glad.h>
namespace Kans {

	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
		:m_WindowHandle(windowHandle)
	{
		HZ_CORE_ASSERT(windowHandle,"���ھ��Ϊ�գ��޷�����Ⱦ�����ĵ�����")
	}
	void OpenGLContext::Init()
	{
		HZ_PROFILE_FUCTION();
		glfwMakeContextCurrent(m_WindowHandle);
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		HZ_CORE_ASSERT(status, "�޷���ʼ��Gald");
		HZ_CORE_INFO("opengl info:");
		HZ_CORE_INFO("opengl vendor:{0}", glGetString(GL_VENDOR));
		HZ_CORE_INFO("opengl render:{0}", glGetString(GL_RENDERER));
		HZ_CORE_INFO("opengl version:{0}", glGetString(GL_VERSION));
	}

	void OpenGLContext::SwapBuffers()
	{
		
		glfwSwapBuffers(m_WindowHandle);
	}


}