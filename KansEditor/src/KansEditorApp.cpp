#include <Kans3D.h>
#include <Kans3D/Core/EntryPoint.h>
#include <Kans3D/FileSystem/FileSystem.h>

#include <cstdlib>
#include <Kans3D/Project/ProjectFile.h>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Debug/DebugLayer.h"
#include "EditorLayer.h"

class KansEditor : public Kans::Application
{
public:
    KansEditor(Kans::ApplicationSpecification spec, const std::filesystem::path& project) : Application(spec)
    {

        PushLayer(new Kans::EditorLayer(project));
        PushLayer(new Kans::DebugLayer());
    }
    ~KansEditor() {}
};
Kans::Application* Kans::createApplication(int argc, char** argv)
{
    const auto executableDir = KansFileSystem::GetExecutableDirectory();
    const auto currentDir = std::filesystem::current_path();
    const auto editorRoot = KansFileSystem::FindEditorRoot(executableDir, currentDir);
    if (!editorRoot)
    {
        CORE_ERROR("Editor root not found: required Resources/ and KansEditor.ini. Searched '{}' and '{}' and its parents.",
                   executableDir.string(), currentDir.string());
        std::exit(EXIT_FAILURE);
    }
    std::filesystem::path project;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) != "--project")
            continue;
        if (!project.empty() || i + 1 >= argc || std::string(argv[i + 1]).empty() ||
            std::string(argv[i + 1]).rfind("--", 0) == 0)
        {
            CORE_ERROR("--project requires exactly one project path");
            std::exit(EXIT_FAILURE);
        }
        project = std::filesystem::absolute(argv[++i]);
    }
    if (project.empty())
    {
        CORE_ERROR("No project specified: use --project <file.kproj or folder>");
        std::exit(EXIT_FAILURE);
    }
    std::error_code ec;
    if (std::filesystem::is_directory(project, ec))
    {
        auto found = FindProjectFile(project);
        if (const auto* error = std::get_if<AssetError>(&found))
        {
            CORE_ERROR("Cannot select project: {}", error->Message);
            std::exit(EXIT_FAILURE);
        }
        project = std::get<std::filesystem::path>(found);
    }
    if (project.extension() != ".kproj")
    {
        CORE_ERROR("Project file must have a .kproj extension: '{}'", project.string());
        std::exit(EXIT_FAILURE);
    }
    const auto unique = FindProjectFile(project.parent_path());
    if (const auto* error = std::get_if<AssetError>(&unique))
    {
        CORE_ERROR("Cannot select project: {}", error->Message);
        std::exit(EXIT_FAILURE);
    }
    auto loaded = LoadProjectFile(project);
    if (const auto* error = std::get_if<AssetError>(&loaded))
    {
        CORE_ERROR("Cannot load project: {}", error->Message);
        std::exit(EXIT_FAILURE);
    }
    const auto absoluteEditorRoot = std::filesystem::absolute(*editorRoot);
    std::filesystem::current_path(absoluteEditorRoot);

    ApplicationSpecification spec;
    spec.Name         = "Kans3D-Editor";
    spec.ConfigPath   = (absoluteEditorRoot / "KansEditor.ini").string();
    spec.Height       = 1080;
    spec.Width        = 1920;
    spec.Fullscreen   = false;
    spec.HideTitlebar = false;

    return new KansEditor(spec, project);
}
