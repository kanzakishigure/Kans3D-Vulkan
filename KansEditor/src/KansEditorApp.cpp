#include <Kans3D.h>
#include <Kans3D/Core/EntryPoint.h>
#include <Kans3D/FileSystem/FileSystem.h>

#include <cstdlib>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Debug/DebugLayer.h"
#include "EditorLayer.h"

class KansEditor : public Kans::Application
{
public:
    KansEditor(Kans::ApplicationSpecification spec) : Application(spec)
    {

        PushLayer(new Kans::EditorLayer());
        PushLayer(new Kans::DebugLayer());
    }
    ~KansEditor() {}
};
Kans::Application* Kans::createApplication(int argc, char** argv)
{
    const auto executableDir = KansFileSystem::GetExecutableDirectory();
    const auto currentDir    = std::filesystem::current_path();
    const auto workspace     = KansFileSystem::FindWorkspace(executableDir, currentDir);
    if (!workspace)
    {
        CORE_ERROR("Editor workspace not found: required assets/, Resources/, and Project.ini. "
                   "Searched executable directory '{}' and current directory '{}' and its parents.",
                   executableDir.string(), currentDir.string());
        std::exit(EXIT_FAILURE);
    }
    std::filesystem::current_path(*workspace);

    ApplicationSpecification spec;
    spec.Name         = "Kans3D-Editor";
    spec.ConfigPath   = (*workspace / "KansEditor.ini").string();
    spec.Height       = 1080;
    spec.Width        = 1920;
    spec.Fullscreen   = false;
    spec.HideTitlebar = false;

    return new KansEditor(spec);
}
