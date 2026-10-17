#include <fstream>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Storage/ProductStore.h"
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;

    class ProductStoreTest : public ::testing::Test
    {
    protected:
        fs::path      Root;
        SourceAssetID Source = SourceAssetID::Generate();
        unsigned      Stages = 0;

        void SetUp() override
        {
            Root = fs::temp_directory_path() / ("kans-store-" + SourceAssetID::Generate().ToString());
            fs::create_directories(Root / "work");
        }
        void TearDown() override
        {
            std::error_code ignored;
            fs::remove_all(Root, ignored);
        }
        Scope<ProductStore> Open()
        {
            auto opened = ProductStore::Open(Root / "store");
            EXPECT_TRUE(std::holds_alternative<Scope<ProductStore>>(opened));
            return std::holds_alternative<Scope<ProductStore>>(opened) ?
                       std::get<Scope<ProductStore>>(std::move(opened)) :
                       nullptr;
        }
        static std::vector<std::byte> Mesh(float x)
        {
            MeshData mesh;
            mesh.Vertices.resize(3);
            mesh.Vertices[0].Position = {x, 0, 0};
            mesh.Indices              = {0, 1, 2};
            BuildSingleSection(mesh);
            return std::get<std::vector<std::byte>>(EncodeMeshProduct(mesh));
        }
        // Stages one mesh Product per entry of `meshes` and returns a request for them.
        PublishRequest Staged(const std::vector<std::vector<std::byte>>& meshes, const LocalIDMap& previous = {})
        {
            PublishRequest request;
            request.Source           = Source;
            request.Fingerprint      = "fingerprint-" + std::to_string(Stages);
            request.StagingDirectory = Root / "work" / ("stage-" + std::to_string(++Stages));
            fs::create_directories(request.StagingDirectory);
            request.Imported.LocalIDs = previous;
            for (size_t i = 0; i < meshes.size(); ++i)
            {
                const uint64_t local = i + 1;
                const auto     file  = std::to_string(local) + ".kmesh";
                std::ofstream(request.StagingDirectory / file, std::ios::binary)
                    .write(reinterpret_cast<const char*>(meshes[i].data()), std::streamsize(meshes[i].size()));
                request.Imported.LocalIDs["mesh:" + std::to_string(local)] = local;
                request.Imported.Products.push_back({local, AssetType::StaticMesh, file, {}});
            }
            return request;
        }
        size_t Generations() const
        {
            size_t count = 0;
            for ([[maybe_unused]] const auto& entry : fs::directory_iterator(Root / "store" / "generations"))
                ++count;
            return count;
        }
        static std::vector<std::byte> Read(const fs::path& file)
        {
            return std::get<std::vector<std::byte>>(ReadProductFile(file));
        }
    };

    TEST_F(ProductStoreTest, PublishedSetSurvivesReopenWithBytesAndBuildRecord)
    {
        auto store           = Open();
        auto request         = Staged({Mesh(1), Mesh(2)});
        request.SourceInputs = {SourceAssetID::Generate()};
        auto published       = store->Publish(request);
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(published))
            << std::get<AssetError>(published).Message;
        const auto registry = std::get<Ref<const AssetRegistry>>(published);
        ASSERT_EQ(registry->GetAllProducts().size(), 2u);
        EXPECT_EQ(store->Snapshot(), registry);
        store.reset();

        store            = Open();
        const auto build = store->FindBuild(Source, "default");
        ASSERT_TRUE(build);
        EXPECT_EQ(build->Fingerprint, request.Fingerprint);
        EXPECT_EQ(build->SourceInputs, request.SourceInputs);
        EXPECT_EQ(store->FindSource(Source)->LocalIDs, request.Imported.LocalIDs);
        const auto record = store->Snapshot()->Find({Source, 2}, "default");
        ASSERT_TRUE(record);
        EXPECT_EQ(Read(Root / "store" / record->Location.RelativeFile.GetPath()), Mesh(2));
    }

    TEST_F(ProductStoreTest, InvalidImportsAreRejectedBeforeAnythingIsWritten)
    {
        auto store = Open();
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(store->Publish(Staged({Mesh(1)}))));
        const auto before      = store->Snapshot();
        const auto generations = Generations();
        const auto rejected    = [&](PublishRequest request) {
            auto result = store->Publish(request);
            EXPECT_TRUE(std::holds_alternative<AssetError>(result));
            EXPECT_EQ(store->Snapshot(), before);
            EXPECT_EQ(Generations(), generations);
        };
        auto request                        = Staged({Mesh(1)});
        request.Imported.LocalIDs["mesh:1"] = 7; // changes a published identity
        rejected(request);
        request = Staged({Mesh(1)});
        request.Imported.LocalIDs.clear(); // drops a published identity
        rejected(request);
        request                              = Staged({Mesh(1), Mesh(2)});
        request.Imported.Products[1].LocalID = 1; // duplicate LocalID
        rejected(request);
        request                                 = Staged({Mesh(1)});
        request.Imported.Products[0].StagedFile = "../escape.kmesh";
        rejected(request);
        request                           = Staged({Mesh(1)});
        request.Imported.Products[0].Type = AssetType::Texture; // bytes are not a texture
        rejected(request);
        request                                   = Staged({Mesh(1)});
        request.Imported.Products[0].Dependencies = {{SourceAssetID::Generate(), 1}}; // not published
        rejected(request);
        request = Staged({{std::byte {1}, std::byte {2}}}); // corrupt
        rejected(request);
        request             = Staged({Mesh(1)});
        request.Fingerprint = "";
        rejected(request);
    }

    TEST_F(ProductStoreTest, CancelledPublicationKeepsPreviousSetAndRemovesItsFiles)
    {
        auto store = Open();
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(store->Publish(Staged({Mesh(1)}))));
        const auto before = store->Snapshot();
        for (auto stop :
             {PublicationPhase::Validated, PublicationPhase::ProductsWritten, PublicationPhase::BeforeCommit})
        {
            auto request     = Staged({Mesh(9)}, store->FindSource(Source)->LocalIDs);
            request.Progress = [stop](PublicationPhase phase) { return phase != stop; };
            auto result      = store->Publish(request);
            ASSERT_TRUE(std::holds_alternative<AssetError>(result));
            EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::Cancelled);
            EXPECT_EQ(store->Snapshot(), before);
            EXPECT_EQ(Generations(), 1u);
        }
    }

    TEST_F(ProductStoreTest, ReplacedGenerationStaysWhileReferencedThenIsCollected)
    {
        auto store = Open();
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(store->Publish(Staged({Mesh(1)}))));
        auto       old     = store->Snapshot();
        const auto oldFile = Root / "store" / old->Find({Source, 1})->Location.RelativeFile.GetPath();
        auto       next    = store->Publish(Staged({Mesh(2)}, store->FindSource(Source)->LocalIDs));
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(next));
        // A runtime still holding the old Registry can read the old bytes.
        EXPECT_EQ(Read(oldFile), Mesh(1));
        EXPECT_EQ(Generations(), 2u);
        old.reset();
        EXPECT_EQ(store->CollectGarbage(), 1u);
        EXPECT_FALSE(fs::exists(oldFile));
        EXPECT_EQ(Generations(), 1u);
    }

    TEST_F(ProductStoreTest, RemovingASourceUnpublishesItButKeepsItsIdentities)
    {
        auto       store     = Open();
        const auto dependent = SourceAssetID::Generate();
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(store->Publish(Staged({Mesh(1), Mesh(2)}))));
        const auto ids = store->FindSource(Source)->LocalIDs;
        // A second source whose build used the first one's Product.
        auto request          = Staged({Mesh(3)});
        request.Source        = dependent;
        request.ProductInputs = {{Source, 1}};
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(store->Publish(request)));
        EXPECT_EQ(store->FindDependents(Source), std::vector<SourceAssetID> {dependent});

        auto removed = store->RemoveSource(Source);
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(removed));
        EXPECT_FALSE(std::get<Ref<const AssetRegistry>>(removed)->Find({Source, 1}));
        EXPECT_TRUE(std::get<Ref<const AssetRegistry>>(removed)->Find({dependent, 1}));
        store.reset();
        store = Open();
        EXPECT_FALSE(store->FindBuild(Source, "default"));
        EXPECT_EQ(store->FindSource(Source)->LocalIDs, ids);
        EXPECT_EQ(Generations(), 1u);
    }

    TEST_F(ProductStoreTest, UnreadableCatalogAndMissingFilesStartOverWithWarnings)
    {
        {
            auto store = Open();
            ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(store->Publish(Staged({Mesh(1)}))));
            const auto record = store->Snapshot()->Find({Source, 1});
            fs::remove(Root / "store" / record->Location.RelativeFile.GetPath());
        }
        auto store = Open();
        EXPECT_FALSE(store->FindBuild(Source, "default"));
        EXPECT_EQ(store->Snapshot()->GetAllProducts().size(), 0u);
        EXPECT_EQ(store->GetWarnings().size(), 1u);
        store.reset();

        std::ofstream(Root / "store" / "catalog.yaml") << "not: [a catalog";
        store = Open();
        EXPECT_EQ(store->GetWarnings().size(), 1u);
        EXPECT_TRUE(store->Snapshot()->GetAllProducts().empty());
        EXPECT_TRUE(fs::exists(Root / "store" / "catalog.invalid.yaml"));
        EXPECT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(store->Publish(Staged({Mesh(1)}))));
    }

    TEST_F(ProductStoreTest, OnlyOneStoreCanOpenARoot)
    {
        auto store  = Open();
        auto second = ProductStore::Open(Root / "store");
        EXPECT_TRUE(std::holds_alternative<AssetError>(second));
        store.reset();
        EXPECT_TRUE(std::holds_alternative<Scope<ProductStore>>(ProductStore::Open(Root / "store")));
    }

#ifndef _WIN32
    // A process killed at any point of a publication leaves either the old or
    // the new complete set published, never a mix.
    TEST_F(ProductStoreTest, ProcessKilledDuringPublicationLeavesACompleteSet)
    {
        {
            auto store = Open();
            ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(store->Publish(Staged({Mesh(1), Mesh(2)}))));
        }
        const PublicationPhase phases[] = {PublicationPhase::Validated,
                                           PublicationPhase::ProductsWritten,
                                           PublicationPhase::BeforeCommit,
                                           PublicationPhase::Committed};
        for (const auto kill : phases)
        {
            LocalIDMap ids;
            {
                auto store = Open();
                ids        = store->FindSource(Source)->LocalIDs;
            }
            auto request     = Staged({Mesh(float(10 + int(kill))), Mesh(float(20 + int(kill)))}, ids);
            request.Progress = [kill](PublicationPhase phase) {
                if (phase == kill)
                    _exit(0);
                return true;
            };
            const pid_t child = fork();
            ASSERT_GE(child, 0);
            if (child == 0)
            {
                auto opened = ProductStore::Open(Root / "store");
                if (std::holds_alternative<Scope<ProductStore>>(opened))
                    (void)std::get<Scope<ProductStore>>(opened)->Publish(request);
                _exit(1);
            }
            int status = 0;
            ASSERT_EQ(waitpid(child, &status, 0), child);
            ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

            auto store = Open();
            EXPECT_TRUE(store->GetWarnings().empty());
            const auto first  = store->Snapshot()->Find({Source, 1});
            const auto second = store->Snapshot()->Find({Source, 2});
            ASSERT_TRUE(first && second);
            // Both Products come from the same publication.
            EXPECT_EQ(first->Location.RelativeFile.GetPath().parent_path(),
                      second->Location.RelativeFile.GetPath().parent_path());
            const auto bytes = Read(Root / "store" / first->Location.RelativeFile.GetPath());
            // Killed after the commit: the new set; before it: the previous set.
            if (kill == PublicationPhase::Committed)
                EXPECT_EQ(bytes, Mesh(float(10 + int(kill))));
            else
                EXPECT_NE(bytes, Mesh(float(10 + int(kill))));
            // Opening collected the unpublished generation.
            EXPECT_EQ(Generations(), 1u);
        }
    }
#endif
} // namespace
