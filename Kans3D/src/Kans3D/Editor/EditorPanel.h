#pragma once
#include "Kans3D/Events/Event.h"
#include "Kans3D/Scene/Scene.h"
namespace Kans
{
	class EditorPanel
	{
	public:
		virtual ~EditorPanel(){}

		virtual void OnImguiRender(bool isOpen) = 0;

		virtual void OnEvent(Event& e){}
		virtual void OnProjectChanged(){}
		virtual void SetSceneContext(const Ref<Scene>& context){}

	};
}