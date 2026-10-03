#include "Project.h"
#include "Kans3D/Asset/AssetScanner.h"
#include "Kans3D/Asset/SourceAssetDatabaseSerializer.h"

namespace Kans
{
    namespace
    {
        namespace fs = std::filesystem;
        bool IsWithin(const fs::path& path, const fs::path& root)
        {
            auto p = path.begin();
            for (auto r = root.begin(); r != root.end(); ++r, ++p)
                if (p == path.end() || *p != *r)
                    return false;
            return true;
        }
    } // namespace

    std::variant<std::unique_ptr<Project>, AssetError> Project::Open(const ProjectConfig&      config,
                                                                     const ProjectOpenOptions& options)
    {
        if (config.Name.empty() || !config.RootDirectory.is_absolute() || config.AssetDirectory.empty() ||
            config.CacheDirectory.empty() ||
            (!options.SourceAssetCachePath.empty() && !options.SourceAssetCachePath.is_absolute()))
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid project configuration"};
        try
        {
            auto  project           = std::unique_ptr<Project>(new Project());
            auto& resolved          = project->m_Config;
            resolved                = config;
            resolved.RootDirectory  = fs::canonical(config.RootDirectory);
            resolved.AssetDirectory = fs::canonical(resolved.RootDirectory / config.AssetDirectory);
            if (!fs::is_directory(resolved.RootDirectory) || !fs::is_directory(resolved.AssetDirectory) ||
                !IsWithin(resolved.AssetDirectory, resolved.RootDirectory))
                return AssetError {AssetErrorCode::InvalidArgument, "Asset directory must be within the project root"};
            resolved.CacheDirectory         = fs::weakly_canonical(resolved.RootDirectory / config.CacheDirectory);
            project->m_SourceAssetCachePath = fs::weakly_canonical(
                options.SourceAssetCachePath.empty() ? resolved.CacheDirectory / "SourceAssetDatabase.cache.yaml" :
                                                       options.SourceAssetCachePath);
            if (IsWithin(project->m_SourceAssetCachePath, resolved.AssetDirectory))
                return AssetError {AssetErrorCode::InvalidArgument,
                                   "Source asset cache must be outside the asset directory"};
            auto cached = SourceAssetDatabaseSerializer::Load(project->m_SourceAssetCachePath, resolved.AssetDirectory);
            if (const auto* error = std::get_if<AssetError>(&cached))
            {
                if (error->Code != AssetErrorCode::NotFound)
                    project->m_AssetWarnings.push_back(*error);
            }
            else
            {
                project->m_SourceAssetDatabase = std::move(std::get<SourceAssetDatabase>(cached));
            }
            // Never expose stale cached records: require a complete successful scan.
            auto initialized = project->ScanAndSaveAssets();
            if (const auto* error = std::get_if<AssetError>(&initialized))
                return *error;
            return project;
        }
        catch (const fs::filesystem_error& error)
        {
            return AssetError {AssetErrorCode::IoError, error.what()};
        }
    }

    std::variant<std::monostate, AssetError> Project::RefreshAssets()
    {
        m_AssetWarnings.clear();
        return ScanAndSaveAssets();
    }

    std::variant<std::monostate, AssetError> Project::ScanAndSaveAssets()
    {
        auto scanned = AssetScanner::Scan(m_Config.AssetDirectory);
        if (const auto* error = std::get_if<AssetError>(&scanned))
            return *error;
        auto& scan = std::get<AssetScanResult>(scanned);
        if (!scan.Errors.empty())
        {
            auto error = scan.Errors.front();
            for (size_t i = 1; i < scan.Errors.size(); ++i)
                error.Message += "\n" + scan.Errors[i].Message;
            return error;
        }
        m_SourceAssetDatabase = std::move(scan.Database);
        auto saved            = SaveAssetCache();
        if (const auto* error = std::get_if<AssetError>(&saved))
            m_AssetWarnings.push_back(*error);
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> Project::SaveAssetCache() const
    {
        std::error_code error;
        fs::create_directories(m_SourceAssetCachePath.parent_path(), error);
        if (error)
            return AssetError {AssetErrorCode::IoError, "Cannot create project cache directory: " + error.message()};
        return SourceAssetDatabaseSerializer::Save(
            m_SourceAssetCachePath, m_Config.AssetDirectory, m_SourceAssetDatabase);
    }
} // namespace Kans
