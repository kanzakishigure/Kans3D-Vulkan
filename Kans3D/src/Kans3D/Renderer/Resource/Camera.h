#pragma once
#include <glm/glm.hpp>

namespace Kans
{

	class Camera
	{
	public:
		
		Camera() = default;
		Camera(const glm::mat4& ProjectionMatrix);
		virtual ~Camera() = default;

		const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
		void SetProjectionMatirx(const glm::mat4& ProjectionMatrix) { m_ProjectionMatrix = ProjectionMatrix; }
		void SetPerspectiveProjectionMatrix(const float radFov, const float width, const float height, const float nearP, const float farP);
		
		
		float GetExposure() const { return m_Exposure; }
		float& GetExposure() { return m_Exposure; }
		void SetExposure(float exposure) { m_Exposure = exposure; }

	protected:
		glm::mat4 m_ProjectionMatrix;
		float m_Exposure = 0.8f;
	};


}