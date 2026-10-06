#include <chrono>
#include <future>
#include <map>
#include <thread>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"

namespace
{
    using namespace Kans;
    using namespace std::chrono_literals;

    // In-memory provider that can block a read, inject failures and count reads.
    class ControlledProvider final : public IAssetProvider
    {
    public:
        enum class Fault
        {
            None,
            Error,
            Exception,
            CorruptBytes
        };
        mutable std::mutex                            Mutex;
        mutable std::condition_variable               Changed;
        std::map<std::string, std::vector<std::byte>> Files;
        mutable unsigned                              Reads     = 0;
        mutable bool                                  BlockNext = false, Blocked = false, Released = false;
        Fault                                         Failure = Fault::None;

        std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord& record) const override
        {
            std::unique_lock<std::mutex> lock(Mutex);
            ++Reads;
            const auto found = Files.find(record.Location.RelativeFile.ToString());
            if (found == Files.end())
                return AssetError {AssetErrorCode::NotFound, "Test Product absent"};
            auto bytes = found->second;
            if (BlockNext)
            {
                BlockNext = false;
                Blocked   = true;
                Changed.notify_all();
                Changed.wait(lock, [&] { return Released; });
                Blocked = false;
            }
            switch (Failure)
            {
                case Fault::Error:
                    return AssetError {AssetErrorCode::IoError, "Injected read failure"};
                case Fault::Exception:
                    throw std::runtime_error("Injected provider exception");
                case Fault::CorruptBytes:
                    bytes[0] ^= std::byte {1};
                    break;
                case Fault::None:
                    break;
            }
            return bytes;
        }
        void Gate()
        {
            std::lock_guard<std::mutex> lock(Mutex);
            BlockNext = true;
            Released  = false;
        }
        bool WaitUntilBlocked()
        {
            std::unique_lock<std::mutex> lock(Mutex);
            return Changed.wait_for(lock, 5s, [&] { return Blocked; });
        }
        void Release()
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Released = true;
            Changed.notify_all();
        }
        void Fail(Fault fault)
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Failure = fault;
        }
        unsigned Count() const
        {
            std::lock_guard<std::mutex> lock(Mutex);
            return Reads;
        }
    };

    class AssetManagerTest : public ::testing::Test
    {
    protected:
        SourceAssetID           Source   = SourceAssetID::Generate();
        AssetID                 MeshID   = {Source, 1};
        AssetID                 ModelID  = {Source, 2};
        Ref<ControlledProvider> Provider = CreateRef<ControlledProvider>();
        unsigned                Version  = 0;

        void TearDown() override { Provider->Release(); }

        ProductRecord Store(AssetID id, AssetType type, std::vector<std::byte> bytes)
        {
            ProductRecord record;
            record.Metadata.assetID       = id;
            record.Metadata.Type          = type;
            record.Location.RelativeFile  = AssetPath("product-" + std::to_string(++Version));
            record.Location.ByteSize      = bytes.size();
            record.Location.ContentDigest = ComputeContentDigest(bytes);
            std::lock_guard<std::mutex> lock(Provider->Mutex);
            Provider->Files.emplace(record.Location.RelativeFile.ToString(), std::move(bytes));
            return record;
        }
        ProductRecord Mesh(AssetID id, float x)
        {
            MeshData mesh;
            mesh.Vertices.resize(3);
            mesh.Vertices[0].Position = {x, 0, 0};
            mesh.Vertices[1].Position = {0, 1, 0};
            mesh.Vertices[2].Position = {0, 0, 1};
            mesh.Indices              = {0, 1, 2};
            return Store(id, AssetType::StaticMesh, std::get<std::vector<std::byte>>(EncodeMeshProduct(mesh)));
        }
        ProductRecord Model(AssetID id, AssetID mesh)
        {
            ModelData     data;
            ModelNodeData node;
            node.Name           = "Root";
            node.LocalTransform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            node.Meshes         = {mesh};
            data.Nodes.push_back(node);
            return Store(id, AssetType::Model, std::get<std::vector<std::byte>>(EncodeModelProduct(data)));
        }
        static Ref<const AssetRegistry> Registry(const std::vector<ProductRecord>& records)
        {
            auto registry = AssetRegistry::FromProducts(records);
            EXPECT_TRUE(std::holds_alternative<AssetRegistry>(registry));
            return CreateRef<const AssetRegistry>(std::get<AssetRegistry>(std::move(registry)));
        }
        template<typename T>
        static AssetLoadStatus<T> WaitForRequest(AssetManager& manager, AssetID id)
        {
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            auto       status   = manager.Request<T>(id);
            while (status.State == AssetLoadState::Loading && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(1ms);
                status = manager.Request<T>(id);
            }
            return status;
        }
        static float FirstX(const Ref<const MeshAsset>& mesh) { return mesh->GetData().Vertices[0].Position[0]; }
    };

    TEST_F(AssetManagerTest, LoadDecodesOnceAndReturnsSharedAsset)
    {
        AssetManager manager(Provider, Registry({Mesh(MeshID, 7)}));
        auto         first  = manager.Load<MeshAsset>(MeshID);
        auto         second = manager.Load<MeshAsset>(MeshID);
        ASSERT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(first));
        ASSERT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(second));
        EXPECT_EQ(std::get<Ref<const MeshAsset>>(first), std::get<Ref<const MeshAsset>>(second));
        EXPECT_EQ(FirstX(std::get<Ref<const MeshAsset>>(first)), 7.0f);
        EXPECT_EQ(std::get<Ref<const MeshAsset>>(first)->assetID, MeshID);
        EXPECT_EQ(Provider->Count(), 1u);
        EXPECT_EQ(manager.GetResidentCount(), 1u);
    }

    TEST_F(AssetManagerTest, RequestLoadsInBackgroundAndThenReportsReady)
    {
        AssetManager manager(Provider, Registry({Mesh(MeshID, 3)}));
        Provider->Gate();
        auto pending = manager.Request<MeshAsset>(MeshID);
        EXPECT_EQ(pending.State, AssetLoadState::Loading);
        EXPECT_FALSE(pending.Asset);
        ASSERT_TRUE(Provider->WaitUntilBlocked());
        EXPECT_EQ(manager.Request<MeshAsset>(MeshID).State, AssetLoadState::Loading);
        EXPECT_EQ(manager.GetLoaded<MeshAsset>(MeshID), nullptr);
        Provider->Release();
        auto ready = WaitForRequest<MeshAsset>(manager, MeshID);
        ASSERT_EQ(ready.State, AssetLoadState::Ready);
        EXPECT_EQ(FirstX(ready.Asset), 3.0f);
        EXPECT_EQ(manager.GetLoaded<MeshAsset>(MeshID), ready.Asset);
        EXPECT_EQ(Provider->Count(), 1u);
    }

    TEST_F(AssetManagerTest, ConcurrentLoadsAndRequestsShareOneRead)
    {
        AssetManager manager(Provider, Registry({Mesh(MeshID, 1)}));
        Provider->Gate();
        auto first = std::async(std::launch::async, [&] { return manager.Load<MeshAsset>(MeshID); });
        ASSERT_TRUE(Provider->WaitUntilBlocked());
        std::vector<std::future<std::variant<Ref<const MeshAsset>, AssetError>>> waiting;
        for (int i = 0; i < 4; ++i)
            waiting.push_back(std::async(std::launch::async, [&] { return manager.Load<MeshAsset>(MeshID); }));
        EXPECT_EQ(manager.Request<MeshAsset>(MeshID).State, AssetLoadState::Loading);
        Provider->Release();
        const auto result = std::get<Ref<const MeshAsset>>(first.get());
        for (auto& other : waiting)
            EXPECT_EQ(std::get<Ref<const MeshAsset>>(other.get()), result);
        EXPECT_EQ(Provider->Count(), 1u);
    }

    TEST_F(AssetManagerTest, MissingTypeMismatchAndInvalidIDsAreErrors)
    {
        AssetManager manager(Provider, Registry({Mesh(MeshID, 1)}));
        auto         missing = manager.Load<MeshAsset>(AssetID {Source, 99});
        ASSERT_TRUE(std::holds_alternative<AssetError>(missing));
        EXPECT_EQ(std::get<AssetError>(missing).Code, AssetErrorCode::NotFound);

        auto wrongType = manager.Load<ModelAsset>(MeshID);
        ASSERT_TRUE(std::holds_alternative<AssetError>(wrongType));
        EXPECT_EQ(std::get<AssetError>(wrongType).Code, AssetErrorCode::InvalidArgument);
        // The failed typed load does not poison the correctly typed request.
        EXPECT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(manager.Load<MeshAsset>(MeshID)));
        auto mismatch = manager.Request<ModelAsset>(MeshID);
        EXPECT_EQ(mismatch.State, AssetLoadState::Failed);

        auto invalid = manager.Request<MeshAsset>(AssetID {});
        EXPECT_EQ(invalid.State, AssetLoadState::Failed);
        EXPECT_TRUE(std::holds_alternative<AssetError>(manager.Load<MeshAsset>(AssetID {})));
    }

    TEST_F(AssetManagerTest, CorruptBytesAndProviderExceptionsBecomeErrors)
    {
        AssetManager manager(Provider, Registry({Mesh(MeshID, 1)}));
        Provider->Fail(ControlledProvider::Fault::CorruptBytes);
        auto corrupt = manager.Load<MeshAsset>(MeshID);
        ASSERT_TRUE(std::holds_alternative<AssetError>(corrupt));
        Provider->Fail(ControlledProvider::Fault::Exception);
        auto thrown = manager.Load<MeshAsset>(MeshID);
        ASSERT_TRUE(std::holds_alternative<AssetError>(thrown));
        EXPECT_EQ(std::get<AssetError>(thrown).Code, AssetErrorCode::IoError);
        EXPECT_EQ(manager.GetResidentCount(), 0u);
    }

    TEST_F(AssetManagerTest, RequestFailureIsReportedUntilRegistryChangesButLoadRetries)
    {
        const auto   record = Mesh(MeshID, 5);
        AssetManager manager(Provider, Registry({record}));
        Provider->Fail(ControlledProvider::Fault::Error);
        auto failed = WaitForRequest<MeshAsset>(manager, MeshID);
        ASSERT_EQ(failed.State, AssetLoadState::Failed);
        ASSERT_TRUE(failed.Error);
        EXPECT_EQ(failed.Error->Code, AssetErrorCode::IoError);

        Provider->Fail(ControlledProvider::Fault::None);
        const unsigned reads = Provider->Count();
        // Polling a failed request does not hammer the provider.
        EXPECT_EQ(manager.Request<MeshAsset>(MeshID).State, AssetLoadState::Failed);
        EXPECT_EQ(Provider->Count(), reads);

        // Replacing the Registry clears failures, so the next request retries.
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(manager.SetRegistry(Registry({record}))));
        auto retried = WaitForRequest<MeshAsset>(manager, MeshID);
        ASSERT_EQ(retried.State, AssetLoadState::Ready);
        EXPECT_EQ(FirstX(retried.Asset), 5.0f);

        // A blocking Load retries a failed entry itself.
        AssetManager blocking(Provider, Registry({record}));
        Provider->Fail(ControlledProvider::Fault::Error);
        EXPECT_TRUE(std::holds_alternative<AssetError>(blocking.Load<MeshAsset>(MeshID)));
        Provider->Fail(ControlledProvider::Fault::None);
        EXPECT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(blocking.Load<MeshAsset>(MeshID)));
    }

    TEST_F(AssetManagerTest, SetRegistryReplacesChangedAssetsAndKeepsOldViewsAlive)
    {
        const auto   model = Model(ModelID, MeshID);
        AssetManager manager(Provider, Registry({Mesh(MeshID, 1), model}));
        auto         oldMesh  = std::get<Ref<const MeshAsset>>(manager.Load<MeshAsset>(MeshID));
        auto         oldModel = std::get<Ref<const ModelAsset>>(manager.Load<ModelAsset>(ModelID));

        auto changes = manager.SetRegistry(Registry({Mesh(MeshID, 2), model}));
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(changes));
        EXPECT_EQ(std::get<ProductChangeSet>(changes).Modified, std::vector<AssetID> {MeshID});

        auto newMesh = std::get<Ref<const MeshAsset>>(manager.Load<MeshAsset>(MeshID));
        EXPECT_NE(newMesh, oldMesh);
        EXPECT_EQ(FirstX(newMesh), 2.0f);
        EXPECT_EQ(FirstX(oldMesh), 1.0f);
        // Unchanged content keeps the cached object.
        EXPECT_EQ(std::get<Ref<const ModelAsset>>(manager.Load<ModelAsset>(ModelID)), oldModel);

        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(manager.SetRegistry(Registry({model}))));
        auto removed = manager.Load<MeshAsset>(MeshID);
        ASSERT_TRUE(std::holds_alternative<AssetError>(removed));
        EXPECT_EQ(std::get<AssetError>(removed).Code, AssetErrorCode::NotFound);
        EXPECT_EQ(FirstX(newMesh), 2.0f);
    }

    TEST_F(AssetManagerTest, RegistryChangeDuringLoadDiscardsTheStaleResult)
    {
        AssetManager manager(Provider, Registry({Mesh(MeshID, 1)}));
        Provider->Gate();
        EXPECT_EQ(manager.Request<MeshAsset>(MeshID).State, AssetLoadState::Loading);
        ASSERT_TRUE(Provider->WaitUntilBlocked());
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(manager.SetRegistry(Registry({Mesh(MeshID, 2)}))));
        Provider->Release();
        auto status = WaitForRequest<MeshAsset>(manager, MeshID);
        ASSERT_EQ(status.State, AssetLoadState::Ready);
        EXPECT_EQ(FirstX(status.Asset), 2.0f);
    }

    TEST_F(AssetManagerTest, UnloadAndCollectUnusedReleaseOnlyUnreferencedAssets)
    {
        AssetManager manager(Provider, Registry({Mesh(MeshID, 1), Model(ModelID, MeshID)}));
        auto         held = std::get<Ref<const MeshAsset>>(manager.Load<MeshAsset>(MeshID));
        ASSERT_TRUE(std::holds_alternative<Ref<const ModelAsset>>(manager.Load<ModelAsset>(ModelID)));
        EXPECT_EQ(manager.CollectUnused(), 1u);
        EXPECT_EQ(manager.GetLoaded<ModelAsset>(ModelID), nullptr);
        EXPECT_EQ(manager.GetLoaded<MeshAsset>(MeshID), held);

        manager.Unload(MeshID);
        EXPECT_EQ(manager.GetResidentCount(), 0u);
        EXPECT_EQ(FirstX(held), 1.0f);
        auto reloaded = std::get<Ref<const MeshAsset>>(manager.Load<MeshAsset>(MeshID));
        EXPECT_NE(reloaded, held);
    }

    TEST_F(AssetManagerTest, DestructionWithQueuedRequestsJoinsWorkers)
    {
        std::vector<ProductRecord> records;
        for (uint64_t i = 1; i <= 32; ++i)
            records.push_back(Mesh(AssetID {Source, i}, float(i)));
        auto manager = std::make_unique<AssetManager>(Provider, Registry(records));
        Provider->Gate();
        for (uint64_t i = 1; i <= 32; ++i)
            manager->Request<MeshAsset>(AssetID {Source, i});
        ASSERT_TRUE(Provider->WaitUntilBlocked());
        auto destroyed = std::async(std::launch::async, [&] { manager.reset(); });
        Provider->Release();
        EXPECT_EQ(destroyed.wait_for(5s), std::future_status::ready);
        EXPECT_LT(Provider->Count(), 32u);
    }
} // namespace
