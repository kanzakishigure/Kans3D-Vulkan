#include <Kans3D.h>
#include <Kans3D/Core/EntryPoint.h>

#include "Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Renderer/Shader.h"

#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "EditorLayer.h"


class KansEditor : public Kans::Application
{
public:
	KansEditor(Kans::ApplicationSpecification spec)
		: Application(spec)
	{
		//PushLayer(new ExampleLayer());
		PushLayer(new Kans::EditorLayer());
	}
	~KansEditor()
	{

	}

};
	Kans::Application* Kans::createApplication(int argc, char** argv)
	{
		std::filesystem::path executable_path(argv[0]);
		std::filesystem::path config_file_path = std::filesystem::current_path() / "KansEditor.ini";

		ApplicationSpecification spec;
		spec.Name = "Kans3D-Editor";
		spec.ConfigPath = config_file_path.string();
		spec.Height = 1080;
		spec.Width = 1920;

		
		return new KansEditor(spec);
	}

