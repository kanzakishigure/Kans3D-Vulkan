#pragma once
#include "Kans3D/Renderer/RenderContext.h"
namespace Kans {

	class OpenGLContext :public RenderContext
	{
	public:
		virtual void Init() override;
		virtual void SwapBuffers() override;
		OpenGLContext(GLFWwindow* windowHandle);

	private:
		GLFWwindow* m_WindowHandle;
		
	};

}
