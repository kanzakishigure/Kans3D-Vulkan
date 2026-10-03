#pragma once

#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Asset/SourceAssetDatabase.h"

#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace Kans
{

    // In-memory configuration only; the project file format is not defined yet.
    // Relative directories are resolved against RootDirectory, never the process
    // working directory. Opening a project must not change the working directory.
    class ProjectConfig
    {
    public:
        std::string           Name;
        std::filesystem::path RootDirectory;
        std::filesystem::path AssetDirectory = "assets";
        std::filesystem::path CacheDirectory = "Cache";
    };

    class ProjectOpenOptions
    {
    public:
        // Empty: use <resolved CacheDirectory>/SourceAssetDatabase.cache.yaml.
        // Otherwise must be absolute. The editor can temporarily supply a path next
        // to its executable; Project itself must not depend on executable discovery.
        std::filesystem::path SourceAssetCachePath;
    };

    // Project-scoped source asset state, not a runtime resource loader.
    // The editor owns the opened Project; there is no global active-project state.
    // Synchronous API: opening/refreshing/saving the same project is serialized by
    // the caller. Cache files must be outside the source asset directory.
    class Project
    {
    public:
        Project(const Project&)            = delete;
        Project& operator=(const Project&) = delete;
        ~Project()                         = default;

        // Validate a nonempty name, an absolute existing root, and an existing asset
        // directory contained in that root after canonicalization. Resolve all paths
        // once; create the cache directory as needed (failure is a cache warning).
        // Load the cache, then scan sources and .kmeta before exposing the database.
        // Missing/incompatible/corrupt caches are rebuildable, not fatal. Cache I/O
        // failures are warnings; traversal or per-file scan failures fail Open.
        // A failed Open may still have created .kmeta files through AssetScanner.
        [[nodiscard]] static std::variant<std::unique_ptr<Project>, AssetError>
        Open(const ProjectConfig& config, const ProjectOpenOptions& options = {});

        // Scan into a temporary database; replace the current database only when the
        // whole scan succeeds, then save its cache. Scan failure preserves the prior
        // database and disk cache; cache-save failure keeps the fresh database and
        // records a warning. A failed scan can still have created .kmeta sidecars.
        [[nodiscard]] std::variant<std::monostate, AssetError> RefreshAssets();

        // Explicit persistence/retry. Failure preserves the previous disk snapshot.
        [[nodiscard]] std::variant<std::monostate, AssetError> SaveAssetCache() const;

        // Config contains resolved absolute directories and is immutable after Open.
        const ProjectConfig&         GetConfig() const { return m_Config; }
        const std::filesystem::path& GetSourceAssetCachePath() const { return m_SourceAssetCachePath; }
        const SourceAssetDatabase&   GetSourceAssetDatabase() const { return m_SourceAssetDatabase; }

        // Nonfatal cache diagnostics from the most recent Open/RefreshAssets attempt.
        // SaveAssetCache reports its error directly rather than modifying warnings.
        const std::vector<AssetError>& GetAssetWarnings() const { return m_AssetWarnings; }

    private:
        Project() = default;
        std::variant<std::monostate, AssetError> ScanAndSaveAssets();

        ProjectConfig           m_Config;
        std::filesystem::path   m_SourceAssetCachePath;
        SourceAssetDatabase     m_SourceAssetDatabase;
        std::vector<AssetError> m_AssetWarnings;
    };

} // namespace Kans
