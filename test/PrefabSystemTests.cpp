#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Renderer/RenderView.h"
#include "Kans3D/Renderer/RenderWorld.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/ScenePlaySession.h"
#include "Kans3D/Scene/SceneSerializer.h"
#include "../KansEditor/src/SceneHistory.h"
#include "PrefabRenderTestUtilities.h"

namespace
{
    using namespace Kans;
    using namespace Kans::Test;
    class PrefabProvider final : public IAssetProvider
    {
    public:
        mutable std::mutex                               Mutex;
        mutable std::condition_variable                  Changed;
        mutable bool                                     Gated = false, Entered = false;
        std::map<std::string, std::vector<std::byte>>    Files;
        std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord& record) const override
        {
            std::unique_lock<std::mutex> lock(Mutex);
            Entered = true;
            Changed.notify_all();
            Changed.wait(lock, [&] { return !Gated; });
            return Files.at(record.Location.RelativeFile.ToString());
        }
        void Release()
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Gated = false;
            Changed.notify_all();
        }
    };
    class PrefabSystem : public testing::Test
    {
    protected:
        const SourceAssetID Source = SourceAssetID::FromString("1234567890abcdef");
        const AssetID       PrefabID {Source, 1}, MeshID {Source, 2};
        const AssetID       Bound {Source, 10}, Authored {Source, 11}, Blue {Source, 12};
        const AssetID       PartChoice {Source, 20}, MeshChoice {Source, 21}, SlotChoice {Source, 22},
            BoundChoice {Source, 23};
        const std::array<float, 16> Identity {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        Ref<PrefabProvider>         Provider = CreateRef<PrefabProvider>();
        Ref<AssetManager>           Assets;
        Ref<Scene>                  Current = CreateRef<Scene>("Prefab test");
        ModelData                   Data;
        MeshData                    Mesh;
        ProductRecord               MeshRecord;
        unsigned                    Version = 0;
        std::filesystem::path       Directory;
        ProductRecord               Store(AssetID id, AssetType type, std::vector<std::byte> bytes)
        {
            ProductRecord record;
            record.Metadata.assetID       = id;
            record.Metadata.Type          = type;
            record.Location.RelativeFile  = AssetPath("product-" + std::to_string(++Version));
            record.Location.ByteSize      = bytes.size();
            record.Location.ContentDigest = ComputeContentDigest(bytes);
            std::lock_guard<std::mutex> lock(Provider->Mutex);
            Provider->Files[record.Location.RelativeFile.ToString()] = std::move(bytes);
            return record;
        }
        Ref<AssetRegistry> Registry()
        {
            auto encoded = EncodeModelProduct(Data);
            EXPECT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
            auto prefab                  = Store(PrefabID, AssetType::Model, std::get<std::vector<std::byte>>(encoded));
            prefab.Metadata.Dependencies = {MeshID};
            auto registry                = AssetRegistry::FromProducts({prefab, MeshRecord});
            EXPECT_TRUE(std::holds_alternative<AssetRegistry>(registry));
            return CreateRef<AssetRegistry>(std::get<AssetRegistry>(registry));
        }
        void SetUp() override
        {
            Directory =
                std::filesystem::temp_directory_path() / ("kans-prefab-" + SourceAssetID::Generate().ToString());
            std::filesystem::create_directory(Directory);
            Mesh.Vertices.resize(9);
            Mesh.Indices          = {0, 1, 2, 3, 4, 5, 6, 7, 8};
            Mesh.MaterialSlots    = {{"Red", "red", Bound, Authored}, {"Blue", "blue", Blue, Blue}};
            Mesh.Material         = Bound;
            Mesh.AuthoredMaterial = Authored;
            for (uint32_t i = 0; i < 3; ++i)
            {
                const float x                     = float(i) * 3;
                Mesh.Vertices[i * 3].Position     = {x, 0, 0};
                Mesh.Vertices[i * 3 + 1].Position = {x + 2, 0, 0};
                Mesh.Vertices[i * 3 + 2].Position = {x, 1, 1};
                Bounds bounds;
                bounds.Expand(glm::vec3(x, 0, 0));
                bounds.Expand(glm::vec3(x + 2, 1, 1));
                Mesh.Sections.push_back({i * 3, 3, i == 1 ? 1u : 0u, bounds});
                Mesh.LocalBounds.Expand(bounds);
                Data.SectionIdentities.push_back({"part" + std::to_string(i), {Source, 30 + i}, MeshID, i});
            }
            MeshRecord =
                Store(MeshID, AssetType::StaticMesh, std::get<std::vector<std::byte>>(EncodeMeshProduct(Mesh)));
            Data.Nodes                      = {{"Root", -1, Identity, {}, "root"},
                                               {"A", 0, Identity, {MeshID}, "node-A"},
                                               {"B", 0, Identity, {MeshID}, "node-B"}};
            Data.Nodes[1].LocalTransform[3] = 10;
            Data.Nodes[2].LocalTransform[7] = 4;
            Assets                          = CreateRef<AssetManager>(Provider, Registry());
        }
        void TearDown() override
        {
            Provider->Release();
            Current->SetPrefabAssetManager(nullptr);
            Current.reset();
            Assets.reset();
            std::filesystem::remove_all(Directory);
        }
        Entity Root(UUID id = UUID(100))
        {
            Entity root                                         = Current->CreateEntityWithID(id, "Instance", false);
            root.AddComponent<PrefabInstanceComponent>().Prefab = PrefabID;
            return root;
        }
        void Ready()
        {
            ASSERT_TRUE(std::holds_alternative<Ref<const PrefabAsset>>(Assets->Load<PrefabAsset>(PrefabID)));
            ASSERT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(Assets->Load<MeshAsset>(MeshID)));
            Current->UpdatePrefabs(*Assets);
        }
        std::string Key(const std::string& node, unsigned part) { return node + "/part:5:part" + std::to_string(part); }
        Entity      Part(UUID root, const std::string& node, unsigned part)
        {
            return Current->GetEntityByUUID(Scene::PrefabEntityUUID(root, Key(node, part)));
        }
        Ref<Scene> SaveLoad(const std::string& filename)
        {
            auto result = SceneSerializer(Current).SerializeChecked(Directory / filename);
            EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
            auto loaded = SceneSerializer::DeserializeChecked(Directory / filename);
            EXPECT_TRUE(std::holds_alternative<Ref<Scene>>(loaded));
            return std::get<Ref<Scene>>(loaded);
        }
        void Reimport()
        {
            std::swap(Data.Nodes[1], Data.Nodes[2]);
            Data.Nodes[2].LocalTransform[3] = 12;
            ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry())));
            Ready();
        }
    };

    TEST_F(PrefabSystem, AssetManagerMeshReplacementRefreshesPersistentProxies)
    {
        // Bug: mesh reimport evicts GPU/CPU caches but never dirties existing section proxies.
        const auto root = Root();
        Ready();
        auto& world = Current->GetRenderWorld();
        world.SetValidationEnabled(true);
        world.Synchronize();
        auto part = Part(root.GetUUID(), "node-A", 0);
        ASSERT_EQ(world.GetEntityDraws(part).size(), 1u);
        const auto before = world.GetEntityDraws(part)[0].WorldBounds;
        for (auto& vertex : Mesh.Vertices)
            vertex.Position[0] += 30;
        for (auto& section : Mesh.Sections)
        {
            section.LocalBounds.Min.x += 30;
            section.LocalBounds.Max.x += 30;
        }
        Mesh.LocalBounds.Min.x += 30;
        Mesh.LocalBounds.Max.x += 30;
        Mesh.MaterialSlots[0].DefaultMaterial = PartChoice;
        MeshRecord = Store(MeshID, AssetType::StaticMesh, std::get<std::vector<std::byte>>(EncodeMeshProduct(Mesh)));
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry())));
        Ready();
        world.Synchronize();
        ASSERT_EQ(world.GetEntityDraws(part).size(), 1u);
        EXPECT_EQ(world.GetEntityDraws(part)[0].WorldBounds.Min, before.Min + glm::vec3(30, 0, 0));
        EXPECT_EQ(world.GetEntityDraws(part)[0].WorldBounds.Max, before.Max + glm::vec3(30, 0, 0));
        EXPECT_EQ(world.GetEntityDraws(part)[0].Material, PartChoice);
        EXPECT_EQ(world.GetStats().Updated, 6u);
    }

    TEST_F(PrefabSystem, SerializedTransformsAndMaterialsPopulatePersistentWorldOnLoad)
    {
        // Bug: serializer writes bypass notifications, leaving initial proxies at default transforms/materials.
        auto root = Root();
        Ready();
        root.PatchComponent<TransformComponent>([](auto& t) { t.Position = {3, 4, 5}; });
        Current->SetPrefabMaterialOverride(root, Key("node-A", 0), 0, PartChoice);
        Current->GetRenderWorld().Synchronize();
        const auto originalPart = Part(root.GetUUID(), "node-A", 0);
        const auto expected     = Current->GetRenderWorld().GetEntityDraws(originalPart);
        ASSERT_EQ(expected.size(), 1u);
        auto loaded = SaveLoad("world-load.kans");
        loaded->SetPrefabAssetManager(Assets.get());
        auto& world = loaded->GetRenderWorld();
        world.SetValidationEnabled(true);
        world.Synchronize();
        auto part = loaded->GetEntityByUUID(originalPart.GetUUID());
        ASSERT_TRUE(part);
        ASSERT_EQ(world.GetEntityDraws(part).size(), 1u);
        EXPECT_EQ(world.GetEntityDraws(part)[0].Transform, expected[0].Transform);
        EXPECT_EQ(world.GetEntityDraws(part)[0].Material, PartChoice);
        EXPECT_EQ(world.GetEntityDraws(part)[0].WorldBounds.Min, expected[0].WorldBounds.Min);
        EXPECT_EQ(world.GetStats().Created, 6u);
    }

    TEST_F(PrefabSystem, OrdinaryMeshRetainsLastReadyProxiesWhileReplacementLoads)
    {
        // Bug: evicting CPU data briefly drops ordinary mesh proxies, unlike the retained GPU cache.
        Ready();
        auto entity = Current->CreateEntity("ordinary mesh");
        entity.AddComponent<MeshComponent>(MeshComponent {MeshID});
        auto& world = Current->GetRenderWorld();
        world.SetValidationEnabled(true);
        world.Synchronize();
        ASSERT_EQ(world.GetEntityDraws(entity).size(), 3u);
        const auto before = world.GetEntityDraws(entity)[0].WorldBounds;
        for (auto& vertex : Mesh.Vertices)
            vertex.Position[0] += 30;
        for (auto& section : Mesh.Sections)
        {
            section.LocalBounds.Min.x += 30;
            section.LocalBounds.Max.x += 30;
        }
        Mesh.LocalBounds.Min.x += 30;
        Mesh.LocalBounds.Max.x += 30;
        MeshRecord = Store(MeshID, AssetType::StaticMesh, std::get<std::vector<std::byte>>(EncodeMeshProduct(Mesh)));
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry())));
        {
            std::lock_guard<std::mutex> lock(Provider->Mutex);
            Provider->Gated = true;
        }
        world.Synchronize();
        ASSERT_EQ(world.GetEntityDraws(entity).size(), 3u);
        EXPECT_EQ(world.GetEntityDraws(entity)[0].WorldBounds.Min, before.Min);
        EXPECT_EQ(Current->GetWorldBounds(entity).Min, before.Min);
        Provider->Release();
        ASSERT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(Assets->Load<MeshAsset>(MeshID)));
        world.Synchronize();
        ASSERT_EQ(world.GetEntityDraws(entity).size(), 3u);
        EXPECT_EQ(world.GetEntityDraws(entity)[0].WorldBounds.Min, before.Min + glm::vec3(30, 0, 0));
        EXPECT_EQ(world.GetStats().Updated, 3u);
    }

    TEST_F(PrefabSystem, NativeInstancesStayFreeOfLegacyComponentsAcrossSaveCopyUndoAndReimport)
    {
        // Bug: placement is clean, but snapshot restore or re-import recreates a transition mirror.
        auto       root = Current->InstantiatePrefab(PrefabID, {1, 2, 3});
        const auto id   = root.GetUUID();
        EXPECT_FALSE(root.HasComponent<ProductModelComponent>());
        EXPECT_FALSE(root.GetComponent<PrefabInstanceComponent>().LegacyPending);
        Ready();
        EXPECT_FALSE(root.HasComponent<ProductModelComponent>());
        const auto   partID = Part(id, "node-A", 0).GetUUID();
        SceneHistory history;
        history.Reset(*Current);
        Part(id, "node-A", 0).PatchComponent<MeshComponent>([&](auto& component) {
            component.Materials[0] = PartChoice;
        });
        ASSERT_TRUE(history.Record(*Current));
        ASSERT_TRUE(history.Undo(*Current));
        EXPECT_FALSE(Current->GetEntityByUUID(id).HasComponent<ProductModelComponent>());
        ASSERT_TRUE(history.Redo(*Current));
        EXPECT_FALSE(Current->GetEntityByUUID(id).HasComponent<ProductModelComponent>());
        EXPECT_EQ(Current->GetEntityByUUID(partID).GetComponent<MeshComponent>().Materials[0], PartChoice);
        auto loaded = SaveLoad("native.scene");
        loaded->UpdatePrefabs(*Assets);
        EXPECT_FALSE(loaded->GetEntityByUUID(id).HasComponent<ProductModelComponent>());
        EXPECT_FALSE(loaded->GetEntityByUUID(id).GetComponent<PrefabInstanceComponent>().LegacyPending);
        Scene copy;
        copy.Restore(loaded->Capture());
        EXPECT_FALSE(copy.GetEntityByUUID(id).HasComponent<ProductModelComponent>());
        EXPECT_EQ(copy.GetEntityByUUID(partID).GetComponent<MeshComponent>().Materials[0], PartChoice);
        Reimport();
        EXPECT_FALSE(Current->GetEntityByUUID(id).HasComponent<ProductModelComponent>());
        EXPECT_EQ(Part(id, "node-A", 0).GetUUID(), partID);
    }

    TEST_F(PrefabSystem, WaitsForReadyThenExpandsExactlyOnceWithDeterministicUUIDs)
    {
        // Bug: expands placeholders before readiness, never polls again, or recreates children every frame.
        Provider->Gated = true;
        auto root       = Root();
        Current->UpdatePrefabs(*Assets);
        {
            std::unique_lock<std::mutex> lock(Provider->Mutex);
            ASSERT_TRUE(Provider->Changed.wait_for(lock, std::chrono::seconds(5), [&] { return Provider->Entered; }));
        }
        EXPECT_TRUE(Current->GetChildren(root).empty());
        EXPECT_FALSE(root.GetComponent<PrefabInstanceComponent>().Expanded);
        Provider->Release();
        Ready();
        EXPECT_EQ(Current->GetChildren(root).size(), 1u);
        ASSERT_EQ(Current->GetMeshEntities().size(), 6u);
        const auto part = Part(root.GetUUID(), "node-A", 0);
        ASSERT_TRUE(part);
        const auto handle   = uint32_t(part);
        const auto snapshot = Current->Capture();
        for (int i = 0; i < 3; ++i)
            Current->UpdatePrefabs();
        EXPECT_EQ(uint32_t(Part(root.GetUUID(), "node-A", 0)), handle);
        EXPECT_EQ(Current->Capture(), snapshot);
        EXPECT_EQ(Current->GetPrefabInstanceRoot(part).GetUUID(), root.GetUUID());
        EXPECT_NE(Scene::PrefabEntityUUID(UUID(101), Key("node-A", 0)), part.GetUUID());
        EXPECT_EQ(uint64_t(part.GetUUID()), 0x23a2583804acde42ull);
    }

    TEST_F(PrefabSystem, ReimportKeepsUUIDOverridesUserChildrenAndRemovedNodesWhenNodesReorder)
    {
        // Bug: uses node indices as identities, rebuild deletes user children, or removed nodes return.
        const auto rootID = Root().GetUUID();
        Ready();
        auto       part                                  = Part(rootID, "node-A", 0);
        const auto partID                                = part.GetUUID();
        part.PatchComponent<TransformComponent>([&](auto& component) { component.Position = {2, 3, 4}; });
        part.PatchComponent<MeshComponent>([&](auto& component) { component.Materials[0] = PartChoice; });
        auto child                                       = Current->CreateEntityWithID(UUID(900), "User child", false);
        Current->SetParent(child, part, {}, false);
        Current->DestroyEntity(Part(rootID, "node-A", 2));
        Reimport();
        part = Part(rootID, "node-A", 0);
        ASSERT_TRUE(part);
        EXPECT_EQ(part.GetUUID(), partID);
        EXPECT_EQ(part.GetComponent<TransformComponent>().Position, glm::vec3(2, 3, 4));
        EXPECT_EQ(part.GetComponent<MeshComponent>().Materials[0], PartChoice);
        EXPECT_EQ(Part(rootID, "node-B", 0).GetComponent<MeshComponent>().Materials[0], Bound);
        EXPECT_EQ(Current->GetParent(Current->GetEntityByUUID(UUID(900))).GetUUID(), partID);
        EXPECT_FALSE(Part(rootID, "node-A", 2));
        const auto& overrides = Current->GetEntityByUUID(rootID).GetComponent<PrefabInstanceComponent>().Overrides;
        EXPECT_TRUE(overrides.at(Key("node-A", 2)).Removed);
        EXPECT_EQ(overrides.at(Key("node-A", 0)).Materials.at(0), PartChoice);
    }

    TEST_F(PrefabSystem, RemovingAndRestoringParentKeepsUserAdditionsAndExplicitChildRemoval)
    {
        // Bug: removing a parent permanently marks every descendant removed, or
        // deleting the entire instance leaves ordinary additions orphaned.
        const auto rootID = Root().GetUUID();
        Ready();
        auto root  = Current->GetEntityByUUID(rootID);
        auto part  = Part(rootID, "node-A", 0);
        auto child = Current->CreateEntityWithID(UUID(900), "Addition", false);
        Current->SetParent(child, part, {}, false);
        Current->SetPrefabNodeRemoved(root, Key("node-A", 2), true);
        const auto nodeID = Scene::PrefabEntityUUID(rootID, "node-A");
        Current->DestroyEntity(Current->GetEntityByUUID(nodeID));
        EXPECT_FALSE(Current->GetEntityByUUID(nodeID));
        ASSERT_TRUE(Current->GetEntityByUUID(UUID(900)));
        EXPECT_EQ(Current->GetParent(child).GetUUID(), rootID);
        Current->SetPrefabNodeRemoved(root, "node-A", false);
        EXPECT_TRUE(Part(rootID, "node-A", 0));
        EXPECT_FALSE(Part(rootID, "node-A", 2));
        Current->UpdatePrefabs();
        EXPECT_EQ(Current->GetParent(child).GetUUID(), Scene::PrefabEntityUUID(rootID, Key("node-A", 0)));
        Current->DestroyEntity(root);
        EXPECT_FALSE(Current->GetEntityByUUID(UUID(900)));
        EXPECT_TRUE(Current->GetMeshEntities().empty());
    }

    TEST_F(PrefabSystem, DirectEditsRecordOverridesAndRevertingClearsThem)
    {
        // Bug: inspectors mutate components but never persist the changes, or no-op edits stay as overrides.
        auto root = Root();
        Ready();
        auto       part                                    = Part(root.GetUUID(), "node-A", 0);
        const auto base                                    = part.GetComponent<TransformComponent>();
        part.PatchComponent<TransformComponent>([&](auto& component) { component.Position.x = 7; });
        part.PatchComponent<MeshComponent>([&](auto& component) { component.Materials[0] = PartChoice; });
        Current->Capture();
        const auto& changes = root.GetComponent<PrefabInstanceComponent>().Overrides;
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes.at(Key("node-A", 0)).LocalTransform->Position.x, 7);
        EXPECT_EQ(changes.at(Key("node-A", 0)).Materials.at(0), PartChoice);
        part.PatchComponent<TransformComponent>([&](auto& component) { component = base; });
        part.PatchComponent<MeshComponent>([&](auto& component) { component.Materials[0] = Bound; });
        Current->UpdatePrefabs();
        EXPECT_TRUE(changes.empty());
        Current->SetPrefabMaterialOverride(root, Key("node-A", 0), 0, PartChoice);
        EXPECT_EQ(part.GetComponent<MeshComponent>().Materials[0], PartChoice);
        Current->ClearPrefabOverride(root, Key("node-A", 0));
        EXPECT_EQ(part.GetComponent<MeshComponent>().Materials[0], Bound);
        // Clearing must also handle an inspector edit made before the next frame.
        part.PatchComponent<MeshComponent>([&](auto& component) { component.Materials[0] = PartChoice; });
        Current->ClearPrefabOverride(root, Key("node-A", 0));
        EXPECT_EQ(part.GetComponent<MeshComponent>().Materials[0], Bound);
        part.PatchComponent<TransformComponent>([&](auto& component) { component.Position.x = 9; });
        Current->SetPrefabTransformOverride(root, Key("node-A", 0), base);
        EXPECT_EQ(part.GetComponent<TransformComponent>().GetTransform(), base.GetTransform());
    }

    TEST_F(PrefabSystem, SaveOmitsExpandedEntitiesAndReattachesOrdinaryChildrenOnReload)
    {
        // Bug: saves the expansion as ordinary entities, duplicates it on reload, or loses an attachment.
        auto root = Root();
        Ready();
        auto part  = Part(root.GetUUID(), "node-A", 1);
        auto child = Current->CreateEntityWithID(UUID(900), "Addition", false);
        Current->SetParent(child, part, {}, false);
        Current->SetPrefabMaterialOverride(root, Key("node-A", 0), 0, PartChoice);
        auto loaded = SaveLoad("compact.scene");
        auto yaml   = YAML::LoadFile((Directory / "compact.scene").string());
        ASSERT_EQ(yaml["Entities"].size(), 2u);
        EXPECT_TRUE(yaml["Entities"][0]["PrefabInstanceComponent"]);
        EXPECT_FALSE(yaml["Entities"][0]["ProductModelComponent"]);
        EXPECT_EQ(yaml["Entities"][1]["Parent"].as<uint64_t>(), uint64_t(part.GetUUID()));
        loaded->UpdatePrefabs(*Assets);
        EXPECT_EQ(loaded->GetMeshEntities().size(), 6u);
        EXPECT_EQ(loaded->GetParent(loaded->GetEntityByUUID(UUID(900))).GetUUID(), part.GetUUID());
        EXPECT_EQ(loaded->GetEntityByUUID(Part(root.GetUUID(), "node-A", 0).GetUUID())
                      .GetComponent<MeshComponent>()
                      .Materials[0],
                  PartChoice);
        EXPECT_EQ(loaded->Capture(), Current->Capture());
    }

    TEST_F(PrefabSystem, LegacyMigrationPreservesRootAndExactPartMeshAuthoredBoundPrecedence)
    {
        // Bug: migration drops a precedence level, chooses another section sharing a slot, or resets the root.
        auto parent = Current->CreateEntityWithID(UUID(50), "Parent", false);
        auto root   = Current->CreateEntityWithID(UUID(100), "Original model", false);
        Current->SetParent(root, parent, {}, false);
        root.PatchComponent<TransformComponent>([&](auto& component) { component.Position = {1, 2, 3}; });
        ProductModelComponent legacy {
            PrefabID, {{Bound, BoundChoice}, {Authored, SlotChoice}, {{Source, 30}, PartChoice}, {MeshID, MeshChoice}}};
        SetLegacyPending(root, legacy);
        auto child = Current->CreateEntityWithID(UUID(900), "Existing child", false);
        Current->SetParent(child, root, {}, false);
        Ready();
        EXPECT_EQ(root.GetUUID(), UUID(100));
        EXPECT_EQ(root.GetComponent<TagComponent>().Tag, "Original model");
        EXPECT_EQ(root.GetComponent<TransformComponent>().Position, glm::vec3(1, 2, 3));
        EXPECT_EQ(Current->GetParent(root).GetUUID(), UUID(50));
        EXPECT_EQ(Current->GetParent(child).GetUUID(), UUID(100));
        EXPECT_EQ(Part(UUID(100), "node-A", 0).GetComponent<MeshComponent>().Materials[0], PartChoice);
        EXPECT_EQ(Part(UUID(100), "node-A", 2).GetComponent<MeshComponent>().Materials[0], MeshChoice);
        legacy.SetOverride(MeshID, {});
        SetLegacyPending(root, legacy);
        Current->UpdatePrefabs();
        EXPECT_EQ(Part(UUID(100), "node-A", 2).GetComponent<MeshComponent>().Materials[0], SlotChoice);
        legacy.SetOverride(Authored, {});
        SetLegacyPending(root, legacy);
        Current->UpdatePrefabs();
        EXPECT_EQ(Part(UUID(100), "node-A", 2).GetComponent<MeshComponent>().Materials[0], BoundChoice);
        EXPECT_EQ(Part(UUID(100), "node-A", 1).GetComponent<MeshComponent>().Materials[1], Blue);
        EXPECT_FALSE(root.GetComponent<PrefabInstanceComponent>().LegacyPending);
        auto loaded = SaveLoad("migrated.scene");
        loaded->UpdatePrefabs(*Assets);
        EXPECT_EQ(loaded->Capture(), Current->Capture());
    }

    TEST_F(PrefabSystem, SavingBeforeConversionPreservesEveryLegacyOverrideUntilReady)
    {
        // Bug: saves only the prefab ID while asynchronous migration still needs the old override table.
        auto                  root = Current->CreateEntityWithID(UUID(100), "Pending legacy", false);
        ProductModelComponent legacy {PrefabID,
                                      {{{Source, 30}, PartChoice}, {Authored, SlotChoice}, {Bound, BoundChoice}}};
        SetLegacyPending(root, legacy);
        auto loaded = SaveLoad("pending.scene");
        auto yaml   = YAML::LoadFile((Directory / "pending.scene").string());
        ASSERT_EQ(yaml["Entities"][0]["PrefabInstanceComponent"]["LegacyPending"]["Overrides"].size(), 3u);
        const auto& pending = loaded->GetEntityByUUID(UUID(100)).GetComponent<PrefabInstanceComponent>();
        ASSERT_TRUE(pending.LegacyPending);
        EXPECT_EQ(pending.LegacyPending->Overrides.size(), 3u);
        ASSERT_TRUE(std::holds_alternative<Ref<const PrefabAsset>>(Assets->Load<PrefabAsset>(PrefabID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(Assets->Load<MeshAsset>(MeshID)));
        loaded->UpdatePrefabs(*Assets);
        EXPECT_FALSE(pending.LegacyPending);
        auto part = loaded->GetEntityByUUID(Scene::PrefabEntityUUID(UUID(100), Key("node-A", 0)));
        ASSERT_TRUE(part);
        EXPECT_EQ(part.GetComponent<MeshComponent>().Materials[0], PartChoice);
        auto other = loaded->GetEntityByUUID(Scene::PrefabEntityUUID(UUID(100), Key("node-A", 2)));
        EXPECT_EQ(other.GetComponent<MeshComponent>().Materials[0], SlotChoice);
    }

    TEST_F(PrefabSystem, LegacySceneLoadsAsPendingInstanceBeforeMeshDefaultsAreReady)
    {
        // Bug: the scene loader discards old overrides, or migration consumes them
        // using an empty/default material table while the CPU mesh is still loading.
        auto root = Current->CreateEntityWithID(UUID(100), "Legacy file", false);
        SetLegacyPending(root, ProductModelComponent {PrefabID, {{{Source, 30}, PartChoice}, {Authored, SlotChoice}}});
        SaveLoad("pending-input.scene");
        auto yaml                       = YAML::LoadFile((Directory / "pending-input.scene").string());
        auto entity                     = yaml["Entities"][0];
        entity["ProductModelComponent"] = YAML::Clone(entity["PrefabInstanceComponent"]["LegacyPending"]);
        entity.remove("PrefabInstanceComponent");
        {
            std::ofstream out(Directory / "old.scene");
            out << yaml;
        }
        auto loaded   = std::get<Ref<Scene>>(SceneSerializer::DeserializeChecked(Directory / "old.scene"));
        auto instance = loaded->GetEntityByUUID(UUID(100));
        ASSERT_TRUE(instance.HasComponent<PrefabInstanceComponent>());
        EXPECT_FALSE(instance.HasComponent<ProductModelComponent>());
        ASSERT_TRUE(instance.GetComponent<PrefabInstanceComponent>().LegacyPending);
        ASSERT_TRUE(std::holds_alternative<Ref<const PrefabAsset>>(Assets->Load<PrefabAsset>(PrefabID)));
        {
            std::lock_guard<std::mutex> lock(Provider->Mutex);
            Provider->Gated   = true;
            Provider->Entered = false;
        }
        loaded->UpdatePrefabs(*Assets);
        EXPECT_TRUE(loaded->GetMeshEntities().empty());
        EXPECT_TRUE(instance.GetComponent<PrefabInstanceComponent>().LegacyPending);
        EXPECT_TRUE(std::holds_alternative<std::monostate>(
            SceneSerializer(loaded).SerializeChecked(Directory / "still-pending.scene")));
        const auto saved = YAML::LoadFile((Directory / "still-pending.scene").string());
        EXPECT_EQ(saved["Entities"][0]["PrefabInstanceComponent"]["LegacyPending"]["Overrides"].size(), 2u);
        Provider->Release();
        ASSERT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(Assets->Load<MeshAsset>(MeshID)));
        loaded->UpdatePrefabs();
        EXPECT_FALSE(instance.GetComponent<PrefabInstanceComponent>().LegacyPending);
        const auto part = loaded->GetEntityByUUID(Scene::PrefabEntityUUID(UUID(100), Key("node-A", 0)));
        ASSERT_TRUE(part);
        EXPECT_EQ(part.GetComponent<MeshComponent>().Materials[0], PartChoice);
    }

    TEST_F(PrefabSystem, SnapshotPlayCopyAndUndoRedoRebuildCompleteInstances)
    {
        // Bug: snapshots capture expanded entities or restore an Expanded flag without rebuilding its children.
        const auto rootID = Root().GetUUID();
        Ready();
        SceneHistory history;
        history.Reset(*Current);
        auto       part                                 = Part(rootID, "node-A", 0);
        const auto partID                               = part.GetUUID();
        part.PatchComponent<MeshComponent>([&](auto& component) { component.Materials[0] = PartChoice; });
        ASSERT_TRUE(history.Record(*Current));
        ASSERT_TRUE(history.Undo(*Current));
        EXPECT_EQ(Current->GetEntityByUUID(partID).GetComponent<MeshComponent>().Materials[0], Bound);
        EXPECT_EQ(Current->GetMeshEntities().size(), 6u);
        ASSERT_TRUE(history.Redo(*Current));
        EXPECT_EQ(Current->GetEntityByUUID(partID).GetComponent<MeshComponent>().Materials[0], PartChoice);
        const auto snapshot = Current->Capture();
        ASSERT_EQ(snapshot.Entities.size(), 1u);
        Scene copy;
        copy.Restore(snapshot);
        EXPECT_EQ(copy.GetMeshEntities().size(), 6u);
        EXPECT_EQ(copy.GetEntityByUUID(partID).GetComponent<MeshComponent>().Materials[0], PartChoice);
        EXPECT_EQ(copy.Capture(), snapshot);
        ScenePlaySession play;
        play.Play(*Current);
        Current->GetEntityByUUID(partID).PatchComponent<TransformComponent>(
            [&](auto& component) { component.Position.x = 9; });
        play.Stop(*Current);
        EXPECT_EQ(Current->Capture(), snapshot);
        EXPECT_EQ(Current->GetMeshEntities().size(), 6u);
    }

    TEST_F(PrefabSystem, ExtractionKeepsStageTwoModelNodeSectionAndGeometryPassOrder)
    {
        // Bug: ECS mesh iteration reverses parts or interleaves each part's stencil/outline/debug passes.
        auto first                                            = Current->CreateEntityWithID(UUID(100), "First", false);
        first.AddComponent<PrefabInstanceComponent>().Prefab  = PrefabID;
        auto second                                           = Current->CreateEntityWithID(UUID(200), "Second", false);
        second.AddComponent<PrefabInstanceComponent>().Prefab = PrefabID;
        first.PatchComponent<TransformComponent>(
            [&](auto& component) { component = {{.13f, .27f, .39f}, {.17f, .23f, .37f}, {1.31f, .83f, 1.17f}}; });
        second.PatchComponent<TransformComponent>(
            [&](auto& component) { component = {{-.17f, .29f, .41f}, {.13f, .31f, .47f}, {.71f, 1.23f, 1.43f}}; });
        Ready();
        const auto     phaseTwo = std::get<ModelInstances>(FlattenModel(Data));
        RenderScene    render(Current);
        RenderResource settings;
        settings.Piplinestate.EnableStencil         = settings.Piplinestate.EnableOutline =
            settings.Piplinestate.EnableDebugNormal = true;
        render.SetRenderResource(settings);
        RenderView view;
        render.Extract(view);
        ASSERT_EQ(view.Meshes.size(), 12u);
        for (size_t model = 0; model < 2; ++model)
            for (size_t node = 0; node < 2; ++node)
                for (size_t section = 0; section < 3; ++section)
                {
                    const auto& draw = view.Meshes[model * 6 + node * 3 + section];
                    const UUID  root = model == 0 ? UUID(200) : UUID(100);
                    EXPECT_EQ(draw.EntityID,
                              Scene::PrefabEntityUUID(root, Key(node == 0 ? "node-A" : "node-B", section)));
                    EXPECT_EQ(draw.InstanceRoot, root);
                    EXPECT_EQ(draw.Mesh.Section, section);
                    const auto rootWorld = Current->GetWorldTransform(Current->GetEntityByUUID(root));
                    EXPECT_EQ(draw.Transform, rootWorld * phaseTwo.Instances[node * 3 + section].Transform);
                }
        ASSERT_EQ(view.Geometry.size(), 36u);
        for (size_t root = 0; root < 2; ++root)
            for (size_t pass = 0; pass < 3; ++pass)
                for (size_t part = 0; part < 6; ++part)
                {
                    const auto& geometry = view.Geometry[root * 18 + pass * 6 + part];
                    EXPECT_EQ(geometry.ShaderName,
                              pass == 0 ? "StencilShader" :
                              pass == 1 ? "OutLineShader" :
                                          "DebugNormalShader");
                    EXPECT_EQ(geometry.EntityID, view.Meshes[root * 6 + part].EntityID);
                }
    }

    TEST_F(PrefabSystem, WorldBoundsTransformEachSectionAndMergeAllAncestors)
    {
        // Bug: transforms the merged mesh box, ignores parent transforms, or never merges descendants.
        auto parent                               = Current->CreateEntityWithID(UUID(50), "Rotated parent", false);
        parent.PatchComponent<TransformComponent>(
            [&](auto& component) { component = {{4, 5, 6}, {0, 0, glm::radians(37.0f)}, {2, 3, 1}}; });
        auto root                                 = Root();
        Current->SetParent(root, parent, {}, false);
        root.PatchComponent<TransformComponent>(
            [&](auto& component) { component = {{1, 2, 3}, {0, glm::radians(23.0f), 0}, {1, 2, 3}}; });
        Ready();
        const auto rootWorld = parent.GetComponent<TransformComponent>().GetTransform() *
                               root.GetComponent<TransformComponent>().GetTransform();
        Bounds     expected;
        for (size_t node = 0; node < 2; ++node)
            for (unsigned section = 0; section < 3; ++section)
            {
                const auto part = Part(root.GetUUID(), node == 0 ? "node-A" : "node-B", section);
                ASSERT_TRUE(part);
                const auto local = glm::translate(glm::mat4(1), node == 0 ? glm::vec3(10, 0, 0) : glm::vec3(0, 4, 0));
                const auto world = rootWorld * local;
                EXPECT_EQ(Current->GetWorldTransform(part), world);
                const auto bounds = Mesh.Sections[section].LocalBounds.Transformed(world);
                EXPECT_EQ(Current->GetWorldBounds(part).Min, bounds.Min);
                EXPECT_EQ(Current->GetWorldBounds(part).Max, bounds.Max);
                expected.Expand(bounds);
            }
        EXPECT_EQ(Current->GetWorldBounds(root).Min, expected.Min);
        EXPECT_EQ(Current->GetWorldBounds(root).Max, expected.Max);
        EXPECT_EQ(Current->GetWorldBounds(parent).Min, expected.Min);
        EXPECT_EQ(Current->GetWorldBounds(parent).Max, expected.Max);
        const auto placement = Current->GetPrefabBounds(PrefabID, rootWorld);
        EXPECT_EQ(placement.Min, expected.Min);
        EXPECT_EQ(placement.Max, expected.Max);
        const auto slots = Current->GetMeshSlots(Part(root.GetUUID(), "node-A", 0));
        ASSERT_EQ(slots.size(), 2u);
        EXPECT_EQ(slots[0].DefaultMaterial, Bound);
        EXPECT_EQ(slots[0].AuthoredMaterial, Authored);
        parent.PatchComponent<TransformComponent>([&](auto& component) { component.Position.x += 7; });
        Current->UpdatePrefabs();
        Bounds     moved;
        const auto movedRoot = parent.GetComponent<TransformComponent>().GetTransform() *
                               root.GetComponent<TransformComponent>().GetTransform();
        for (const auto offset : {glm::vec3(10, 0, 0), glm::vec3(0, 4, 0)})
            for (const auto& section : Mesh.Sections)
                moved.Expand(section.LocalBounds.Transformed(movedRoot * glm::translate(glm::mat4(1), offset)));
        EXPECT_EQ(Current->GetWorldBounds(root).Min, moved.Min);
        EXPECT_EQ(Current->GetWorldBounds(root).Max, moved.Max);
    }
} // namespace
