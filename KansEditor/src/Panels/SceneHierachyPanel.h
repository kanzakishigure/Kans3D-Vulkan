#pragma once
#include "Kans3D/Editor/EditorPanel.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Editor/EditorResources.h"
namespace Kans
{

	class SceneHierachyPanel : EditorPanel
	{

	public: 
		SceneHierachyPanel() = default;

		virtual void OnImguiRender(bool isOpen) override;
		virtual void SetSceneContext(const Ref<Scene>& context) override { m_Context = context; }

	private:
		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		Ref<Scene> m_Context = nullptr;
		Entity m_SelectionContext;

		
	};
}