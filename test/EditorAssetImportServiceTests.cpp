#include <chrono>
#include <fstream>
#include <future>
#include <thread>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Editor/EditorAssetImportService.h"
#include "Kans3D/Renderer/RenderView.h"
#include "Kans3D/Scene/Entity.h"
#include "LegacyModelCacheFixture.h"

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
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->StartOutdated(Options())));
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

    TEST_F(EditorAssetImportServiceTest, PublishedSweepUsesOneWorkerAndCancellationKeepsAtomicPublications)
    {
        // Bug: batch jobs bypass the busy/owner rule, or cancelling a sweep
        // replaces an earlier successfully published source with partial output.
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Source, Options())));
        ASSERT_EQ(Finish().State, EditorAssetImportState::Succeeded);
        const auto before = Service->GetRuntimeAssetRegistry()->GetAllProducts();
        Image(1);
        Checkpoint checkpoint {PublicationPhase::BeforeCommit};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options(&checkpoint))));
        ASSERT_EQ(checkpoint.Entered.get_future().wait_for(10s), std::future_status::ready);
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->Start(Source, Options())));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Service->StartOutdated(Options())));
        EXPECT_FALSE(LegacyModelCacheFixture::Value(Service->Poll()));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Cancel()));
        checkpoint.Release.set_value();
        EXPECT_EQ(Finish().State, EditorAssetImportState::Cancelled);
        const auto after = Service->GetRuntimeAssetRegistry()->GetAllProducts();
        ASSERT_EQ(after.size(), before.size());
        EXPECT_EQ(after[0].Location.ContentDigest, before[0].Location.ContentDigest);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        EXPECT_EQ(Finish().State, EditorAssetImportState::Succeeded);
        auto fromWorker = std::async(std::launch::async, [&] { return Service->StartOutdated(Options()); }).get();
        EXPECT_TRUE(std::holds_alternative<AssetError>(fromWorker));
    }

    TEST_F(EditorAssetImportServiceTest, EmptyPublishedSweepDoesNotImportUnpublishedSources)
    {
        // Bug: the open-time sweep imports the entire source database, including
        // large assets the project has never published, or starts a job per source.
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto done = Finish();
        ASSERT_EQ(done.State, EditorAssetImportState::Succeeded);
        ASSERT_TRUE(done.Result);
        EXPECT_TRUE(done.Result->CheckedSources.empty());
        EXPECT_EQ(done.Result->Root.Disposition, AssetBuildDisposition::UpToDate);
        EXPECT_TRUE(done.Result->Root.Registry->GetAllProducts().empty());
        auto options            = Options();
        options.CancelRequested = [] { return true; };
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(options)));
        EXPECT_EQ(Finish().State, EditorAssetImportState::Cancelled);
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

namespace
{
    namespace Legacy = Kans::LegacyModelCacheFixture;
    class PublishedAssetImportTest : public ::testing::Test
    {
    protected:
        fs::path                             Root;
        Scope<EditorAssetImportService>      Service;
        std::map<std::string, SourceAssetID> Sources;
        std::map<std::string, ProductRecord> Before;
        std::atomic<size_t>                  Publications {0};

        void Image(const std::string& name, uint8_t value)
        {
            std::string tga(18, '\0');
            tga[2] = 2, tga[12] = 1, tga[14] = 1, tga[16] = 32, tga[17] = 8;
            tga += {char(value), char(value), char(value), char(255)};
            Legacy::Write(Root / "assets" / name, tga);
        }
        AssetBuildOptions Options()
        {
            AssetBuildOptions options;
            options.Target   = EditorAssetTarget;
            options.Progress = [this](PublicationPhase phase) {
                if (phase == PublicationPhase::Committed)
                    ++Publications;
                return true;
            };
            return options;
        }
        void SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            Root = fs::temp_directory_path() / ("kans-published-import-" + SourceAssetID::Generate().ToString());
            Legacy::WriteSources(Root);
            Legacy::Seed(Root, EditorAssetTarget);
            unsigned ordinal = 0;
            // Fixed IDs put the broken source first: stopping at a failure would
            // prevent the edited texture and old model from rebuilding.
            for (const std::string name : {"broken.tga", "edited.tga", "current.tga", "other.tga", "unpublished.tga"})
            {
                Image(name, 255);
                SourceAssetMetadata metadata;
                metadata.sourceAssetID = SourceAssetID::FromString("000000000000000" + std::to_string(++ordinal));
                metadata.Path          = AssetPath(name);
                Legacy::Value(SourceAssetMetaSerializer::Save(Root / "assets" / (name + ".kmeta"), metadata));
                Sources.emplace(name, metadata.sourceAssetID);
            }
            Legacy::Write(Root / "assets/bound.material",
                          "magic: KansMaterial\nversion: 2\nparameters: {base_color: [1,1,1,1]}\n");
            SourceAssetMetadata bound;
            bound.sourceAssetID = SourceAssetID::FromString("0000000000000009");
            bound.Path          = AssetPath("bound.material");
            Legacy::Value(SourceAssetMetaSerializer::Save(Root / "assets/bound.material.kmeta", bound));
            Sources.emplace("bound.material", bound.sourceAssetID);
            ProjectConfig config;
            config.Name          = "Published import";
            config.RootDirectory = Root;
            auto owner           = Legacy::Value(Project::Open(config));
            for (const std::string name : {"broken.tga", "edited.tga", "current.tga", "other.tga"})
            {
                auto options = Options();
                if (name == "other.tga")
                    options.Target = "other-target";
                const auto built = Legacy::Value(owner->BuildAsset(Sources.at(name), options));
                Before.emplace(name,
                               *built.Root.Registry->Find({Sources.at(name), built.Root.LocalIDs.at("texture:root")},
                                                          options.Target));
            }
            Publications = 0;
            Image("edited.tga", 7);
            Service = Legacy::Value(EditorAssetImportService::Create(std::move(owner)));
        }
        void TearDown() override
        {
            Service.reset();
            std::error_code error;
            fs::remove_all(Root, error);
            EXPECT_FALSE(error);
        }
        EditorAssetImportSnapshot Finish()
        {
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (std::chrono::steady_clock::now() < deadline)
            {
                const auto polled = Legacy::Value(Service->Poll());
                if (polled)
                    return Legacy::Value(Service->GetSnapshot());
                std::this_thread::sleep_for(1ms);
            }
            throw std::runtime_error("Published import did not finish");
        }
        static std::map<SourceAssetID, AssetBuildDisposition> Dispositions(const AssetBuildGraphResult& result)
        {
            std::map<SourceAssetID, AssetBuildDisposition> dispositions;
            for (const auto& item : result.CheckedSources)
                EXPECT_TRUE(dispositions.emplace(item.Source, item.Disposition).second);
            return dispositions;
        }
        void ExpectFinalRegistry(const AssetBuildGraphResult& result)
        {
            ASSERT_TRUE(result.Root.Registry);
            EXPECT_EQ(result.Root.Registry, Service->GetRuntimeAssetRegistry());
            const auto& registry = *result.Root.Registry;
            for (const auto& name : {"broken.tga", "current.tga", "other.tga"})
            {
                const auto& before = Before.at(name);
                const auto  record = registry.Find(before.Metadata.assetID, before.Target);
                ASSERT_TRUE(record);
                EXPECT_EQ(record->Location.RelativeFile, before.Location.RelativeFile);
                EXPECT_EQ(record->Location.ContentDigest, before.Location.ContentDigest);
            }
            const auto& edited  = Before.at("edited.tga");
            const auto  changed = registry.Find(edited.Metadata.assetID, edited.Target);
            ASSERT_TRUE(changed);
            EXPECT_NE(changed->Location.ContentDigest, edited.Location.ContentDigest);
            size_t meshes = 0;
            for (const auto& product : registry.GetAllProducts())
            {
                EXPECT_NE(product.Metadata.assetID.GetSourceID(), Sources.at("unpublished.tga"));
                if (product.Metadata.assetID.GetSourceID() == Legacy::Source() &&
                    product.Metadata.Type == AssetType::StaticMesh)
                    ++meshes;
            }
            EXPECT_EQ(meshes, 1u);
            EXPECT_FALSE(registry.Find(Legacy::ID(4), EditorAssetTarget));
            EXPECT_FALSE(registry.Find(Legacy::ID(5), EditorAssetTarget));
        }
    };

    TEST_F(PublishedAssetImportTest, CapturedV12CacheReimportsOnlyOutdatedTargetSourcesAndSecondRunPublishesNothing)
    {
        // Bug: opening a project never checks published v1 KMOD, forces all
        // sources, checks each Product separately, or imports unrelated targets.
        auto provider = Legacy::Value(LooseFileAssetProvider::Open(Service->GetProductDirectory()));
        AssetManagerSpecification spec;
        spec.Target = EditorAssetTarget;
        AssetManager assets(provider, Service->GetRuntimeAssetRegistry(), spec);
        const auto   old = assets.Load<ModelAsset>(Legacy::ID(1));
        ASSERT_TRUE(std::holds_alternative<AssetError>(old));
        EXPECT_EQ(std::get<AssetError>(old).Code, AssetErrorCode::UnsupportedVersion);
        const auto oldMesh = assets.Load<MeshAsset>(Legacy::ID(4));
        ASSERT_TRUE(std::holds_alternative<AssetError>(oldMesh));
        EXPECT_EQ(std::get<AssetError>(oldMesh).Code, AssetErrorCode::UnsupportedVersion);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto done = Finish();
        ASSERT_EQ(done.State, EditorAssetImportState::Succeeded);
        ASSERT_TRUE(done.Result);
        EXPECT_FALSE(done.Source.IsValid());
        EXPECT_FALSE(done.Result->Root.Source.IsValid());
        EXPECT_EQ(done.Result->Root.Disposition, AssetBuildDisposition::Published);
        EXPECT_TRUE(done.Result->Root.Warnings.empty());
        EXPECT_EQ(Publications, 2u);
        const std::map<SourceAssetID, AssetBuildDisposition> expected {
            {Legacy::Source(), AssetBuildDisposition::Published},
            {Sources.at("edited.tga"), AssetBuildDisposition::Published},
            {Sources.at("broken.tga"), AssetBuildDisposition::UpToDate},
            {Sources.at("current.tga"), AssetBuildDisposition::UpToDate}};
        EXPECT_EQ(Dispositions(*done.Result), expected);
        ExpectFinalRegistry(*done.Result);
        EXPECT_FALSE(Legacy::Value(Service->Poll()));
        const auto firstRegistry = done.Result->Root.Registry;
        Publications             = 0;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto again = Finish();
        ASSERT_TRUE(again.Result);
        EXPECT_EQ(again.State, EditorAssetImportState::Succeeded);
        EXPECT_EQ(again.Result->Root.Disposition, AssetBuildDisposition::UpToDate);
        EXPECT_TRUE(again.Result->Root.Warnings.empty());
        EXPECT_EQ(Publications, 0u);
        ASSERT_EQ(again.Result->CheckedSources.size(), 4u);
        for (const auto& item : again.Result->CheckedSources)
            EXPECT_EQ(item.Disposition, AssetBuildDisposition::UpToDate);
        EXPECT_EQ(again.Result->Root.Registry, firstRegistry);
    }

    TEST_F(PublishedAssetImportTest, FirstSourceFailureDoesNotBlockOtherRootsOrDiscardTheirFinalRegistry)
    {
        // Bug: returning the first error leaves every remaining outdated source
        // unreadable, or Root.Registry refers only to an earlier publication.
        Legacy::Write(Root / "assets/broken.tga", "broken raster");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto done = Finish();
        ASSERT_EQ(done.State, EditorAssetImportState::Succeeded);
        ASSERT_TRUE(done.Result);
        EXPECT_EQ(Publications, 2u);
        const std::map<SourceAssetID, AssetBuildDisposition> expected {
            {Legacy::Source(), AssetBuildDisposition::Published},
            {Sources.at("edited.tga"), AssetBuildDisposition::Published},
            {Sources.at("current.tga"), AssetBuildDisposition::UpToDate}};
        EXPECT_EQ(Dispositions(*done.Result), expected);
        ASSERT_EQ(done.Result->Root.Warnings.size(), 1u);
        EXPECT_NE(done.Result->Root.Warnings[0].find("Source build failed: broken.tga:"), std::string::npos);
        EXPECT_NE(done.Result->Root.Warnings[0].find("Unsupported or malformed Texture source"), std::string::npos);
        EXPECT_FALSE(done.Error);
        ExpectFinalRegistry(*done.Result);
    }

    TEST_F(PublishedAssetImportTest, SharedPublishedDependencyBuildsOnceBeforeItsModelConsumer)
    {
        // Bug: starting independent BuildAsset sessions per root duplicates shared
        // dependencies or lets a model fingerprint/use the previous material bytes.
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->Start(Sources.at("bound.material"), Options())));
        ASSERT_EQ(Finish().State, EditorAssetImportState::Succeeded);
        Legacy::Write(Root / "assets/bound.material",
                      "magic: KansMaterial\nversion: 2\nparameters: {base_color: [0,1,0,1]}\n");
        const AssetID bound {Sources.at("bound.material"), 1};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            Service->SetModelMaterialRemap(Legacy::Source(), "material:3:Red", bound)));
        Publications = 0;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto done = Finish();
        ASSERT_TRUE(done.Result);
        EXPECT_TRUE(done.Result->Root.Warnings.empty());
        EXPECT_EQ(Publications, 3u);
        const auto& checked = done.Result->CheckedSources;
        ASSERT_EQ(checked.size(), 5u);
        const std::vector<SourceAssetID> order {Sources.at("broken.tga"),
                                                Sources.at("edited.tga"),
                                                Sources.at("current.tga"),
                                                Sources.at("bound.material"),
                                                Legacy::Source()};
        for (size_t i = 0; i < order.size(); ++i)
            EXPECT_EQ(checked[i].Source, order[i]);
        EXPECT_EQ(checked[3].Disposition, AssetBuildDisposition::Published);
        EXPECT_EQ(checked[4].Disposition, AssetBuildDisposition::Published);
        auto provider = Legacy::Value(LooseFileAssetProvider::Open(Service->GetProductDirectory()));
        AssetManagerSpecification spec;
        spec.Target = EditorAssetTarget;
        AssetManager assets(provider, done.Result->Root.Registry, spec);
        const auto   model  = Legacy::Value(assets.Load<ModelAsset>(Legacy::ID(1)));
        const auto   meshID = model->GetMeshData().Nodes[1].Meshes.at(0);
        const auto   mesh   = Legacy::Value(assets.Load<MeshAsset>(meshID));
        EXPECT_EQ(mesh->GetData().MaterialSlots.at(0).DefaultMaterial, bound);
        const auto material = Legacy::Value(assets.Load<MaterialDataAsset>(bound));
        EXPECT_EQ(material->GetData().BaseColor, (std::array<float, 4> {0, 1, 0, 1}));
        // Now the model is current. Only its input Product changes: skipping
        // models by importer version/source digest alone would miss this rebuild.
        const auto modelFingerprint = checked.back().Fingerprint;
        Legacy::Write(Root / "assets/bound.material",
                      "magic: KansMaterial\nversion: 2\nparameters: {base_color: [0,0,1,1]}\n");
        Publications = 0;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto dependencyEdit = Finish();
        ASSERT_TRUE(dependencyEdit.Result);
        EXPECT_TRUE(dependencyEdit.Result->Root.Warnings.empty());
        EXPECT_EQ(Publications, 2u);
        auto expected = Dispositions(*done.Result);
        for (auto& [id, disposition] : expected)
            disposition = AssetBuildDisposition::UpToDate;
        expected[Sources.at("bound.material")] = AssetBuildDisposition::Published;
        expected[Legacy::Source()]             = AssetBuildDisposition::Published;
        EXPECT_EQ(Dispositions(*dependencyEdit.Result), expected);
        EXPECT_NE(dependencyEdit.Result->CheckedSources.back().Fingerprint, modelFingerprint);
        // An input file (.mtl) has no Products and is not a sweep root. Its digest
        // must nevertheless invalidate its already-current model consumer.
        const auto dependencyFingerprint = dependencyEdit.Result->CheckedSources.back().Fingerprint;
        Legacy::Write(Root / "assets/legacy.mtl", "newmtl Red\nKd 0.5 0 0\nnewmtl Blue\nKd 0 0 1\n");
        Publications = 0;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto inputEdit = Finish();
        ASSERT_TRUE(inputEdit.Result);
        EXPECT_TRUE(inputEdit.Result->Root.Warnings.empty());
        EXPECT_EQ(Publications, 1u);
        expected[Sources.at("bound.material")] = AssetBuildDisposition::UpToDate;
        EXPECT_EQ(Dispositions(*inputEdit.Result), expected);
        EXPECT_NE(inputEdit.Result->CheckedSources.back().Fingerprint, dependencyFingerprint);
        // A failed published remap must not replace the last-good model with an
        // embedded-material fallback while unrelated sources are being checked.
        const auto lastModel = inputEdit.Result->Root.Registry->Find(Legacy::ID(1), EditorAssetTarget);
        ASSERT_TRUE(lastModel);
        Legacy::Write(Root / "assets/bound.material", "broken material");
        Publications = 0;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto failedDependency = Finish();
        ASSERT_TRUE(failedDependency.Result);
        EXPECT_EQ(failedDependency.State, EditorAssetImportState::Succeeded);
        EXPECT_EQ(Publications, 0u);
        ASSERT_EQ(failedDependency.Result->Root.Warnings.size(), 2u);
        EXPECT_NE(failedDependency.Result->Root.Warnings[0].find("Source build failed: bound.material:"),
                  std::string::npos);
        EXPECT_NE(failedDependency.Result->Root.Warnings[1].find(
                      "Skipped dependent legacy.obj: dependency bound.material failed:"),
                  std::string::npos);
        const auto retained = failedDependency.Result->Root.Registry->Find(Legacy::ID(1), EditorAssetTarget);
        ASSERT_TRUE(retained);
        EXPECT_EQ(retained->Location.RelativeFile, lastModel->Location.RelativeFile);
        EXPECT_EQ(retained->Location.ContentDigest, lastModel->Location.ContentDigest);
        ASSERT_EQ(failedDependency.Result->CheckedSources.size(), 3u);
        for (const auto& item : failedDependency.Result->CheckedSources)
            EXPECT_EQ(item.Disposition, AssetBuildDisposition::UpToDate);
    }

    TEST_F(PublishedAssetImportTest, ApplyingRegistryRetriesUnsupportedModelAndKeepsOldScenePartOverrides)
    {
        // Bug: failed AssetManager entries survive SetRegistry, or migration maps
        // a saved old part override to the wrong section/material after the sweep.
        auto provider = Legacy::Value(LooseFileAssetProvider::Open(Service->GetProductDirectory()));
        AssetManagerSpecification spec;
        spec.Target = EditorAssetTarget;
        AssetManager assets(provider, Service->GetRuntimeAssetRegistry(), spec);
        auto         scene     = CreateRef<Scene>("existing scene");
        auto         entity    = scene->CreateEntityWithID(UUID(123), "Body", true);
        auto&        component = entity.AddComponent<ProductModelComponent>();
        component.Model        = Legacy::ID(1);
        component.Overrides    = {{Legacy::ID(4), Legacy::ID(3)}, {Legacy::ID(5), Legacy::ID(2)}};
        auto old               = assets.Load<ModelAsset>(component.Model);
        ASSERT_TRUE(std::holds_alternative<AssetError>(old));
        EXPECT_EQ(std::get<AssetError>(old).Code, AssetErrorCode::UnsupportedVersion);
        const auto failed = assets.Request<ModelAsset>(component.Model);
        ASSERT_EQ(failed.State, AssetLoadState::Failed);
        ASSERT_TRUE(failed.Error);
        EXPECT_EQ(failed.Error->Code, AssetErrorCode::UnsupportedVersion);
        RenderAssetCache cache(assets);
        EXPECT_EQ(cache.GetModel(component.Model), nullptr);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->StartOutdated(Options())));
        const auto done = Finish();
        ASSERT_TRUE(done.Result);
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(assets.SetRegistry(done.Result->Root.Registry)));
        const ModelInstances* instances = nullptr;
        const auto            deadline  = std::chrono::steady_clock::now() + 10s;
        while (!(instances = cache.GetModel(component.Model)) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        ASSERT_NE(instances, nullptr);
        ASSERT_EQ(instances->Instances.size(), 2u);
        RenderView view;
        for (size_t i = 0; i < instances->Instances.size(); ++i)
        {
            const auto& instance = instances->Instances[i];
            EXPECT_EQ(instance.Part, Legacy::ID(4 + i));
            EXPECT_EQ(instance.Section, i);
            const auto  mesh = Legacy::Value(assets.Load<MeshAsset>(instance.Mesh));
            const auto& data = mesh->GetData();
            ASSERT_EQ(data.Sections.size(), 2u);
            const auto& section = data.Sections[instance.Section];
            const auto& slot    = data.MaterialSlots[section.MaterialSlot];
            ExtractModelPart(view,
                             component,
                             glm::mat4(1),
                             ModelStencilMode::Disabled,
                             SceneViewMode::Lit,
                             instance,
                             {section.LocalBounds, slot.DefaultMaterial, slot.AuthoredMaterial});
        }
        ASSERT_EQ(view.Draws.size(), 2u);
        EXPECT_EQ(view.Draws[0].Material, Legacy::ID(3));
        EXPECT_EQ(view.Draws[1].Material, Legacy::ID(2));
        EXPECT_EQ(component.Model, Legacy::ID(1));
        ASSERT_EQ(component.Overrides.size(), 2u);
        EXPECT_EQ(component.Overrides[0].Authored, Legacy::ID(4));
        EXPECT_EQ(component.Overrides[1].Authored, Legacy::ID(5));
    }
} // namespace
