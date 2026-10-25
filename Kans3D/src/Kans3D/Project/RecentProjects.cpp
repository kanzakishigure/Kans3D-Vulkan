#include "RecentProjects.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include "ProjectYaml.h"

namespace Kans
{
    namespace fs = std::filesystem;
    namespace
    {
        fs::path Identity(const fs::path& file)
        {
            std::error_code ec;
            auto            path = fs::weakly_canonical(fs::absolute(file), ec);
            return ec ? fs::absolute(file).lexically_normal() : path;
        }
    } // namespace

    RecentProjects::RecentProjects(fs::path storageFile) : m_StorageFile(std::move(storageFile)) {}

    std::optional<AssetError> RecentProjects::Load()
    {
        m_Entries.clear();
        try
        {
            if (!fs::exists(m_StorageFile))
                return std::nullopt;
            std::ifstream input(m_StorageFile);
            if (!input)
                return ProjectYaml::Error(AssetErrorCode::IoError, m_StorageFile, "Cannot read recent projects");
            const auto node = YAML::Load(input);
            if (input.bad())
                return ProjectYaml::Error(AssetErrorCode::IoError, m_StorageFile, "Cannot read recent projects");
            if (!node.IsSequence())
                return ProjectYaml::Error(
                    AssetErrorCode::InvalidData, m_StorageFile, "Expected recent projects sequence");
            for (const auto& item : node)
            {
                try
                {
                    if (!item.IsMap())
                        continue;
                    RecentProjectEntry entry {fs::u8path(item["ProjectFile"].as<std::string>()),
                                              item["Name"].as<std::string>(),
                                              item["LastOpened"].as<int64_t>()};
                    if (entry.ProjectFile.empty() || entry.Name.empty() || entry.ProjectFile.extension() != ".kproj")
                        continue;
                    entry.ProjectFile = Identity(entry.ProjectFile);
                    m_Entries.push_back(std::move(entry));
                }
                catch (const YAML::Exception&)
                {}
            }
            std::stable_sort(m_Entries.begin(), m_Entries.end(), [](const auto& a, const auto& b) {
                return a.LastOpened > b.LastOpened;
            });
            std::vector<RecentProjectEntry> unique;
            for (auto& entry : m_Entries)
                if (std::none_of(unique.begin(),
                                 unique.end(),
                                 [&](const auto& other) { return other.ProjectFile == entry.ProjectFile; }) &&
                    unique.size() < 20)
                    unique.push_back(std::move(entry));
            m_Entries = std::move(unique);
            return std::nullopt;
        }
        catch (const YAML::Exception& e)
        {
            return ProjectYaml::Error(AssetErrorCode::InvalidData, m_StorageFile, e.what());
        }
        catch (const fs::filesystem_error& e)
        {
            return ProjectYaml::Error(AssetErrorCode::IoError, m_StorageFile, e.what());
        }
    }

    std::optional<AssetError> RecentProjects::Save() const
    {
        try
        {
            if (!m_StorageFile.parent_path().empty())
                fs::create_directories(m_StorageFile.parent_path());
            YAML::Emitter out;
            out << YAML::BeginSeq;
            for (const auto& entry : m_Entries)
                out << YAML::BeginMap << YAML::Key << "ProjectFile" << YAML::Value
                    << entry.ProjectFile.generic_u8string() << YAML::Key << "Name" << YAML::Value << entry.Name
                    << YAML::Key << "LastOpened" << YAML::Value << entry.LastOpened << YAML::EndMap;
            out << YAML::EndSeq;
            return ProjectYaml::Save(m_StorageFile, out);
        }
        catch (const fs::filesystem_error& e)
        {
            return ProjectYaml::Error(AssetErrorCode::IoError, m_StorageFile, e.what());
        }
    }

    void RecentProjects::Touch(const fs::path& kproj, const std::string& name, int64_t now)
    {
        const auto path = Identity(kproj);
        Remove(path);
        m_Entries.insert(m_Entries.begin(), {path, name, now});
        if (m_Entries.size() > 20)
            m_Entries.resize(20);
    }

    void RecentProjects::Remove(const fs::path& kproj)
    {
        const auto path = Identity(kproj);
        m_Entries.erase(std::remove_if(m_Entries.begin(),
                                       m_Entries.end(),
                                       [&](const auto& entry) { return Identity(entry.ProjectFile) == path; }),
                        m_Entries.end());
    }

    bool RecentProjects::Exists(const RecentProjectEntry& entry)
    {
        std::error_code ec;
        return fs::is_regular_file(entry.ProjectFile, ec);
    }

    fs::path RecentProjects::DefaultStorageFile()
    {
#ifdef _WIN32
        if (const auto* appdata = std::getenv("APPDATA"); appdata && *appdata)
            return fs::u8path(appdata) / "Kans3D/recent-projects.yaml";
#else
        if (const auto* config = std::getenv("XDG_CONFIG_HOME"); config && *config)
            return fs::u8path(config) / "Kans3D/recent-projects.yaml";
#endif
        if (const auto* homeDirectory = std::getenv("HOME"); homeDirectory && *homeDirectory)
            return fs::u8path(homeDirectory) / ".config/Kans3D/recent-projects.yaml";
        return fs::path(".config/Kans3D/recent-projects.yaml");
    }
} // namespace Kans
