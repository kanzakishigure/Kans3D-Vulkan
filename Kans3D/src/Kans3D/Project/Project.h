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

    // 相对目录以 RootDirectory 为基准。
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
        // 留空时使用 CacheDirectory/SourceAssetDatabase.cache.yaml，否则须为绝对路径。
        std::filesystem::path SourceAssetCachePath;
    };

    // 管理项目的源资产数据库及缓存，同一项目的操作需由调用方串行执行。
    class Project
    {
    public:
        Project(const Project&)            = delete;
        Project& operator=(const Project&) = delete;
        ~Project()                         = default;

        // 校验目录并扫描源资产；缓存读写失败记录为警告。
        [[nodiscard]] static std::variant<std::unique_ptr<Project>, AssetError>
        Open(const ProjectConfig& config, const ProjectOpenOptions& options = {});

        // 重新扫描并按 ID 合并；消失的源保留为 Missing，相同 ID 恢复。
        // 扫描错误或路径身份冲突时保留现有数据库及缓存。
        [[nodiscard]] std::variant<std::monostate, AssetError> RefreshAssets();

        [[nodiscard]] std::variant<std::monostate, AssetError> SaveAssetCache() const;

        // 返回目录已解析为绝对路径的配置。
        const ProjectConfig&         GetConfig() const { return m_Config; }
        const std::filesystem::path& GetSourceAssetCachePath() const { return m_SourceAssetCachePath; }
        const SourceAssetDatabase&   GetSourceAssetDatabase() const { return m_SourceAssetDatabase; }

        // 最近一次打开或刷新过程中产生的缓存警告。
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
