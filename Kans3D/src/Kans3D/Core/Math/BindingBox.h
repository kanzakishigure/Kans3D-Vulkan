#pragma once
#include <glm/glm.hpp>
namespace Kans
{
	enum class BindboxType
	{
		AABB = 0, Sphere
	};
	class BindBox
	{
	public:
		BindBox(glm::vec3 min ,glm::vec3 max)
			:Min(min), Max(max){}
		BindBox()
			:Min(0.0f), Max(0.0f){}
		glm::vec3 Min, Max;
		BindboxType type = BindboxType::AABB;
	};

	class AABB :BindBox
	{

	};
}