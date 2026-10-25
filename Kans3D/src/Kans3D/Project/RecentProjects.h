#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    struct RecentProjectEntry
    {
        std::filesystem::path ProjectFile;
        std::string           Name;
        int64_t               LastOpened = 0;
    };

    class RecentProjects
    {
    public:
        explicit RecentProjects(std::filesystem::path storageFile);
        [[nodiscard]] std::optional<AssetError> Load();
        [[nodiscard]] std::optional<AssetError> Save() const;
        void Touch(const std::filesystem::path& kproj, const std::string& name, int64_t now);
        void Remove(const std::filesystem::path& kproj);
        const std::vector<RecentProjectEntry>& Entries() const { return m_Entries; }
        static bool                            Exists(const RecentProjectEntry& entry);
        static std::filesystem::path           DefaultStorageFile();

    private:
        std::filesystem::path           m_StorageFile;
        std::vector<RecentProjectEntry> m_Entries;
    };
} // namespace Kans
