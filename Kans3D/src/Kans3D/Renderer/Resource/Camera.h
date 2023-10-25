#pragma once
#include <glm/glm.hpp>

namespace Kans
{

	class Camera
	{
	public:
		//ֻ�ṩprojectmatrix,view matrix��transformmatrix�õ�
		Camera() = default;
		Camera(const glm::mat4& ProjectionMatrix);
		virtual ~Camera() = default;

		const glm::mat4& GetProjectMatrix() const { return m_ProjectionMatrix; }
		void SetProjectMatirx(const glm::mat4& ProjectionMatrix) { m_ProjectionMatrix = ProjectionMatrix; }

		//�����ع��
		float GetExposure() const { return m_Exposure; }
		float& GetExposure() { return m_Exposure; }
		void SetExposure(float exposure) { m_Exposure = exposure; }

		//�趨����ع��
	protected:
		glm::mat4 m_ProjectionMatrix;
		float m_Exposure = 0.8f;
	};


}