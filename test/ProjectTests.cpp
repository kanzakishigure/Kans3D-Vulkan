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
        auto&      project  = std::get<std::unique_ptr<Project>>(opened);
        const auto id       = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        const auto original = project->GetSourceAssetDatabase().FindByID(id);
        fs::remove(directory / "assets/a.png");
        Write(directory / "assets/b.png", "new");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        EXPECT_EQ(project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png")), id);
        const auto missing = project->GetSourceAssetDatabase().FindByID(id);
        EXPECT_FALSE(missing.Exists);
        EXPECT_TRUE(missing.Missing);
        EXPECT_EQ(missing.FileSize, original.FileSize);
        EXPECT_EQ(missing.last_edit_time, original.last_edit_time);
        EXPECT_TRUE(project->GetSourceAssetDatabase().Contains(AssetPath("b.png")));
        EXPECT_TRUE(std::holds_alternative<std::monostate>(project->SaveAssetCache()));
    }

    TEST_F(ProjectTest, MissingIdentitySurvivesReopenAndMatchingSidecarRestoresIt)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&      project = std::get<std::unique_ptr<Project>>(opened);
        const auto id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        fs::remove(directory / "assets/a.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        auto reopened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(reopened));
        auto& restored = std::get<std::unique_ptr<Project>>(reopened);
        EXPECT_TRUE(restored->GetSourceAssetDatabase().FindByID(id).Missing);
        Write(directory / "assets/a.png", "restored content");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(restored->RefreshAssets()));
        const auto record = restored->GetSourceAssetDatabase().FindByID(id);
        EXPECT_TRUE(record.Exists);
        EXPECT_FALSE(record.Missing);
        EXPECT_EQ(record.FileSize, 16u);
        EXPECT_EQ(restored->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png")), id);
    }

    TEST_F(ProjectTest, OpenDetectsOfflineDeletionAndRestoresMovedMissingIdentity)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&      project = std::get<std::unique_ptr<Project>>(opened);
        const auto id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        project.reset();
        fs::remove(directory / "assets/a.png");
        auto reopened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(reopened));
        auto& restored = std::get<std::unique_ptr<Project>>(reopened);
        EXPECT_TRUE(restored->GetSourceAssetDatabase().FindByID(id).Missing);
        fs::rename(directory / "assets/a.png.kmeta", directory / "assets/moved.png.kmeta");
        Write(directory / "assets/moved.png", "returned");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(restored->RefreshAssets()));
        EXPECT_EQ(restored->GetSourceAssetDatabase().FindIDByPath(AssetPath("moved.png")), id);
        EXPECT_FALSE(restored->GetSourceAssetDatabase().Contains(AssetPath("a.png")));
        EXPECT_FALSE(restored->GetSourceAssetDatabase().FindByID(id).Missing);
    }

    TEST_F(ProjectTest, MoveWithSidecarPreservesIdentityAndReleasesOldPath)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&      project = std::get<std::unique_ptr<Project>>(opened);
        const auto id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        fs::rename(directory / "assets/a.png", directory / "assets/moved.png");
        fs::rename(directory / "assets/a.png.kmeta", directory / "assets/moved.png.kmeta");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        EXPECT_FALSE(project->GetSourceAssetDatabase().Contains(AssetPath("a.png")));
        EXPECT_EQ(project->GetSourceAssetDatabase().FindIDByPath(AssetPath("moved.png")), id);
        EXPECT_FALSE(project->GetSourceAssetDatabase().FindByID(id).Missing);
        EXPECT_EQ(project->GetSourceAssetDatabase().GetAllSources().size(), 1u);
    }

    TEST_F(ProjectTest, DifferentIdentityAtMissingPathPreservesDatabaseAndCache)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&      project = std::get<std::unique_ptr<Project>>(opened);
        const auto id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        fs::remove(directory / "assets/a.png");
        fs::remove(directory / "assets/a.png.kmeta");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        const auto cache = Read(project->GetSourceAssetCachePath());
        Write(directory / "assets/a.png", "different source");
        const auto result = project->RefreshAssets();
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::DuplicatePath);
        EXPECT_EQ(project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png")), id);
        EXPECT_TRUE(project->GetSourceAssetDatabase().FindByID(id).Missing);
        EXPECT_EQ(Read(project->GetSourceAssetCachePath()), cache);
        EXPECT_TRUE(std::holds_alternative<AssetError>(Project::Open(config)));
        EXPECT_EQ(Read(project->GetSourceAssetCachePath()), cache);
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
