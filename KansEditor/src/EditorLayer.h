#pragma once
#include <Kans3D.h>
#include "Panels/SceneHierachyPanel.h"
#include "Panels/ContentBrowserPanel.h"

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
	private:
		OrthographicCameraController m_CameraController;
		
		Ref<SceneRenderer> m_StaticMeshRenderer;
		Ref<FrameBuffer> m_Framebuffer;
		glm::vec2 m_ViewportSize = glm::vec2(0.0f,0.0f);

		bool m_ViewportFocused= false,m_viewprotHovered = false;
		bool Switchcamera = false;
		Ref<Scene> m_ActiveScene;

		Entity m_CameraEntity;
		Entity m_SecondCameraEntity;

		//We Should Have PanelManger or such things 
		//HierachyPanel
		SceneHierachyPanel m_SceneHierachyPanel;
		//ContentBrowserPanel
		ContentBrowserPanel m_ContentBrowserPanel;
	};


}
