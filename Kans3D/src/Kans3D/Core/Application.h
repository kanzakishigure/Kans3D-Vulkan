#pragma once
#include "Kans3D/Core/Base.h"
#include "Kans3D/Core/Window.h"
#pragma once
#include "Kans3D/Core/LayerStack.h"
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Events/Event.h"
#include "Kans3D/Events/ApplicationEvent.h"
#include "Kans3D/ImGui/ImGuiLayer.h"
namespace Kans{


	struct ApplicationSpecification
	{
		std::string Name = "Kans3D";
		std::string ConfigPath;


		//the Windows Specification
		uint32_t  Width = 1920;
		uint32_t Height = 1080;
		bool Fullscreen = false;
		bool HideTitlebar = true;
	};

	class  Application
	{
		public:
			Application(const ApplicationSpecification& spec);

			//保证application在派生类调用时，能调用到派生类的虚析构函数
			virtual ~Application();

			void run();
			void OnEvent(Event& e);
			void PushLayer(Layer* layer);
			void PushOverlay(Layer* overlay);
			void Close() { m_Running = false; }

			//返回的是application的单例，所以不应该将指针返回
			inline static Application& Get() { return *s_Instance; }
			inline  Window& GetWindow() { return *m_Window; }

			const ApplicationSpecification& GetSpecification() const { return m_Specification; }

			float GetTime() const;
			TimeStep GetTimestep() const { return m_TimeStep; }
			TimeStep GetFrametime() const { return m_Frametime; }
			
			
	public:
		ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }
	private:
		void ProcessEvents();
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);
	private:
		ApplicationSpecification m_Specification;

		Scope<Window> m_Window;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running =true;
		bool m_Minimized = false;

		LayerStack m_LayerStack;
		static Application* s_Instance;

		TimeStep m_TimeStep;
		TimeStep m_Frametime;

		float m_LastFrameTime=0.0f;
	private:
		
	};

	Application* createApplication(int argc, char** argv);//该函数只应在应用端实现


}

