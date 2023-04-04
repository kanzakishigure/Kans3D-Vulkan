#include "kspch.h"
#include "Platform/windows/WindowsWindow.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/Vulkan/VulkanContext.h"

#include "kans3D/Core/Events/ApplicationEvent.h"
#include "kans3D/Core/Events/KeyEvent.h"
#include "kans3D/Core/Events/MouseEvent.h"
#include "Kans3D/Input/Input.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Renderer/RHI/RendererAPI.h"

#include <stb_image.h>
#include <glad/glad.h>
#include <imgui.h>

namespace Kans {

	static bool s_GLiFWIntialized = false;
	int WindowsWindow::s_GLFWWindowsCount = 0;

	static void GLFWErrorCallback(int error,const char* description)
	{
		HZ_CORE_ERROR("GLFW Error ({0}):{1}", error, description);
	}
	Window* Window::Create(const WindowSpecification& props)
	{
		return new WindowsWindow(props);
		
	}
	WindowsWindow::WindowsWindow(const WindowSpecification& props)
		:m_Specification(props),m_Window(nullptr)
	{
		
	}

	WindowsWindow::~WindowsWindow()
	{
		WindowsWindow::Shutdown();
	}

	void WindowsWindow::Init()
	{
		HZ_PROFILE_FUCTION();
		

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
		if (RendererAPI::GetAPIType() == RendererAPIType::Vulkan)
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

			m_Window = glfwCreateWindow(mode->width, mode->height, m_Specification.Title.c_str(), primaryMonitor, nullptr);
		}
		else
		{
			HZ_PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow(m_Specification.Width, m_Specification.Height, m_Specification.Title.c_str(), nullptr, nullptr);
			s_GLFWWindowsCount++;			

		}
		
		//Set GLFW Event Callback data
		glfwSetWindowUserPointer(m_Window, this);
		//Set GLFW Event Callback
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window,int width,int height) 
			{

				WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
				data.m_Specification.Height = height;
				data.m_Specification.Width = width;
				
				WindowResizeEvent event(width,height);
				data.m_EventCallback(event);
				
			});
		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
			{
				WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
				WindowCloseEvent event;
				data.m_EventCallback(event);
			});
		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
				switch (action)
				{
					case GLFW_PRESS:
					{
						KeyPressedEvent event(key,0);
						data.m_EventCallback(event);
					}
						break;
					case GLFW_REPEAT:
					{
						KeyPressedEvent event(key, 1);
						data.m_EventCallback(event);
					}
						break;
					case GLFW_RELEASE:
					{
						KeyReleasedEvent event(key);
						data.m_EventCallback(event);
					}
						break;
				}
			});
		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window,int button,int action,int mods) 
			{
				WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
				switch (action)
				{
					case GLFW_PRESS:
						{
							
							MouseButtonPressedEvent event(button);
							data.m_EventCallback(event);
						}
						break;
					case GLFW_RELEASE:
						{
							
							MouseButtonReleasedEvent event(button);
							data.m_EventCallback(event);
						}
							break;
				}
				
			
			});
		glfwSetScrollCallback(m_Window, [](GLFWwindow* window,double xoffset,double yoffset) {
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			MouseScrolledEvent event((float)xoffset, (float)yoffset);
			data.m_EventCallback(event);
			});
		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos) {
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			MouseMoveEvent event((float)xpos, (float)ypos);
			data.m_EventCallback(event);
			});
		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode) {
			WindowsWindow& data = *(WindowsWindow*)glfwGetWindowUserPointer(window);
			KeyTypedEvent event(keycode);
			data.m_EventCallback(event);
			});

		// Update window size to actual size.if we use the fullscreenOption
		{
			int width, height;
			glfwGetWindowSize(m_Window, &width, &height);
			m_Specification.Width = width;
			m_Specification.Height = height;
		}
		//set the windows Icon
		
		{
			GLFWimage icon;
			int channels;
			std::filesystem::path iconpath = KansFileSystem::GetResoucesFolder() / "Icon\\Kanslogo.png";
			icon.pixels = stbi_load(iconpath.string().c_str(), &icon.width, &icon.height, &channels, 4);
			glfwSetWindowIcon(m_Window, 1, &icon);
			stbi_image_free(icon.pixels);
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
		glfwSwapBuffers(m_Window);
		//m_SwapChain.Present();
	}

	void WindowsWindow::SetVSync(bool enable)
	{
		HZ_PROFILE_FUCTION();
		//∆Ù∂Ø“Ï≤Ω
		if (enable)
			glfwSwapInterval(1);
		else
			glfwSwapInterval(0);

		m_Specification.VSync = enable;
	}

	bool WindowsWindow::IsVSync() const
	{
		return m_Specification.VSync;
	}
	
}