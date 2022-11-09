#include"hzpch.h"

#include "Application.h"
#include "Kans3D/Core/Log.h"
#include "Input.h"
#include "kans3D/Renderer/Renderer.h"
#include "Kans3D/Core/KeyCodes.h"

#include <filesystem>
#include <GLFW/glfw3.h>
namespace Kans
{
	#define BIND_EVENT_FN(x) std::bind(&x,this,std::placeholders::_1)
	Application* Application::s_Instance = nullptr;
	//使用在bind中使用this指针会永久的将函数this指针绑定为第一个参数
	

	Application::Application(const ApplicationSpecification& spec)
		:m_Specification(spec)
	{
		HZ_CORE_ASSERT(!s_Instance, "Application already exist!");


		if (!spec.WorkDirectory.empty())
		{
			std::filesystem::current_path(spec.WorkDirectory);
		}

		HZ_CORE_INFO("Application <{0}> is Create :\n", spec.Name);
		HZ_CORE_INFO("WorkDirectory: <{0}>\n", std::filesystem::current_path());

		HZ_PROFILE_FUCTION();
		s_Instance = this;
		RendererAPI::SetAPI(RendererAPIType::OPENGL);

		WindowSpecification windowProps(spec.Name);
		m_Window = Window::Create(windowProps);


		Renderer::Init();
		m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

		

		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);

		
	}
	Application::~Application()
	{
		Renderer::Shutdown();
	}
	void Application::run()
	{
		HZ_PROFILE_FUCTION();
		while (m_Running)
		{
			float time = (float)glfwGetTime();
			m_TimeStep = TimeStep(float(glfwGetTime()-m_LastFrameTime) );
			m_LastFrameTime = time;

			
			if (!m_Minimized)
			{
				{
					HZ_PROFILE_SCOPE("application_layerstark_update");
					//普通layer进行渲染
					for (Layer* layer : m_LayerStack)
					layer->OnUpdate(m_TimeStep);
				}
				{
					HZ_PROFILE_SCOPE("application_imgui_layerstark_update");
					//处理 ImGuiLayer渲染
					m_ImGuiLayer->Begin();
					for (Layer* layer : m_LayerStack)
					layer->OnImGuiRender();
					m_ImGuiLayer->End();
				}
			}
			m_Window->OnUpdate();

		}
	}
	void Application::OnEvent(Event& e)
	{
		HZ_PROFILE_FUCTION();
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));

		for (auto it = m_LayerStack.begin() ;it!=m_LayerStack.end(); ++it)
		{
			
			(*it)->OnEvent(e);//时间的处理从最后渲染的layer向前穿透
			if (e.Handeled)
				break;
		}
	}
	void Application::PushLayer(Layer* layer)
	{
		HZ_PROFILE_FUCTION();

		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}
	void Application::PushOverlay(Layer* overlay)
	{
		HZ_PROFILE_FUCTION();

		m_LayerStack.PushLayer(overlay);
		overlay->OnAttach();
	}
	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;

		return true;
	}
	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		HZ_PROFILE_FUCTION();

		if (e.GetHeight() == 0 || e.GetWidth() == 0)
		{
			m_Minimized = true;
			return false;
		}
		m_Minimized = false;
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());
		return false;
	}

}