#pragma once
#include "Kans3D/Core/Base.h"
#include "Kans3D/Events/Event.h"
#include "Kans3D/Core/TimeStep.h"
namespace Kans{
	class  Layer
	{
	public:
		Layer(const std::string& name ="Layer" );
		virtual ~Layer();
		virtual void OnAttach(){}
		virtual void OnDetach(){}
		virtual void OnUpdate(TimeStep ts){}
		virtual void OnImGuiRender(){}
		virtual void OnEvent(Event& event){}

		inline const std::string& GetName()const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	
	};
}