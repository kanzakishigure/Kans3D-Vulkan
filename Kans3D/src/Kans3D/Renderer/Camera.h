#pragma once
#include <glm/glm.hpp>

namespace Kans
{

	class Camera
	{
	public:
		//只提供projectmatrix,view matrix从transformmatrix得到
		Camera() = default;
		Camera(const glm::mat4& ProjectionMatrix);
		virtual ~Camera() = default;

		const glm::mat4& GetProjectMatrix() const { return m_ProjectionMatrix; }
		void SetProjectMatirx(const glm::mat4& ProjectionMatrix) { m_ProjectionMatrix = ProjectionMatrix; }

		//返回曝光度
		float GetExposure() const { return m_Exposure; }
		float& GetExposure() { return m_Exposure; }
		void SetExposure(float exposure) { m_Exposure = exposure; }

		//设定相机曝光度
	protected:
		glm::mat4 m_ProjectionMatrix;
		float m_Exposure = 0.8f;
	};


}