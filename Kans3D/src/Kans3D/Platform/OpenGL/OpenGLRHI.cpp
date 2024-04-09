#include "kspch.h"
#include "OpenGLRHI.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>
namespace Kans {

	OpenGLRHI::OpenGLRHI(const Scope<Window>& window)
		:m_WindowHandle(window.get())
	{
		CORE_ASSERT(m_WindowHandle,"窗口句柄为空，无法绑定渲染上下文到窗口")
	}

	OpenGLRHI::OpenGLRHI()
		:m_WindowHandle(nullptr)
	{
		CORE_INFO_TAG("Rnederer", "current Context set to offscreen rendering");
	}

	void OpenGLRHI::Init()
	{
		PROFILE_FUCTION();

		if (m_WindowHandle != nullptr)
		{
			glfwMakeContextCurrent((GLFWwindow*)m_WindowHandle->GetNativeWindow());
			m_WindowHandle->SetVSync(m_WindowHandle->IsVSync());
		}
		
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		CORE_ASSERT(status, "无法初始化Gald");
		CORE_INFO("opengl info:");
		CORE_INFO("opengl vendor:{0}", glGetString(GL_VENDOR));
		CORE_INFO("opengl render:{0}", glGetString(GL_RENDERER));
		CORE_INFO("opengl version:{0}", glGetString(GL_VERSION));
	}
	
	void OpenGLRHI::Shutdown()
	{

	}

	void OpenGLRHI::CreateSwapChain()
	{

	}

	void OpenGLRHI::RecreateSwapchain()
	{
		
	}

}