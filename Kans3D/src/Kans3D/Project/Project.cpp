#include "Project.h"
#include "Kans3D/Asset/AssetScanner.h"
#include "Kans3D/Asset/SourceAssetDatabaseSerializer.h"

namespace Kans
{
    namespace
    {
        bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& root)
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
            resolved.RootDirectory  = std::filesystem::canonical(config.RootDirectory);
            resolved.AssetDirectory = std::filesystem::canonical(resolved.RootDirectory / config.AssetDirectory);
            if (!std::filesystem::is_directory(resolved.RootDirectory) ||
                !std::filesystem::is_directory(resolved.AssetDirectory) ||
                !IsWithin(resolved.AssetDirectory, resolved.RootDirectory))
                return AssetError {AssetErrorCode::InvalidArgument, "Asset directory must be within the project root"};
            resolved.CacheDirectory = std::filesystem::weakly_canonical(resolved.RootDirectory / config.CacheDirectory);
            project->m_SourceAssetCachePath = std::filesystem::weakly_canonical(
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
            auto initialized = project->ScanAndSaveAssets();
            if (const auto* error = std::get_if<AssetError>(&initialized))
                return *error;
            return project;
        }
        catch (const std::filesystem::filesystem_error& error)
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
        // Reconcile by identity, not path. A matching sidecar restores a Missing
        // record even after a move; a different ID cannot inherit its references.
        // Build the entire candidate before publishing so conflicts preserve the
        // live database and its cache, just like scan failures.
        auto candidate = std::move(scan.Database);
        for (auto previous : m_SourceAssetDatabase.GetAllSources())
        {
            if (candidate.Contains(previous.sourceAssetID))
                continue;
            if (candidate.Contains(previous.Path))
                return AssetError {AssetErrorCode::DuplicatePath,
                                   previous.Path.ToString() +
                                       ": source identity changed; resolve the old record explicitly"};
            previous.Exists  = false;
            previous.Missing = true;
            auto registered  = candidate.RegisterSource(previous);
            if (const auto* error = std::get_if<AssetError>(&registered))
                return *error;
        }
        m_SourceAssetDatabase = std::move(candidate);
        auto saved            = SaveAssetCache();
        if (const auto* error = std::get_if<AssetError>(&saved))
            m_AssetWarnings.push_back(*error);
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> Project::SaveAssetCache() const
    {
        std::error_code error;
        std::filesystem::create_directories(m_SourceAssetCachePath.parent_path(), error);
        if (error)
            return AssetError {AssetErrorCode::IoError, "Cannot create project cache directory: " + error.message()};
        return SourceAssetDatabaseSerializer::Save(
            m_SourceAssetCachePath, m_Config.AssetDirectory, m_SourceAssetDatabase);
    }
} // namespace Kans
