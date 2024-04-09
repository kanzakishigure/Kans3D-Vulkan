#include "kspch.h"
#include "EditorCamera.h"

#include "Kans3D/Input/Input.h"

#include <glm/gtx/quaternion.hpp>

namespace Kans
{

	EditorCamera::EditorCamera(const float degFov, const float width, const float height, const float nearP, const float farP)
		:Camera(glm::perspectiveFov(glm::radians(degFov), width , height, nearP, farP)),
		m_VerticalFOV(degFov),m_AspectRatio(width/height), 
		m_NearClip(nearP), m_FarClip(farP),
		m_ViewportWidth(width),m_ViewportHeight(height)
	{
		m_Position = { 5.0,5.0,-5.0f };
		m_FocusPosition = { 0,0,0 };
		m_Distance = glm::distance(m_Position, m_FocusPosition);
		
		m_Yaw = 3.0f * glm::pi<float>() / 4.0f;
		m_Yaw = 0;

		m_Pitch = glm::pi<float>() / 4.0f;
		m_Pitch = 0;

		m_Position = calculatePosition();
		glm::quat orientation = getOrientation();

		m_FrontDirection = glm::eulerAngles(orientation) * (180.0f / glm::pi<float>());
		m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(glm::quat(orientation));
		m_ViewMatrix = inverse(m_ViewMatrix);
	}

	void EditorCamera::onEvent( Event& e)
	{
		EventDispatcher dispatcher(e);

		dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& e) {return onMouseScroll(e);});
	}
	bool EditorCamera::onMouseScroll(MouseScrolledEvent& e)
	{
		if (Input::IsMouseButtonPressed(MouseButton::ButtonRight))
		{
			float delta = e.GetYOffset();
			m_CameraMoveSpeed += delta*m_CameraMoveSpeed*0.2f;
			m_CameraMoveSpeed = glm::clamp(m_CameraMoveSpeed, MIN_SPEED, MAX_SPEED);
		}
		else
		{
			mouseZoom(e.GetYOffset() * 0.1f);
			updateViewMatrix();
		}
		return true;
	}
	void EditorCamera::onUpdate(TimeStep ts)
	{
		glm::vec2 mousePos = { Input::GetMouseX() ,Input::GetMouseY()};
		glm::vec2 mousedelta = (mousePos - m_MousePos) *0.002f;


		
		if (Input::IsMouseButtonPressed(MouseButton::ButtonRight))
		{
			

			float up = getUpDirection().y>=0 ? 1: -1;
			
			float speed = getCameraSpeed();
			Input::SetCursorMode(CursorMode::Disable);	
			//Get CameraUp Direction
			if (Input::IsKeyPressed(KeyCode::Q))
			{
				m_MoveDelta -= glm::vec3(0, up,0) * speed *ts.GetMilliseconds();
			}
			if (Input::IsKeyPressed(KeyCode::E))
			{
				m_MoveDelta += glm::vec3(0, up, 0) * speed * ts.GetMilliseconds();
			}
			//Get Camera Front Direction
			if (Input::IsKeyPressed(KeyCode::W))
			{
				m_MoveDelta += m_FrontDirection * speed *ts.GetMilliseconds();
			}
			if (Input::IsKeyPressed(KeyCode::S))
			{
				m_MoveDelta -= m_FrontDirection * speed * ts.GetMilliseconds();
			}
			//Get the Camera Right Direction
			if (Input::IsKeyPressed(KeyCode::A))
			{
				m_MoveDelta -= m_RightDirection * speed * ts.GetMilliseconds();
			}
			if (Input::IsKeyPressed(KeyCode::D))
			{
				m_MoveDelta += m_RightDirection * speed * ts.GetMilliseconds();
			}

			float maxAngleRate = 0.12f;
			
			
			m_YawDelta += glm::clamp(up *mousedelta.x * getRotateSpeed(), -maxAngleRate, maxAngleRate);
			m_PitchDelta += glm::clamp(mousedelta.y * getRotateSpeed(), -maxAngleRate, maxAngleRate);
			

			m_RightDirection = glm::cross(m_FrontDirection, glm::vec3(0, up, 0));
			m_FrontDirection = glm::rotate(glm::normalize(glm::cross(glm::angleAxis(-m_PitchDelta, m_RightDirection),
				glm::angleAxis(-m_YawDelta, glm::vec3{ 0.f, up, 0.f }))), m_FrontDirection);
			m_FrontDirection = glm::normalize(m_FrontDirection);

			const float distance = glm::distance(m_FocusPosition, m_Position);
			m_FocusPosition = m_Position + getFrontDirection() * distance;
			
		}
		else
		{
			Input::SetCursorMode(CursorMode::Normal);
		}

		
			
		m_MousePos = mousePos;
		m_Yaw += m_YawDelta;
		m_Pitch += m_PitchDelta;
		m_Position += m_MoveDelta;
		updateViewMatrix();
	}

	

	

	void EditorCamera::updateViewMatrix()
	{
		float up = getUpDirection().y >= 0.0f ? 1.0 : -1.0;
		glm::vec3 lookatPos = m_Position + getFrontDirection();

		m_FrontDirection = glm::normalize(lookatPos - m_Position);
		m_Distance = glm::distance(m_Position, m_FocusPosition);
		
		m_ViewMatrix = glm::lookAt(m_Position, lookatPos, glm::vec3(0, up, 0));
		
		m_MoveDelta  *= 0.8f;
		m_YawDelta	 *= 0.6f;
		m_PitchDelta *= 0.6f;

	}

	glm::vec3 EditorCamera::getUpDirection()
	{

		return glm::rotate(getOrientation(), glm::vec3(0.f, 1.f, 0.f));
		
	}

	glm::vec3 EditorCamera::getRightDirection()
	{
		return glm::rotate(getOrientation(), glm::vec3(1.f, 0.f, 0.f));
	}

	glm::vec3 EditorCamera::getFrontDirection()
	{
		return glm::rotate(getOrientation(), glm::vec3(0.f, 0.f, -1.f));
	}

	glm::quat EditorCamera::getOrientation()
	{
		return glm::quat(glm::vec3( -m_Pitch - m_PitchDelta, -m_Yaw - m_YawDelta, 0));
	}

	glm::vec3 EditorCamera::calculatePosition()
	{
		return m_FocusPosition  -getFrontDirection() * m_Distance + m_MoveDelta;
	}

	float EditorCamera::getCameraSpeed()
	{
		return glm::clamp(m_CameraMoveSpeed, MIN_SPEED,MAX_SPEED);
	}

	float EditorCamera::getRotateSpeed()
	{
		return 0.3f;
	}

	float EditorCamera::getZoomSpeed()
	{
		float zoomDistance = m_Distance * 0.18f;
		zoomDistance = glm::max(zoomDistance, 0.0f);
		float speed = zoomDistance * zoomDistance;
		speed = glm::min(speed, 25.0f); 
		return speed;
	}

	void EditorCamera::mouseZoom(float delta)
	{
		
		
		CORE_INFO("zoom Speed:[{}]", delta);
		m_Distance -= delta * getZoomSpeed();
		const glm::vec3 forwardDir = getFrontDirection();
		m_Position = m_FocusPosition - forwardDir * m_Distance;
		
		if (m_Distance <= 1.0f)
		{
			m_FocusPosition += forwardDir *m_Distance;
			m_Distance = 1.0f;
		}
		m_MoveDelta += delta * getZoomSpeed() * forwardDir;
		
	}

	void EditorCamera::setViewportSize(uint32_t width, uint32_t height)
	{
		if (width == m_ViewportWidth && height == m_ViewportHeight)
		{
			return;
		}
		SetProjectionMatirx(glm::perspectiveFov(glm::radians(m_VerticalFOV), (float)width, (float)height, m_NearClip, m_FarClip));
		m_ViewportWidth = width;
		m_ViewportHeight = height;
	}

}