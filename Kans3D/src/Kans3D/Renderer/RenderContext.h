#pragma once
#include "GLFW/glfw3.h"	
namespace Kans {

	class RenderContext
	{
	public:
		virtual ~RenderContext() {};
		virtual void Init() = 0;
		virtual void SwapBuffers() = 0;
		static Ref<RenderContext> Create(GLFWwindow* windowHandle);
	private:
		
	};

}