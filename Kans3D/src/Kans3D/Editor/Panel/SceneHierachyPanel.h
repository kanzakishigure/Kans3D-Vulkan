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

		virtual void onImGuiRender(bool isOpen) override;
		virtual void setSceneContext(const Ref<Scene>& context) override { m_Context = context; }
		Entity getSelectEntity() const{ return m_SelectionContext; }
	private:
		void drawEntityNode(Entity entity);
		void drawComponents(Entity entity);
	private:
		Ref<Scene> m_Context = nullptr;
		Entity m_SelectionContext;

		
	};
}