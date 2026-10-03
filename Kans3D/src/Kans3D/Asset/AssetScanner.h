#pragma once

#include "AssetError.h"
#include "SourceAssetDatabase.h"

#include <filesystem>
#include <variant>
#include <vector>

namespace Kans
{

    struct AssetScanResult
    {
        SourceAssetDatabase Database;
        // Recoverable per-file errors. Messages must identify the affected path.
        std::vector<AssetError> Errors;
    };

    class AssetScanner
    {
    public:
        // Synchronous full scan; calls affecting the same root must be serialized.
        // Each result owns a fresh database. Errors contains per-file failures;
        // an outer AssetError means directory traversal could not finish.
        // Skips dotfiles, symlinks, .kmeta sidecars and .kmeta.tmp-* files, and
        // directories named Cache, Intermediate or Products (case-insensitive).
        // Created sidecars persist even if a later part of the scan fails.
        [[nodiscard]] static std::variant<AssetScanResult, AssetError> Scan(const std::filesystem::path& assetRoot);

    private:
        static std::variant<AssetScanResult, AssetError> ScanNode(const std::filesystem::path& assetRoot);
    };

} // namespace Kans
