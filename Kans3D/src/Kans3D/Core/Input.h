#pragma once
#include "Kans3D/Core/Base.h"
namespace Kans {

	class Input
	{
	public:
		 static bool IsKeyPressed(int KeyCode) ;

		 static bool IsMouseButtonPressed(int button);
		 static float GetMouseX();
		 static float GetMouseY();

		 static std::pair<float, float> GetMousePosition();
	};

}