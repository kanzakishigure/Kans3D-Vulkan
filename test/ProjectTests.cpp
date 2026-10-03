#include <fstream>
#include <iterator>
#include <gtest/gtest.h>
#include "Kans3D/Project/Project.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;
    class ProjectTest : public ::testing::Test
    {
    protected:
        fs::path      directory;
        ProjectConfig config;
        bool          owned = false;
        void          SetUp() override
        {
            directory = fs::temp_directory_path() / ("Kans3D-Project-" + SourceAssetID::Generate().ToString());
            owned     = fs::create_directory(directory);
            ASSERT_TRUE(owned);
            fs::create_directory(directory / "assets");
            config.Name          = "Test";
            config.RootDirectory = directory;
            Write(directory / "assets/a.png", "source");
        }
        void TearDown() override
        {
            if (owned)
            {
                std::error_code ec;
                fs::remove_all(directory, ec);
            }
        }
        void Write(const fs::path& path, const std::string& text)
        {
            std::ofstream out(path);
            out << text;
            out.close();
            ASSERT_TRUE(out.good());
        }
        std::string Read(const fs::path& path)
        {
            std::ifstream in(path);
            return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        }
    };

    TEST_F(ProjectTest, OpenResolvesPathsAndPersistsWithoutChangingWorkingDirectory)
    {
        const auto cwd    = fs::current_path();
        auto       opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        const auto& project = std::get<std::unique_ptr<Project>>(opened);
        EXPECT_EQ(fs::current_path(), cwd);
        EXPECT_EQ(project->GetConfig().AssetDirectory, fs::canonical(directory / "assets"));
        EXPECT_TRUE(project->GetAssetWarnings().empty());
        EXPECT_TRUE(fs::exists(project->GetSourceAssetCachePath()));
        const auto id = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        ASSERT_TRUE(id.IsValid());
        auto reopened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(reopened));
        EXPECT_EQ(
            std::get<std::unique_ptr<Project>>(reopened)->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png")),
            id);
    }

    TEST_F(ProjectTest, CorruptOverrideCacheIsRebuiltAndReportsWarning)
    {
        ProjectOpenOptions options;
        options.SourceAssetCachePath = directory / "editor/cache.yaml";
        fs::create_directory(directory / "editor");
        Write(options.SourceAssetCachePath, "[broken");
        auto opened = Project::Open(config, options);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        const auto& project = std::get<std::unique_ptr<Project>>(opened);
        EXPECT_FALSE(project->GetAssetWarnings().empty());
        EXPECT_EQ(project->GetSourceAssetCachePath(), options.SourceAssetCachePath);
        EXPECT_NE(Read(options.SourceAssetCachePath), "[broken");
    }

    TEST_F(ProjectTest, ScanFailurePreservesDatabaseAndCache)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&      project = std::get<std::unique_ptr<Project>>(opened);
        const auto before  = Read(project->GetSourceAssetCachePath());
        const auto id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        Write(directory / "assets/a.png.kmeta", "[broken");
        EXPECT_TRUE(std::holds_alternative<AssetError>(project->RefreshAssets()));
        EXPECT_EQ(project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png")), id);
        EXPECT_EQ(Read(project->GetSourceAssetCachePath()), before);
        EXPECT_TRUE(std::holds_alternative<AssetError>(Project::Open(config)));
        EXPECT_EQ(Read(project->GetSourceAssetCachePath()), before);
    }

    TEST_F(ProjectTest, RefreshReconcilesAddedAndRemovedSources)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto& project = std::get<std::unique_ptr<Project>>(opened);
        fs::remove(directory / "assets/a.png");
        Write(directory / "assets/b.png", "new");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        EXPECT_FALSE(project->GetSourceAssetDatabase().Contains(AssetPath("a.png")));
        EXPECT_TRUE(project->GetSourceAssetDatabase().Contains(AssetPath("b.png")));
        EXPECT_TRUE(std::holds_alternative<std::monostate>(project->SaveAssetCache()));
    }

    TEST_F(ProjectTest, CacheWriteFailureDoesNotPreventOpening)
    {
        ProjectOpenOptions options;
        options.SourceAssetCachePath = directory / "blocked/cache.yaml";
        Write(directory / "blocked", "not a directory");
        auto opened = Project::Open(config, options);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        const auto& project = std::get<std::unique_ptr<Project>>(opened);
        EXPECT_FALSE(project->GetAssetWarnings().empty());
        EXPECT_TRUE(project->GetSourceAssetDatabase().Contains(AssetPath("a.png")));
        EXPECT_TRUE(std::holds_alternative<AssetError>(project->SaveAssetCache()));
    }

    TEST_F(ProjectTest, RejectsInvalidConfigurationAndCacheInsideAssets)
    {
        auto invalid = config;
        invalid.Name.clear();
        EXPECT_TRUE(std::holds_alternative<AssetError>(Project::Open(invalid)));
        invalid               = config;
        invalid.RootDirectory = "relative";
        EXPECT_TRUE(std::holds_alternative<AssetError>(Project::Open(invalid)));
        invalid                = config;
        invalid.AssetDirectory = directory.parent_path();
        EXPECT_TRUE(std::holds_alternative<AssetError>(Project::Open(invalid)));
        ProjectOpenOptions options;
        options.SourceAssetCachePath = directory / "assets/cache.yaml";
        EXPECT_TRUE(std::holds_alternative<AssetError>(Project::Open(config, options)));
        options.SourceAssetCachePath = "relative.yaml";
        EXPECT_TRUE(std::holds_alternative<AssetError>(Project::Open(config, options)));
        EXPECT_FALSE(fs::exists(directory / "assets/a.png.kmeta"));
    }
} // namespace
