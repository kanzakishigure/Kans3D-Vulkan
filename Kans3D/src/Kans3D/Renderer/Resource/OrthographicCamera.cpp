#include "kspch.h"
#include "OrthographicCamera.h"

#include <glm/gtc/matrix_transform.hpp>
namespace Kans {

	OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top)
		:m_ProjectionMatrix(glm::ortho(left,right,bottom,top)),m_ViewMatrix(glm::mat4(1.0f))
	{
		PROFILE_FUCTION();

		m_ViewProjectionMatrtix = m_ProjectionMatrix * m_ViewMatrix;
	}

	void OrthographicCamera::SetProjection(float left, float right, float bottom, float top)
	{
		PROFILE_FUCTION();

		m_ProjectionMatrix = glm::ortho(left, right, bottom, top);
		m_ViewProjectionMatrtix = m_ProjectionMatrix * m_ViewMatrix;
	}

	void OrthographicCamera::RecalculateViewMatrix()
	{
		PROFILE_FUCTION();

		 glm::mat4 transform = glm::translate(glm::mat4(1.0), m_Position)
			 *glm::rotate(glm::mat4(1.0),m_Rotation,glm::vec3(0,0,1));//��Ϊ�˴�Ϊ2dƽ�棬��z����ת����Ϊ����vec3(0,0,1)

		 m_ViewMatrix = glm::inverse(transform);//��������
		 m_ViewProjectionMatrtix = m_ProjectionMatrix * m_ViewMatrix;
	}

}