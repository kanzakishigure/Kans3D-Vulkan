#include <chrono>
#include <fstream>
#include <future>
#include <thread>
#include <gtest/gtest.h>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Editor/EditorAssetImportService.h"

namespace
{
    using namespace Kans;
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;

    // Blocks a build at one publication phase until released.
    struct Checkpoint
    {
        PublicationPhase         Phase;
        std::promise<void>       Entered, Release;
        std::shared_future<void> Released = Release.get_future().share();
        std::atomic<bool>        Visited {false};
        bool                     Observe(PublicationPhase phase)
        {
            if (phase == Phase && !Visited.exchange(true))
            {
                Entered.set_value();
                Released.wait_for(10s);
            }
            return true;
        }
    };

    class EditorAssetImportServiceTest : public ::testing::Test
    {
    protected:
        fs::path                        Root;
        Scope<EditorAssetImportService> Service;
        SourceAssetID                   Source;

        void SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            Root = fs::temp_directory_path() / ("kans-import-service-" + SourceAssetID::Generate().ToString());
            fs::create_directories(Root / "assets");
            Image(255);
            ProjectConfig config;
            config.Name          = "Import service";
            config.RootDirectory = Root;
            auto project         = Project::Open(config);
            ASSERT_TRUE(std::holds_alternative<Scope<Project>>(project));
            auto owned = std::get<Scope<Project>>(std::move(project));
            Source     = owned->GetSourceAssetDatabase().FindIDByPath(AssetPath("pixel.tga"));
            ASSERT_TRUE(Source.IsValid());
            Service = std::get<Scope<EditorAssetImportService>>(EditorAssetImportService::Create(std::move(owned)));
        }
        void TearDown() override
        {
            Service.reset();
            std::error_code ignored;
            fs::remove_all(Root, ignored);
        }
        void Image(uint8_t value)
        {
            std::string tga(18, '\0');
            tga[2] = 2, tga[12] = 1, tga[14] = 1, tga[16] = 32, tga[17] = 8;
            tga += {char(value), char(value), char(value), char(255)};
            std::ofstream(Root / "assets" / "pixel.tga", std::ios::binary | std::ios::trunc) << tga;
        }
        EditorAssetImportSnapshot Finish()
        {
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (std::chrono::steady_clock::now() < deadline)
            {
                auto polled = Service->Poll();
                EXPECT_TRUE(std::holds_alternative<bool>(polled));
                if (std::holds_alternative<bool>(polled) && std::get<bool>(polled))
                    break;
                std::this_thread::sleep_for(1ms);
            }
            return std::get<EditorAssetImportSnapshot>(Service->GetSnapshot());
        }
        AssetBuildOptions Options(Checkpoint* checkpoint = nullptr)
        {
            AssetBuildOptions options;
            options.Target = EditorAssetTarget;
            if (checkpoint)
                options.Progress = [checkpoint](PublicationPhase phase) { return checkpoint->Observe(phase); };
            return options;
        }
    };

    TEST_F(EditorAssetImportServiceTest, BuildRunsOnAWorkerAndPollReportsCompletionOnce)
    {
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Source, Options())));
        const auto done = Finish();
        EXPECT_EQ(done.State, EditorAssetImportState::Succeeded);
        ASSERT_TRUE(done.Result);
        EXPECT_EQ(done.Result->Root.Source, Source);
        EXPECT_EQ(std::get<bool>(Service->Poll()), false);
        EXPECT_FALSE(Service->GetRuntimeAssetRegistry()->GetAllProducts().empty());
    }

    TEST_F(EditorAssetImportServiceTest, ProjectOperationsAreRejectedWhileBuilding)
    {
        Checkpoint checkpoint {PublicationPhase::BeforeCommit};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Source, Options(&checkpoint))));
        ASSERT_EQ(checkpoint.Entered.get_future().wait_for(10s), std::future_status::ready);
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->Start(Source, Options())));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->GetSources()));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->RefreshSources()));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->DeleteSourceAsset(Source)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->MoveSourceAsset(Source, AssetPath("moved.tga"))));
        // Reading the published Registry is allowed.
        EXPECT_TRUE(Service->GetRuntimeAssetRegistry());
        EXPECT_EQ(std::get<bool>(Service->Poll()), false);
        checkpoint.Release.set_value();
        EXPECT_EQ(Finish().State, EditorAssetImportState::Succeeded);
        EXPECT_TRUE(std::holds_alternative<std::vector<SourceAssetMetadata>>(Service->GetSources()));
    }

    TEST_F(EditorAssetImportServiceTest, CancelBeforeCommitKeepsTheLastPublishedProducts)
    {
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Source, Options())));
        ASSERT_EQ(Finish().State, EditorAssetImportState::Succeeded);
        const auto before = Service->GetRuntimeAssetRegistry()->GetAllProducts();

        Image(1);
        Checkpoint checkpoint {PublicationPhase::Validated};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Source, Options(&checkpoint))));
        ASSERT_EQ(checkpoint.Entered.get_future().wait_for(10s), std::future_status::ready);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Cancel()));
        EXPECT_TRUE(std::get<EditorAssetImportSnapshot>(Service->GetSnapshot()).CancelRequested);
        checkpoint.Release.set_value();
        const auto cancelled = Finish();
        EXPECT_EQ(cancelled.State, EditorAssetImportState::Cancelled);
        const auto after = Service->GetRuntimeAssetRegistry()->GetAllProducts();
        ASSERT_EQ(after.size(), before.size());
        EXPECT_EQ(after[0].Location.ContentDigest, before[0].Location.ContentDigest);

        // A new build starts from a fresh state and publishes the edit.
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Source, Options())));
        EXPECT_EQ(Finish().State, EditorAssetImportState::Succeeded);
        EXPECT_NE(Service->GetRuntimeAssetRegistry()->GetAllProducts()[0].Location.ContentDigest,
                  before[0].Location.ContentDigest);
    }

    TEST_F(EditorAssetImportServiceTest, DeletionAndRefreshPublishARegistryChangeOnce)
    {
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Source, Options())));
        ASSERT_EQ(Finish().State, EditorAssetImportState::Succeeded);
        EXPECT_FALSE(std::get<std::optional<Ref<const AssetRegistry>>>(Service->ConsumeRuntimeRegistryChange()));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->DeleteSourceAsset(Source)));
        auto change = std::get<std::optional<Ref<const AssetRegistry>>>(Service->ConsumeRuntimeRegistryChange());
        ASSERT_TRUE(change && *change);
        EXPECT_TRUE((*change)->GetAllProducts().empty());
        EXPECT_FALSE(std::get<std::optional<Ref<const AssetRegistry>>>(Service->ConsumeRuntimeRegistryChange()));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->RefreshSources()));
        EXPECT_TRUE(std::get<std::optional<Ref<const AssetRegistry>>>(Service->ConsumeRuntimeRegistryChange()));
    }

    TEST_F(EditorAssetImportServiceTest, OtherThreadsAreRejectedAndShutdownJoinsTheWorker)
    {
        auto fromWorker = std::async(std::launch::async, [&] { return Service->Start(Source, Options()); }).get();
        EXPECT_TRUE(std::holds_alternative<AssetError>(fromWorker));

        Checkpoint checkpoint {PublicationPhase::ProductsWritten};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Source, Options(&checkpoint))));
        ASSERT_EQ(checkpoint.Entered.get_future().wait_for(10s), std::future_status::ready);
        auto shutdown = std::async(std::launch::deferred, [&] { return Service->Shutdown(); });
        checkpoint.Release.set_value();
        EXPECT_TRUE(std::holds_alternative<std::monostate>(shutdown.get()));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->Start(Source, Options())));
        // The project is released when the service is destroyed.
        Service.reset();
        ProjectConfig config;
        config.Name          = "Reopen";
        config.RootDirectory = Root;
        EXPECT_TRUE(std::holds_alternative<Scope<Project>>(Project::Open(config)));
    }
} // namespace
