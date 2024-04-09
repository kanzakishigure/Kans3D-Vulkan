#pragma once
#include "Kans3D/Core/Window.h"
#include "Kans3D/Renderer/RHI/RHI.h"
#include "Kans3D/Platform/Vulkan/VulkanSwapChain.h"



#include "kans3D/Core/Events/ApplicationEvent.h"
#include "kans3D/Core/Events/KeyEvent.h"
#include "kans3D/Core/Events/MouseEvent.h"

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
		//�ṹ
		inline void SetEventCallback(const EventCallbackFn& callback) override     { m_EventCallback = callback; }
		void SetVSync(bool enable) override;
		bool IsVSync() const override ;
		virtual inline void* GetNativeWindow() const override { return m_Window; }

	protected:

		static void windowSizeCallback(GLFWwindow* window, int width, int height)
		{
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			data.m_Specification.Height = height;
			data.m_Specification.Width = width;

			WindowResizeEvent event(width, height);
			data.m_EventCallback(event);
		}
		static void windowCloseCallback(GLFWwindow* window) {
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			WindowCloseEvent event;
			data.m_EventCallback(event);
		}
		static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			uint16_t repeatCount = 0;
			switch (action)
			{
			case GLFW_PRESS:
			{
				repeatCount = 0;
				KeyPressedEvent event((KeyCode)key, repeatCount);
				data.m_EventCallback(event);
			}
			break;
			case GLFW_REPEAT:
			{
				repeatCount = 1;
				KeyPressedEvent event((KeyCode)key, repeatCount);
				data.m_EventCallback(event);
			}
			break;
			case GLFW_RELEASE:
			{
				KeyReleasedEvent event((KeyCode)key);
				data.m_EventCallback(event);
			}
			break;
			}
		}
		static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			switch (action)
			{
			case GLFW_PRESS:
			{

				MouseButtonPressedEvent event((MouseButton)button);
				data.m_EventCallback(event);
			}
			break;
			case GLFW_RELEASE:
			{

				MouseButtonReleasedEvent event((MouseButton)button);
				data.m_EventCallback(event);
			}
			break;
			}
		}
		static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset){
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			MouseScrolledEvent event((float)xoffset, (float)yoffset);
			data.m_EventCallback(event);
		}
		static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos){
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			MouseMoveEvent event((float)xpos, (float)ypos);
			data.m_EventCallback(event);
		}
		static void charCallback(GLFWwindow* window, unsigned int codepoint) {
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			KeyTypedEvent event((KeyCode)codepoint);
			data.m_EventCallback(event);
		};
	private:
		virtual void Shutdown ();

	private:
		GLFWwindow* m_Window;
		WindowSpecification m_Specification;
		EventCallbackFn m_EventCallback;


		static int s_GLFWWindowsCount;
	

	};
}