#include "kspch.h"
#include "Kans3D/Input/Input.h"

#include "Kans3D/Core/Application.h"
#include <GLFW/glfw3.h>
namespace Kans{
	
	bool Input::IsKeyPressed(int keycode)
	{
		auto Window =static_cast<GLFWwindow*>( Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetKey(Window, keycode);
		return state == GLFW_PRESS||state==GLFW_REPEAT;
	}

	bool Input::IsMouseButtonPressed(int button)
	{
		auto Window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetMouseButton(Window, button);
		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}


	float Input::GetMouseX()
	{
		auto [X, Y] = GetMousePosition();
		return (float)X;
	}
	float Input::GetMouseY()
	{
		auto [X, Y] = GetMousePosition();
		return (float)Y;
	}
	//c++17新特性std::pair 返回一对数值，使用{}
	std::pair<float, float> Input::GetMousePosition()
	{
		auto Window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		double Xpos, Ypos;
		glfwGetCursorPos(Window, &Xpos, &Ypos);
		return {(float)Xpos,(float)Ypos};
	}

}