#pragma once
#include "Kans3D/Renderer/Resource/Camera.h"
#include "Kans3D/Core/Events/Event.h"
#include "Kans3D/Core/Events/MouseEvent.h"
#include "Kans3D/Core/TimeStep.h"

namespace Kans
{
	class EditorCamera : public Camera
	{
	public:
		EditorCamera(const float degFov, const float width, const float height, const float nearP, const float farP);
		
		 void onEvent( Event& e);
		 void onUpdate(TimeStep ts);

		 void updateViewMatrix();
		 glm::vec3 getUpDirection();
		 glm::vec3 getRightDirection();
		 glm::vec3 getFrontDirection();
		 glm::quat getOrientation();
		 glm::vec3 calculatePosition();

		 float getCameraSpeed();
		 float getRotateSpeed();
		 float getZoomSpeed();
		 bool EditorCamera::onMouseScroll(MouseScrolledEvent& e);
		 void mouseZoom(float delta);
		 void setViewportSize(uint32_t width, uint32_t height);
		 

		 glm::vec3 getPosition()const { return m_Position; }
		 const glm::mat4& getViewMatrix() const { return m_ViewMatrix; }
		 glm::mat4 getViewProjection() const { return GetProjectionMatrix() * m_ViewMatrix; }
	private:
		
		float m_VerticalFOV;
		float m_AspectRatio;
		float m_NearClip;
		float m_FarClip;

		float m_Yaw, m_Pitch;
		float m_YawDelta = {}, m_PitchDelta = {};

		float m_Distance;

		glm::mat4 m_ViewMatrix;
		glm::vec3 m_FrontDirection;
		glm::vec3 m_RightDirection;

		glm::vec3 m_Position {0.0f,1.0f,-5.0f};
		glm::vec3 m_FocusPosition{};

		glm::vec3 m_MoveDelta{};

		glm::vec2 m_MousePos {};

		uint32_t m_ViewportWidth{ 1920 }, m_ViewportHeight{ 1080 };

		float m_CameraMoveSpeed{ 0.0015f };
		constexpr static float MIN_SPEED{ 0.0001f }, MAX_SPEED{ 1.0f };
	};
}