#pragma once
#include "Kans3D/Core/Base.h"
#include "Kans3D/Events/Event.h"
#include "Kans3D/Events/ApplicationEvent.h"
#include "Kans3D/Core/LayerStack.h"
#include "Window.h"
#include "Kans3D//ImGui/ImGuiLayer.h"
#include "Kans3D/Core/TimeStep.h"
namespace Kans{


	class  Application
	{
		public:
			Application();

			//��֤application�����������ʱ���ܵ��õ������������������
			virtual ~Application();

			void run();
			void OnEvent(Event& e);
			void PushLayer(Layer* layer);
			void PushOverlay(Layer* overlay);
			void Close() { m_Running = false; }
			//���ص���application�ĵ��������Բ�Ӧ�ý�ָ�뷵��
			inline static Application& Get() { return *s_Instance; }
			inline  Window& GetWindow() { return *m_Window; }
	public:
		ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }
	private:
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);
	private:
		Scope<Window> m_Window;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running =true;
		bool m_Minimized = false;

		LayerStack m_LayerStack;
		static Application* s_Instance;
		TimeStep m_TimeStep;
		float m_LastFrameTime=0.0f;
	private:
		
	};

	Application* createApplication();//�ú���ֻӦ��Ӧ�ö�ʵ��


}

