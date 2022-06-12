#include"hzpch.h"
#include"Platform/windows/WindowsWindow.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/Vulkan/VulkanContext.h"

#include"kans3D/Events/ApplicationEvent.h"
#include"kans3D/Events/KeyEvent.h"
#include"kans3D/Events/MouseEvent.h"
#include "Kans3D/Renderer/RendererAPI.h"
#include<glad/glad.h>

namespace Kans {

	static bool s_GLiFWIntialized = false;
	int WindowsWindow::s_GLFWWindowsCount = 0;

	static void GLFWErrorCallback(int error,const char* description)
	{
		HZ_CORE_ERROR("GLFW Error ({0}):{1}", error, description);
	}
	Scope<Window> Window::Create(const WindowProps& props)
	{
		return CreateScope<WindowsWindow>(props);
		
	}
	WindowsWindow::WindowsWindow(const WindowProps& props)
	{
		Init(props);
	}

	WindowsWindow::~WindowsWindow()
	{
		
		m_SwapChain.Cleanup();
		WindowsWindow::Shutdown();
	}

	void WindowsWindow::Init(const WindowProps& props)
	{
		HZ_PROFILE_FUCTION();
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		HZ_CORE_INFO("创建窗口{0}({1},{2})", props.Title, props.Width, props.Height);
		if (!s_GLiFWIntialized)
		{
			//TODO：glfwterminate on system shutdown
			HZ_PROFILE_SCOPE("glfwInit");
			
			int success = glfwInit();
			HZ_CORE_ASSERT(success, "无法初始化GLFW!");
			glfwSetErrorCallback(GLFWErrorCallback);
			s_GLiFWIntialized = true;
		}
		if (RendererAPI::GetAPI() == RendererAPIType::Vulkan)
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		{
			HZ_PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow(m_Data.Width, m_Data.Height, m_Data.Title.c_str(), NULL, NULL);
			s_GLFWWindowsCount++;			

		}
		
		m_Context = RenderContext::Create(m_Window);
		m_Context->Init();
		if(RendererAPI::GetAPI() == RendererAPIType::Vulkan)
		{
			auto&  context = std::dynamic_pointer_cast<VulkanContext>(m_Context);
			m_SwapChain.Init(context->GetInstance(), context->GetVulkanDevice());
			m_SwapChain.InitSurface(m_Window);
		}


		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);
		//设置窗口回调函数
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
		
	}
	void WindowsWindow::Shutdown()
	{
		HZ_PROFILE_FUCTION();
		//GLFWapi
		glfwDestroyWindow(m_Window);
		
	}
	void WindowsWindow::OnUpdate()
	{
		HZ_PROFILE_FUCTION();
		//检测是否有事件触发，若有事件触发，更新状态，调用该事件的回调函数
		glfwPollEvents();
		m_Context->SwapBuffers();
		//(它是一个储存着GLFW窗口每一个像素颜色值的大缓冲），它在这一迭代中被用来绘制，并且将会作为输出显示在屏幕上。
	}
	void WindowsWindow::SetVSync(bool enable)
	{
		HZ_PROFILE_FUCTION();
		//启动异步
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