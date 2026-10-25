#include "LauncherModel.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include "Kans3D/Project/ProjectFile.h"

namespace Kans::Launcher
{
    namespace fs = std::filesystem;
    namespace
    {
        AssetError Error(AssetErrorCode code, const fs::path& path, const std::string& message)
        {
            return {code, path.generic_u8string() + ": " + message};
        }
    } // namespace

    Selection ResolveSelection(const fs::path& path)
    {
        std::error_code ec;
        if (fs::is_directory(path, ec))
        {
            auto found = FindProjectFile(path);
            if (auto* error = std::get_if<AssetError>(&found))
            {
                if (error->Code == AssetErrorCode::NotFound)
                    return MissingProjectFile {fs::absolute(path).lexically_normal()};
                return *error;
            }
            return ResolveSelection(std::get<fs::path>(found));
        }
        if (!fs::is_regular_file(path, ec))
            return Error(AssetErrorCode::NotFound, path, "Not found");
        if (path.extension() != ".kproj")
            return Error(AssetErrorCode::InvalidArgument, path, "Not a project file (.kproj)");
        // Opening one of several project files in a folder would be ambiguous about its assets.
        auto unique = FindProjectFile(path.parent_path());
        if (auto* error = std::get_if<AssetError>(&unique))
            return *error;
        auto loaded = LoadProjectFile(path);
        if (auto* error = std::get_if<AssetError>(&loaded))
            return *error;
        return fs::absolute(path).lexically_normal();
    }

    std::variant<fs::path, AssetError> CreateProjectFile(const fs::path& folder)
    {
        const auto root = fs::absolute(folder).lexically_normal();
        auto       name = (root.has_filename() ? root.filename() : root.parent_path().filename()).u8string();
        if (!IsValidProjectName(name))
            return Error(AssetErrorCode::InvalidArgument, root, "The folder name cannot be used as a project name");
        auto existing = FindProjectFile(root);
        if (std::holds_alternative<fs::path>(existing))
            return Error(AssetErrorCode::InvalidArgument, root, "The folder already has a project file");
        ProjectFile project;
        project.Name = name;
        std::error_code ec;
        fs::create_directories(root / project.AssetDirectory, ec);
        if (ec)
            return Error(AssetErrorCode::IoError, root / project.AssetDirectory, ec.message());
        const auto file = root / fs::u8path(name + ".kproj");
        if (auto error = SaveProjectFile(file, project))
            return *error;
        return file;
    }

    std::vector<ProjectTemplate> ListTemplates(const fs::path& templatesDir)
    {
        std::vector<ProjectTemplate> templates;
        std::error_code              ec;
        for (fs::directory_iterator it(templatesDir, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_directory(ec))
                continue;
            auto found = FindProjectFile(it->path());
            if (auto* file = std::get_if<fs::path>(&found))
            {
                auto loaded = LoadProjectFile(*file);
                if (auto* project = std::get_if<ProjectFile>(&loaded))
                    templates.push_back({project->Name, it->path()});
            }
        }
        std::sort(templates.begin(), templates.end(), [](const auto& a, const auto& b) {
            if ((a.Name == "Blank") != (b.Name == "Blank"))
                return a.Name == "Blank";
            return a.Name < b.Name;
        });
        return templates;
    }

    std::optional<std::string> NewProjectProblem(const fs::path& location, const std::string& name)
    {
        if (name.empty())
            return "Enter a project name.";
        if (!IsValidProjectName(name))
            return "The name cannot contain < > : \" / \\ | ? * or control characters.";
        if (location.empty())
            return "Choose a location.";
        if (!location.is_absolute())
            return "The location must be a full path.";
        std::error_code ec;
        const auto      target = location / fs::u8path(name);
        if (fs::exists(target, ec) && (!fs::is_directory(target, ec) || !fs::is_empty(target, ec)))
            return "A non-empty folder with this name already exists.";
        if (fs::exists(location, ec) && !fs::is_directory(location, ec))
            return "The location is a file, not a folder.";
        return std::nullopt;
    }

    std::string UniqueProjectName(const fs::path& location, const std::string& base)
    {
        for (int i = 1;; ++i)
        {
            auto name = i == 1 ? base : base + std::to_string(i);
            if (!NewProjectProblem(location, name))
                return name;
            if (i > 999)
                return base;
        }
    }

    fs::path DefaultLocation(const std::vector<fs::path>& recentProjectFiles, const fs::path& home)
    {
        // A project lives in <location>/<Name>/<Name>.kproj.
        for (const auto& file : recentProjectFiles)
        {
            std::error_code ec;
            const auto      location = file.parent_path().parent_path();
            if (!location.empty() && fs::is_directory(location, ec))
                return location;
        }
        return home / "Kans3D Projects";
    }

    fs::path HomeDirectory()
    {
#ifdef _WIN32
        if (const auto* profile = std::getenv("USERPROFILE"); profile && *profile)
            return fs::u8path(profile);
#else
        if (const auto* home = std::getenv("HOME"); home && *home)
            return fs::u8path(home);
#endif
        std::error_code ec;
        return fs::current_path(ec);
    }

    std::string FormatLastOpened(int64_t now, int64_t then)
    {
        const auto elapsed = now - then;
        const auto plural  = [](int64_t count, const char* unit) {
            return std::to_string(count) + " " + unit + (count == 1 ? "" : "s") + " ago";
        };
        if (then <= 0)
            return "never";
        if (elapsed < 60)
            return "just now";
        if (elapsed < 3600)
            return plural(elapsed / 60, "minute");
        if (elapsed < 86400)
            return plural(elapsed / 3600, "hour");
        if (elapsed < 2 * 86400)
            return "yesterday";
        if (elapsed < 30 * 86400)
            return plural(elapsed / 86400, "day");
        std::time_t time = static_cast<std::time_t>(then);
        std::tm     local {};
#ifdef _WIN32
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
        return text;
    }

    LaunchArguments ParseLaunchArguments(int argc, const char* const* argv)
    {
        LaunchArguments arguments;
        for (int i = 1; i < argc; ++i)
        {
            if (std::string(argv[i]) != "--project")
                continue;
            if (!arguments.Project.empty() || i + 1 >= argc || std::string(argv[i + 1]).empty() ||
                std::string(argv[i + 1]).rfind("--", 0) == 0)
            {
                arguments.Error   = "--project requires exactly one project path";
                arguments.Project.clear();
                return arguments;
            }
            arguments.Project = fs::absolute(fs::u8path(argv[++i])).lexically_normal();
        }
        return arguments;
    }

    fs::path LayoutFile(const fs::path& projectRoot, const fs::path& cacheDirectory)
    {
        return (projectRoot / cacheDirectory / "Editor" / "imgui.ini").lexically_normal();
    }
} // namespace Kans::Launcher
