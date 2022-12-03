#include "kspch.h"
#include "kans3D/Renderer/OrthographicCameraController.h"
#include "Kans3D/Core/Input.h"
#include "Kans3D/Core/KeyCodes.h"
namespace Kans {

	//m_Camera left right bottom top
	OrthographicCameraController::OrthographicCameraController(float aspecRatio, bool Rotation /* = false*/)
		:m_AspecRatio(aspecRatio), m_Bounds({ -m_AspecRatio * m_ZoomLevel, m_AspecRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel}), m_Camera(-m_AspecRatio * m_ZoomLevel, m_AspecRatio* m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel), m_Rotation(Rotation)
	{

	}



	void OrthographicCameraController::OnUpdate(TimeStep ts)
	{
		HZ_PROFILE_FUCTION();

		if (Input::IsKeyPressed(HZ_KEY_W))
			m_CameraPos.y += m_CameraTranslationSpeed * ts;
		if (Input::IsKeyPressed(HZ_KEY_S))
			m_CameraPos.y -= m_CameraTranslationSpeed * ts;
		if (Input::IsKeyPressed(HZ_KEY_A))
			m_CameraPos.x -= m_CameraTranslationSpeed * ts;
		if (Input::IsKeyPressed(HZ_KEY_D))
			m_CameraPos.x += m_CameraTranslationSpeed * ts;

		if (m_Rotation)
		{
			if (Input::IsKeyPressed(HZ_KEY_Q))
				m_CameraRotation += m_CameraRotationSpeed * ts;
			if (Input::IsKeyPressed(HZ_KEY_E))
				m_CameraRotation -= m_CameraRotationSpeed * ts;
			m_Camera.SetRotation(m_Rotation);
		}
		m_Camera.SetPosition(m_CameraPos);
		
		
	}

	void OrthographicCameraController::OnEvent(Event& e)
	{
		HZ_PROFILE_FUCTION();

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(HZ_BIND_EVENT_FN(OrthographicCameraController::OnMouseScrolled));
		dispatcher.Dispatch<WindowResizeEvent>(HZ_BIND_EVENT_FN(OrthographicCameraController::OnWindowResize));

	}

	void OrthographicCameraController::OnResize(uint32_t width,uint32_t height)
	{
		m_AspecRatio = (float)width/ height;
		m_Camera.SetProjection(-m_AspecRatio * m_ZoomLevel, m_AspecRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
		m_Bounds = { -m_AspecRatio * m_ZoomLevel, m_AspecRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel };
		
	}

	bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		HZ_PROFILE_FUCTION();

		m_ZoomLevel -= (float)e.GetYOffset()*0.25;
		m_ZoomLevel = std::max(m_ZoomLevel,0.25f);
		m_Bounds = { -m_AspecRatio * m_ZoomLevel, m_AspecRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel };
		m_Camera.SetProjection(-m_AspecRatio * m_ZoomLevel, m_AspecRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
		return false;
	}

	bool OrthographicCameraController::OnWindowResize(WindowResizeEvent& e)
	{
		HZ_PROFILE_FUCTION();

		OnResize(e.GetWidth(), e.GetHeight());
		
		return false;
	}

}