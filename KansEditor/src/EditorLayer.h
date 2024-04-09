#pragma once
#include <Kans3D.h>

#include "Kans3D/Editor/Panel/SceneHierachyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Kans3D/Editor/EditorCamera.h"
namespace Kans {
	class EditorLayer :public Kans::Layer
	{

	public:
		EditorLayer();
		virtual ~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void OnUpdate(Kans::TimeStep ts) override;
		void OnEvent(Kans::Event& e) override;
		virtual void OnImGuiRender() override;

		bool OnKeyPressedEvent(KeyPressedEvent& e);
	private:
		
		
		
		glm::vec2 m_ViewportSize = glm::vec2(0.0f,0.0f);

		bool m_ViewportFocused = false;
		bool m_viewprotHovered = false;
		bool Switchcamera	   = false;
		
		Ref<Scene> m_ActiveScene;
		Ref<RenderScene> m_ActiveRenderScene;
		Ref<SceneRenderer> m_ViewportRenderer;

		Entity m_CameraEntity;
		Entity m_SecondCameraEntity;


		//Gizmo
		int m_GizmoType = 7; // -1 = no gizmo
		int m_GizmoMode = 0; // 0 = local

		EditorCamera m_EditorCamera;
		//We Should Have PanelManger or such things 
		//HierachyPanel
		SceneHierachyPanel m_SceneHierachyPanel;
		//ContentBrowserPanel
		ContentBrowserPanel m_ContentBrowserPanel;
	};


}
