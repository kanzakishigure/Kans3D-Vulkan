#include <Kans3D.h>
#include <Kans3D/Core/EntryPoint.h>

#include "Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Renderer/Shader.h"

#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "EditorLayer.h"

namespace Kans {

	class KansEditor : public Kans::Application
	{
	public:

		KansEditor()
		{
			//PushLayer(new ExampleLayer());
			PushLayer(new EditorLayer());
		}
		~KansEditor()
		{

		}

	};
	Kans::Application* Kans::createApplication()
	{
		return new KansEditor();
	}


}
