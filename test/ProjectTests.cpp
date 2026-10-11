#include <fstream>
#include <iterator>
#include <gtest/gtest.h>
#include "Kans3D/Asset/SourceAssetMetaSerializer.h"
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

    TEST_F(ProjectTest, ConfigurationPersistsAcrossRefreshReopenAndMissingHistory)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&                     project = std::get<std::unique_ptr<Project>>(opened);
        const auto                id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        SourceImportConfiguration settings;
        settings.Importer = "TextureImporter";
        settings.Settings = {{"usage", "normal"}, {"generate_mips", "false"}};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->SetSourceImportConfiguration(id, settings)));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        EXPECT_EQ(project->GetSourceAssetDatabase().FindByID(id).Import.Settings, settings.Settings);
        fs::remove(directory / "assets/a.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        project.reset(); // A project is locked while open.
        auto reopened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(reopened));
        auto& restored = std::get<std::unique_ptr<Project>>(reopened);
        EXPECT_TRUE(restored->GetSourceAssetDatabase().FindByID(id).Missing);
        EXPECT_EQ(restored->GetSourceAssetDatabase().FindByID(id).Import.Settings, settings.Settings);
        EXPECT_TRUE(std::holds_alternative<AssetError>(restored->SetSourceImportConfiguration(id, {})));
        fs::rename(directory / "assets/a.png.kmeta", directory / "assets/moved.png.kmeta");
        Write(directory / "assets/moved.png", "restored");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(restored->RefreshAssets()));
        EXPECT_EQ(restored->GetSourceAssetDatabase().FindIDByPath(AssetPath("moved.png")), id);
        EXPECT_EQ(restored->GetSourceAssetDatabase().FindByID(id).Import.Settings, settings.Settings);
    }

    // Two projects opened by the same process must not share a source asset
    // cache: a shared, executable-relative cache let each overwrite the other's.
    TEST_F(ProjectTest, DefaultSourceAssetCacheIsPerProject)
    {
        const fs::path otherRoot = directory.parent_path() / (directory.filename().string() + "_other");
        fs::remove_all(otherRoot);
        fs::create_directories(otherRoot / "assets");
        Write(otherRoot / "assets/b.png", "other");
        ProjectConfig other = config;
        other.RootDirectory = otherRoot;

        auto first  = Project::Open(config);
        auto second = Project::Open(other);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(first));
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(second));
        const auto firstCache  = std::get<std::unique_ptr<Project>>(first)->GetSourceAssetCachePath();
        const auto secondCache = std::get<std::unique_ptr<Project>>(second)->GetSourceAssetCachePath();
        EXPECT_EQ(firstCache.parent_path(), fs::weakly_canonical(directory / config.CacheDirectory));
        EXPECT_EQ(secondCache.parent_path(), fs::weakly_canonical(otherRoot / config.CacheDirectory));
        // Each cache still describes its own project after both are open.
        EXPECT_NE(Read(firstCache).find("a.png"), std::string::npos);
        EXPECT_EQ(Read(firstCache).find("b.png"), std::string::npos);
        EXPECT_NE(Read(secondCache).find("b.png"), std::string::npos);
        second = std::unique_ptr<Project>();
        fs::remove_all(otherRoot);
    }

    TEST_F(ProjectTest, ConfigurationFailurePreservesDatabaseCacheAndSidecar)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&                     project = std::get<std::unique_ptr<Project>>(opened);
        const auto                id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        const auto                sidecar = directory / "assets/a.png.kmeta";
        const auto                before  = Read(sidecar);
        const auto                cache   = Read(project->GetSourceAssetCachePath());
        SourceImportConfiguration invalid;
        invalid.Settings[""] = "value";
        EXPECT_TRUE(std::holds_alternative<AssetError>(project->SetSourceImportConfiguration(id, invalid)));
        EXPECT_EQ(Read(sidecar), before);
        Write(sidecar, "version: 1\nsource_id: fedcba9876543210\n");
        const auto changed = Read(sidecar);
        EXPECT_TRUE(std::holds_alternative<AssetError>(project->SetSourceImportConfiguration(id, {})));
        EXPECT_EQ(Read(sidecar), changed);
        EXPECT_EQ(Read(project->GetSourceAssetCachePath()), cache);
        EXPECT_TRUE(project->GetSourceAssetDatabase().FindByID(id).Import.Settings.empty());
    }

#if defined(_WIN32)
    TEST_F(ProjectTest, LockedSidecarCommitFailurePreservesLiveConfiguration)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&      project = std::get<std::unique_ptr<Project>>(opened);
        const auto id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        const auto sidecar = directory / "assets/a.png.kmeta";
        const auto before  = Read(sidecar);
        const auto cache   = Read(project->GetSourceAssetCachePath());
        // MSVC file streams share reading/writing but deny deleting/replacing.
        std::ifstream locked(sidecar, std::ios::binary);
        ASSERT_TRUE(locked.good());
        SourceImportConfiguration settings;
        settings.Settings["usage"] = "normal";
        const auto result          = project->SetSourceImportConfiguration(id, settings);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::IoError);
        EXPECT_EQ(Read(sidecar), before);
        EXPECT_EQ(Read(project->GetSourceAssetCachePath()), cache);
        EXPECT_TRUE(project->GetSourceAssetDatabase().FindByID(id).Import.Settings.empty());
        locked.close();
        for (const auto& entry : fs::directory_iterator(directory / "assets"))
            EXPECT_EQ(entry.path().filename().u8string().find(".tmp-"), std::string::npos);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->SetSourceImportConfiguration(id, settings)));
    }
#endif

    TEST_F(ProjectTest, ConfigurationCommitSucceedsWhenCacheCannotBeReplaced)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&      project = std::get<std::unique_ptr<Project>>(opened);
        const auto id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        fs::remove(project->GetSourceAssetCachePath());
        fs::create_directory(project->GetSourceAssetCachePath());
        SourceImportConfiguration settings;
        settings.Settings["usage"] = "normal";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->SetSourceImportConfiguration(id, settings)));
        EXPECT_EQ(project->GetSourceAssetDatabase().FindByID(id).Import.Settings, settings.Settings);
        EXPECT_EQ(project->GetAssetWarnings().size(), 1u);
        auto loaded = SourceAssetMetaSerializer::Load(directory / "assets/a.png.kmeta", AssetPath("a.png"));
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loaded));
        EXPECT_EQ(std::get<SourceAssetMetadata>(loaded).Import.Settings, settings.Settings);
    }

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
        std::get<std::unique_ptr<Project>>(opened).reset();
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

    TEST_F(ProjectTest, UnreadableSidecarIsAWarningAndKeepsTheRecord)
    {
        auto opened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto&      project = std::get<std::unique_ptr<Project>>(opened);
        const auto id      = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        Write(directory / "assets/a.png.kmeta", "[broken");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        EXPECT_FALSE(project->GetAssetWarnings().empty());
        const auto kept = project->GetSourceAssetDatabase().FindByID(id);
        EXPECT_EQ(kept.Path, AssetPath("a.png"));
        EXPECT_FALSE(kept.Missing);
        project.reset();
        auto reopened = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(reopened));
        EXPECT_EQ(
            std::get<std::unique_ptr<Project>>(reopened)->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png")),
            id);
        EXPECT_EQ(Read(directory / "assets/a.png.kmeta"), "[broken");
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
        project.reset();
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

    TEST_F(ProjectTest, NewFileAtAMissingSourcePathTakesANewIdentity)
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
        // The new file gets a new identity; the old record is dropped with a warning.
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->RefreshAssets()));
        const auto replacement = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("a.png"));
        EXPECT_TRUE(replacement.IsValid());
        EXPECT_NE(replacement, id);
        EXPECT_FALSE(project->GetSourceAssetDatabase().Contains(id));
        ASSERT_EQ(project->GetAssetWarnings().size(), 1u);
        EXPECT_EQ(project->GetAssetWarnings()[0].Code, AssetErrorCode::DuplicatePath);
        EXPECT_NE(Read(project->GetSourceAssetCachePath()), cache);
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
