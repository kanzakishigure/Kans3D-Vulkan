#pragma once
#include "Kans3D/Core/Window.h"
#include "Kans3D/Renderer/RHI/RHI.h"
#include "Kans3D/Platform/Vulkan/VulkanSwapChain.h"

#include<GLFW/glfw3.h>

namespace Kans {
	class  WindowsWindow : public Window
	{
	public:
		
		WindowsWindow(const WindowSpecification& props);
		virtual ~WindowsWindow();

		
		virtual void Init() override;
		virtual void ProcessEvents() override;
		virtual void SwapBuffers() override;

		inline unsigned int GetWidth() const override		{ return m_Specification.Width; }
		inline unsigned int GetHeight() const override		{ return m_Specification.Height; }

		virtual const WindowSpecification& GetWindowSpecification() const override { return m_Specification; }
		virtual const void SetWindowSpecification(const WindowSpecification& spec) override { m_Specification = spec; }
		//½á¹¹
		inline void SetEventCallback(const EventCallbackFn& callback) override     { m_EventCallback = callback; }
		void SetVSync(bool enable) override;
		bool IsVSync() const override ;
		virtual inline void* GetNativeWindow() const override { return m_Window; }
	private:
		virtual void Shutdown ();

	private:
		GLFWwindow* m_Window;
		WindowSpecification m_Specification;
		EventCallbackFn m_EventCallback;

	private:
		static int s_GLFWWindowsCount;
	

	};
}