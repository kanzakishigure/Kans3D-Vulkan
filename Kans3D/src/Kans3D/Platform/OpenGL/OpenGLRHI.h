#pragma once
#include "Kans3D/Renderer/RHI/RHI.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>	
namespace Kans {

	class OpenGLRHI :public RHI
	{
	public:
		OpenGLRHI();
		OpenGLRHI(const Scope<Window>& window);

		virtual void Init() override;
		virtual void Shutdown() override;
		virtual void CreateSwapChain() override;
		virtual void RecreateSwapchain() override;

	private:
	static void EnsureDSAFunctionsLoaded(GLADloadproc load); 
		Window* m_WindowHandle;
		
	};

}
