#pragma once
#include <imgui.h>
#include "kans3D/Core/Base.h"
#include "kans3D/Core/Log.h"
#include "kans3D/Scene/Scene.h"
#include "kans3D/Scene/Entity.h"
namespace Kans
{

	class SceneHierachyPanel {

	public: 
		SceneHierachyPanel() = default;
		SceneHierachyPanel(const Ref<Scene>& scene);
		void SetContext(const Ref<Scene>& context);
		void OnImguiRender();
	private:
		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		Ref<Scene> m_Context = nullptr;
		Entity m_SelectionContext;

		
	};
}