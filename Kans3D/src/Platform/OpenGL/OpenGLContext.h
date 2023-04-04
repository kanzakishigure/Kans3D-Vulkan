#pragma once
#include "Kans3D/Renderer/RHI/RenderContext.h"
#include <GLFW/glfw3.h>
namespace Kans {

	class OpenGLContext :public RenderContext
	{
	public:
		OpenGLContext();
		OpenGLContext(const Scope<Window>& window);

		virtual void Init() override;
		virtual void Shutdown() override;
		

	private:
		Window* m_WindowHandle;
		
	};

}
