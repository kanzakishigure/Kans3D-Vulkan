#pragma once
#include "Kans3D/Core/Layer.h"
#include "Kans3D/Core/Events/KeyEvent.h"
#include "Kans3D/Core/Events/MouseEvent.h"
#include "Kans3D/Core/Events/ApplicationEvent.h"
namespace Kans
{
	class  ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& event) override;

		void BlockEvents(bool block) { m_BlokEvents = block; }
		void Begin();
		void End();
	public:
		void SetDarkThemeColors();
	private:
		float m_Time = 0.0f;
		bool m_BlokEvents =false;
	};
	
}