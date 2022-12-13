#include"kspch.h"
#include"Platform/windows/WindowsWindow.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/Vulkan/VulkanContext.h"

#include"kans3D/Events/ApplicationEvent.h"
#include "Kans3D/Core/Input.h"
#include"kans3D/Events/KeyEvent.h"
#include"kans3D/Events/MouseEvent.h"
#include "Kans3D/Renderer/RendererAPI.h"
#include<glad/glad.h>
#include <imgui.h>

namespace Kans {

	static bool s_GLiFWIntialized = false;
	int WindowsWindow::s_GLFWWindowsCount = 0;

	static void GLFWErrorCallback(int error,const char* description)
	{
		HZ_CORE_ERROR("GLFW Error ({0}):{1}", error, description);
	}
	Scope<Window> Window::Create(const WindowSpecification& props)
	{
		return CreateScope<WindowsWindow>(props);
		
	}
	WindowsWindow::WindowsWindow(const WindowSpecification& props)
		:m_Specification(props)
	{
		
	}

	WindowsWindow::~WindowsWindow()
	{
		if (RendererAPI::GetAPI() == RendererAPIType::Vulkan)
		{
			m_SwapChain.Cleanup();
		}
		WindowsWindow::Shutdown();
	}

	void WindowsWindow::Init()
	{
		HZ_PROFILE_FUCTION();
		m_Data.Title = m_Specification.Title;
		m_Data.Width = m_Specification.Width;
		m_Data.Height = m_Specification.Height;

		HZ_CORE_INFO("create window [{0}] : ({1},{2})", m_Specification.Title, m_Specification.Width, m_Specification.Height);
		if (!s_GLiFWIntialized)
		{
			//TODO£∫glfwterminate on system shutdown
			HZ_PROFILE_SCOPE("glfwInit");
			
			int success = glfwInit();
			HZ_CORE_ASSERT(success, "unable init GLFW!");
			glfwSetErrorCallback(GLFWErrorCallback);
			s_GLiFWIntialized = true;
		}
		if (RendererAPI::GetAPI() == RendererAPIType::Vulkan)
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		if (m_Specification.HideTitlebar)
		{
#ifdef HZ_PLATFORM_WINDOWS
			glfwWindowHint(GLFW_TITLEBAR, false);
#else
			glfwWindowHint(GLFW_DECORATED, false);
#endif
		}

		if (m_Specification.Fullscreen)
		{
			GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

			glfwWindowHint(GLFW_DECORATED, false);
			glfwWindowHint(GLFW_RED_BITS, mode->redBits);
			glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
			glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

			m_Window = glfwCreateWindow(mode->width, mode->height, m_Data.Title.c_str(), primaryMonitor, nullptr);
		}
		else
		{
			HZ_PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow(m_Data.Width, m_Data.Height, m_Data.Title.c_str(), nullptr, nullptr);
			s_GLFWWindowsCount++;			

		}
		
		m_Context = RenderContext::Create(m_Window);
		m_Context->Init();
		if(RendererAPI::GetAPI() == RendererAPIType::Vulkan)
		{
			auto context = std::dynamic_pointer_cast<VulkanContext>(m_Context);
			m_SwapChain.Init(VulkanContext::GetInstance(), context->GetVulkanDevice());
			m_SwapChain.InitSurface(m_Window);
		}

		//Set GLFW Event Callback data
		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);
		//Set GLFW Event Callback
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window,int width,int height) 
			{

				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				data.Height = height;
				data.Width = width;
				
				WindowResizeEvent event(width,height);
				data.EventCallback(event);
				
			});
		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				WindowCloseEvent event;
				data.EventCallback(event);
			});
		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				switch (action)
				{
					case GLFW_PRESS:
					{
						WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
						KeyPressedEvent event(key,0);
						data.EventCallback(event);
					}
						break;
					case GLFW_REPEAT:
					{
						WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
						KeyPressedEvent event(key, 1);
						data.EventCallback(event);
					}
						break;
					case GLFW_RELEASE:
					{
						WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
						KeyReleasedEvent event(key);
						data.EventCallback(event);
					}
						break;
				}
			});
		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window,int button,int action,int mods) 
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				switch (action)
				{
					case GLFW_PRESS:
						{
							WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
							MouseButtonPressedEvent event(button);
							data.EventCallback(event);
						}
						break;
					case GLFW_RELEASE:
						{
							WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
							MouseButtonReleasedEvent event(button);
							data.EventCallback(event);
						}
							break;
				}
				
			
			});
		glfwSetScrollCallback(m_Window, [](GLFWwindow* window,double xoffset,double yoffset) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			MouseScrolledEvent event((float)xoffset, (float)yoffset);
			data.EventCallback(event);
			});
		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			MouseMoveEvent event((float)xpos, (float)ypos);
			data.EventCallback(event);
			});
		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			KeyTypedEvent event(keycode);
			data.EventCallback(event);
			});

		// Update window size to actual size
		{
			int width, height;
			glfwGetWindowSize(m_Window, &width, &height);
			m_Data.Width = width;
			m_Data.Height = height;
		}
		
		
	}
	void WindowsWindow::Shutdown()
	{
		HZ_PROFILE_FUCTION();
		//GLFWapi
		glfwDestroyWindow(m_Window);
		
	}
	void WindowsWindow::ProcessEvents()
	{
		HZ_PROFILE_FUCTION();
		glfwPollEvents();
		//detect the handle or other device
		Input::Update();
	}

	void WindowsWindow::SwapBuffers()
	{
		if (RendererAPI::GetAPI() == RendererAPIType::OPENGL)
		{
			m_Context->SwapBuffers();
		}
		if (RendererAPI::GetAPI() == RendererAPIType::Vulkan)
		{
			m_SwapChain;
		}
	}

	void WindowsWindow::SetVSync(bool enable)
	{
		HZ_PROFILE_FUCTION();
		//∆Ù∂Ø“Ï≤Ω
		if (enable)
			glfwSwapInterval(1);
		else
			glfwSwapInterval(0);

		m_Data.VSync = enable;
	}

	bool WindowsWindow::IsVSync() const
	{
		return m_Data.VSync;
	}
	
}