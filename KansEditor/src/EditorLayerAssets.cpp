#include "Kans3D/FileSystem/FileSystem.h"
#include "EditorAssetCachePath.h"
#include "EditorLayer.h"

namespace Kans
{
    void EditorLayer::OpenProject()
    {
        const auto cacheLocation = GetEditorAssetCachePath();
        if (const auto* error = std::get_if<AssetError>(&cacheLocation))
        {
            CORE_ERROR("Project cache location: {}", error->Message);
            return;
        }
        // Bootstrap from editor settings until project files are implemented.
        ProjectConfig config;
        config.Name = "KansEditor Project";
        std::error_code ec;
        config.RootDirectory = std::filesystem::current_path(ec);
        if (ec)
        {
            CORE_ERROR("Cannot resolve project root: {}", ec.message());
            return;
        }
        config.AssetDirectory = KansFileSystem::GetAssetFolder();
        ProjectOpenOptions options;
        options.SourceAssetCachePath = std::get<std::filesystem::path>(cacheLocation);
        auto opened                  = Project::Open(config, options);
        if (const auto* error = std::get_if<AssetError>(&opened))
        {
            CORE_ERROR("Cannot open project: {}", error->Message);
            return;
        }
        m_Project = std::move(std::get<std::unique_ptr<Project>>(opened));
        for (const auto& warning : m_Project->GetAssetWarnings())
            CORE_WARN("Project asset cache: {}", warning.Message);
        CORE_INFO("Opened project '{}'", m_Project->GetConfig().Name);
    }
} // namespace Kans
