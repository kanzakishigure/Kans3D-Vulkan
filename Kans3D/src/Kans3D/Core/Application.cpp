#include "kspch.h"

#include "Kans3D/Core/Application.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Input/Input.h"
#include "Kans3D/Input/KeyCodes.h"
#include "kans3D/Renderer/Renderer.h"
#include "Kans3D/Script/ScriptEngine.h"
#include "Kans3D/FileSystem/FileSystem.h"

#include <GLFW/glfw3.h>
#include <filesystem>
namespace Kans
{
	#define BIND_EVENT_FN(x) std::bind(&x,this,std::placeholders::_1)
	Application* Application::s_Instance = nullptr;
	//使用在bind中使用this指针会永久的将函数this指针绑定为第一个参数
	

	Application::Application(const ApplicationSpecification& spec)
		:m_Specification(spec)
	{

		//-------------create application Surface--------------------//
		CORE_ASSERT(!s_Instance, "Application already exist!");



		KansFileSystem::Init(spec.ConfigPath);
		

		CORE_INFO("Application [{:>8}]  is Create :", spec.Name);
		HZ_PROFILE_FUCTION();
		s_Instance = this;

		
		//-------------create application Surface and init the render context-----------------//
		{
			
			WindowSpecification windowSpec;
			windowSpec.Title = spec.Name;
			windowSpec.Height = spec.Height;
			windowSpec.Width = spec.Width;
			windowSpec.Fullscreen = spec.Fullscreen;
			windowSpec.HideTitlebar = spec.HideTitlebar;
			m_Window = std::unique_ptr<Window>(Window::Create(windowSpec));
			m_Window->Init();
			m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
		}
		//-------------Init the Renderer----------------------------//
		RendererAPI::SetAPI(RendererAPIType::OPENGL);
		Renderer::Init(m_Window);
		
		//-------------Init The UI Layer------------------------//
		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);

		//-------------Init the ScriptEengine-----------------------//
		ScriptEngine::Init();
	
	}
	Application::~Application()
	{

		//we will terminate  all thread here

		m_Window->SetEventCallback([](Event& e) {});

		//clear the resource
		for (Layer* layer : m_LayerStack)
		{
			layer->OnDetach();
			delete layer;
		}

		KansFileSystem::ShutDown();
		Renderer::Shutdown();
		ScriptEngine::ShutDown();
	}
	void Application::run()
	{
		HZ_PROFILE_FUCTION();
		while (m_Running)
		{
			float time = GetTime();
			m_Frametime = TimeStep(float(glfwGetTime()-m_LastFrameTime) );
			m_TimeStep = glm::min<float>(m_Frametime, 0.0333f);
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
				m_Window->SwapBuffers();
			}
			ProcessEvents();
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
			
			(*it)->OnEvent(e);//the processing order of events depends on render order ,the last layer will handle the event fast
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

	float Application::GetTime() const
	{
		return (float)glfwGetTime();
	}

	void Application::ProcessEvents()
	{
		m_Window->ProcessEvents();
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