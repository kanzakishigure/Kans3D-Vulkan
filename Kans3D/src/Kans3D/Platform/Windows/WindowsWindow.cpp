#include "kspch.h"
#include "kans3D/Platform/Windows/WindowsWindow.h"
#include "kans3D/Platform/OpenGL/OpenGLRHI.h"
#include "kans3D/Platform/Vulkan/VulkanRHI.h"

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
		CORE_ERROR("GLFW Error ({0}):{1}", error, description);
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
		PROFILE_FUCTION();
		

		CORE_INFO("create window [{0}] : ({1},{2})", m_Specification.Title, m_Specification.Width, m_Specification.Height);
		if (!s_GLiFWIntialized)
		{
			//TODO£∫glfwterminate on system shutdown
			PROFILE_SCOPE("glfwInit");
			
			int success = glfwInit();
			CORE_ASSERT(success, "unable init GLFW!");
			glfwSetErrorCallback(GLFWErrorCallback);
			s_GLiFWIntialized = true;
		}
		if (RendererAPI::Current() == RendererAPIType::Vulkan)
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		if (m_Specification.HideTitlebar)
		{
#ifdef PLATFORM_WINDOWS
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
			PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow(m_Specification.Width, m_Specification.Height, m_Specification.Title.c_str(), nullptr, nullptr);
			s_GLFWWindowsCount++;			

		}
		
		//Set GLFW Event Callback data
		glfwSetWindowUserPointer(m_Window, this);
		//Set GLFW Event Callback
		glfwSetWindowSizeCallback(m_Window, windowSizeCallback);
		glfwSetWindowCloseCallback(m_Window, windowCloseCallback);
		glfwSetKeyCallback(m_Window, keyCallback);
		glfwSetScrollCallback(m_Window, scrollCallback);
		glfwSetCursorPosCallback(m_Window, cursorPosCallback);
		glfwSetCharCallback(m_Window, charCallback);
		

		// Update window size to actual size.if we use the fullscreenOption
		if(false)
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
		PROFILE_FUCTION();
		//GLFWapi
		glfwDestroyWindow(m_Window);
		
	}
	void WindowsWindow::ProcessEvents()
	{
		PROFILE_FUCTION();
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
		PROFILE_FUCTION();
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