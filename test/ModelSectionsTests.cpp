#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ImportedModelData.h"
#include "Kans3D/Asset/Importer/ModelImporter.h"
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Renderer/RenderView.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/SceneSerializer.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;

    class ModelSections : public testing::Test
    {
    protected:
        fs::path                                   Root;
        ImportContext                              Context;
        const std::array<std::array<float, 3>, 12> Positions {{{-3, 0, 0},
                                                               {-1, 0, 0},
                                                               {-3, 2, 0},
                                                               {1, -2, 1},
                                                               {4, -2, 1},
                                                               {1, 1, 1},
                                                               {-2, 3, -1},
                                                               {0, 3, -1},
                                                               {-2, 5, -1},
                                                               {10, 0, 0},
                                                               {11, 0, 0},
                                                               {10, 1, 0}}};
        void                                       Write(const fs::path& path, const std::string& text)
        {
            std::ofstream out(path, std::ios::binary);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            ASSERT_TRUE(out.good());
        }
        void SetUp() override
        {
            Root = fs::temp_directory_path() / ("kans-sections-" + SourceAssetID::Generate().ToString());
            ASSERT_TRUE(fs::create_directory(Root));
            Context.SourceID         = SourceAssetID::FromString("1234567890abcdef");
            Context.SourceFile       = Root / "sections.gltf";
            Context.StagingDirectory = Root / "stage";
            ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
            std::string bytes;
            for (const auto& position : Positions)
                for (float value : position)
                {
                    uint32_t bits;
                    std::memcpy(&bits, &value, 4);
                    for (unsigned i = 0; i < 4; ++i)
                        bytes.push_back(char(bits >> (i * 8)));
                }
            Write(Root / "geometry.bin", bytes);
            // Red/Blue/Red primitives are one glTF mesh. It is instanced with
            // different transforms; a second, same-named mesh must stay separate.
            Write(Context.SourceFile, R"({"asset":{"version":"2.0"},
"buffers":[{"uri":"geometry.bin","byteLength":144}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":144}],
"accessors":[
{"bufferView":0,"byteOffset":0,"componentType":5126,"count":3,"type":"VEC3","min":[-3,0,0],"max":[-1,2,0]},
{"bufferView":0,"byteOffset":36,"componentType":5126,"count":3,"type":"VEC3","min":[1,-2,1],"max":[4,1,1]},
{"bufferView":0,"byteOffset":72,"componentType":5126,"count":3,"type":"VEC3","min":[-2,3,-1],"max":[0,5,-1]},
{"bufferView":0,"byteOffset":108,"componentType":5126,"count":3,"type":"VEC3","min":[10,0,0],"max":[11,1,0]}],
"materials":[{"name":"Red"},{"name":"Blue"},{"name":"Yellow"}],
"meshes":[{"name":"Body","primitives":[
{"attributes":{"POSITION":0},"material":0},
{"attributes":{"POSITION":1},"material":1},
{"attributes":{"POSITION":2},"material":0}]},
{"name":"Body","primitives":[{"attributes":{"POSITION":3},"material":2}]}],
"nodes":[{"name":"First","mesh":0,"translation":[100,200,300],"scale":[2,3,4]},
{"name":"Second","mesh":0,"translation":[-100,0,0]},
{"name":"Other","mesh":1}],"scenes":[{"nodes":[0,1,2]}],"scene":0})");
            auto inputs = ModelImporter().DiscoverInputs(Context);
            ASSERT_TRUE(std::holds_alternative<ImportInputs>(inputs));
            for (const auto& path : std::get<ImportInputs>(inputs).SourceFiles)
                Context.SourceInputs.emplace(path, SourceAssetID::Generate());
        }
        void TearDown() override
        {
            std::error_code error;
            fs::remove_all(Root, error);
            EXPECT_FALSE(error);
        }
        std::vector<std::byte> Read(const ProductDescriptor& product)
        {
            std::ifstream          input(Context.StagingDirectory / product.StagedFile, std::ios::binary);
            std::string            text {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            std::vector<std::byte> bytes(text.size());
            std::memcpy(bytes.data(), text.data(), text.size());
            return bytes;
        }
        struct Products
        {
            ImportResult                               Result;
            ModelData                                  Model;
            std::map<AssetID, MeshData>                Meshes;
            std::map<uint64_t, std::vector<std::byte>> Bytes;
        };
        Products Import()
        {
            auto imported = ModelImporter().Import(Context);
            if (auto* error = std::get_if<AssetError>(&imported))
                throw std::runtime_error(error->Message);
            Products products;
            products.Result = std::get<ImportResult>(std::move(imported));
            for (const auto& product : products.Result.Products)
            {
                auto bytes                      = Read(product);
                products.Bytes[product.LocalID] = bytes;
                if (product.Type == AssetType::Model)
                    products.Model = std::get<ModelData>(DecodeModelProduct(bytes.data(), bytes.size()));
                if (product.Type == AssetType::StaticMesh)
                    products.Meshes.emplace(AssetID(Context.SourceID, product.LocalID),
                                            std::get<MeshData>(DecodeMeshProduct(bytes.data(), bytes.size())));
            }
            return products;
        }
        static void Box(const Bounds& bounds, glm::vec3 low, glm::vec3 high)
        {
            EXPECT_EQ(bounds.Min, low);
            EXPECT_EQ(bounds.Max, high);
        }
    };

    TEST_F(ModelSections, GltfPrimitivesGroupByOrderedNodeTupleWithExactSlotsIndicesAndLocalBounds)
    {
        // Bugs: grouping by material/name merges unrelated objects; adding indices
        // without the vertex offset addresses the first primitive's vertices;
        // sorting slots/sections changes authored order; applying nodes to AABBs.
        const auto products = Import();
        ASSERT_EQ(products.Meshes.size(), 2u);
        ASSERT_EQ(products.Model.Nodes.size(), 4u);
        ASSERT_EQ(products.Model.Nodes[1].Meshes.size(), 1u);
        EXPECT_EQ(products.Model.Nodes[1].Meshes, products.Model.Nodes[2].Meshes);
        EXPECT_NE(products.Model.Nodes[1].Meshes, products.Model.Nodes[3].Meshes);
        const auto& mesh = products.Meshes.at(products.Model.Nodes[1].Meshes[0]);
        ASSERT_EQ(mesh.Sections.size(), 3u);
        ASSERT_EQ(mesh.MaterialSlots.size(), 2u);
        EXPECT_EQ(mesh.Indices, (std::vector<uint32_t> {0, 1, 2, 3, 4, 5, 6, 7, 8}));
        ASSERT_EQ(mesh.Vertices.size(), 9u);
        for (size_t i = 0; i < 9; ++i)
            EXPECT_EQ(mesh.Vertices[i].Position, Positions[i]);
        for (uint32_t i = 0; i < 3; ++i)
        {
            EXPECT_EQ(mesh.Sections[i].FirstIndex, i * 3);
            EXPECT_EQ(mesh.Sections[i].IndexCount, 3u);
            EXPECT_EQ(mesh.Sections[i].MaterialSlot, i == 1 ? 1u : 0u);
        }
        EXPECT_EQ(mesh.MaterialSlots[0].Name, "Red");
        EXPECT_EQ(mesh.MaterialSlots[0].ObjectKey, "material:3:Red");
        EXPECT_EQ(mesh.MaterialSlots[1].Name, "Blue");
        EXPECT_EQ(mesh.MaterialSlots[1].ObjectKey, "material:4:Blue");
        for (const auto& slot : mesh.MaterialSlots)
        {
            EXPECT_EQ(slot.AuthoredMaterial, AssetID(Context.SourceID, products.Result.LocalIDs.at(slot.ObjectKey)));
            EXPECT_EQ(slot.DefaultMaterial, slot.AuthoredMaterial);
        }
        Box(mesh.Sections[0].LocalBounds, {-3, 0, 0}, {-1, 2, 0});
        Box(mesh.Sections[1].LocalBounds, {1, -2, 1}, {4, 1, 1});
        Box(mesh.Sections[2].LocalBounds, {-2, 3, -1}, {0, 5, -1});
        Box(mesh.LocalBounds, {-3, -2, -1}, {4, 5, 1});
        const auto& other = products.Meshes.at(products.Model.Nodes[3].Meshes[0]);
        ASSERT_EQ(other.Sections.size(), 1u);
        EXPECT_EQ(other.MaterialSlots[0].Name, "Yellow");
        Box(other.LocalBounds, {10, 0, 0}, {11, 1, 0});
        auto flattened = FlattenModel(products.Model);
        ASSERT_TRUE(std::holds_alternative<ModelInstances>(flattened));
        const auto& draws = std::get<ModelInstances>(flattened).Instances;
        ASSERT_EQ(draws.size(), 7u);
        for (size_t i = 0; i < 6; ++i)
        {
            EXPECT_EQ(draws[i].Mesh, products.Model.Nodes[1].Meshes[0]);
            EXPECT_EQ(draws[i].Section, i % 3);
            EXPECT_EQ(draws[i].Transform[3].x, i < 3 ? 100 : -100);
        }
        EXPECT_EQ(draws[6].Mesh, products.Model.Nodes[3].Meshes[0]);
        EXPECT_EQ(draws[6].Section, 0u);
        Context.StagingDirectory = Root / "fresh-again";
        ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
        // No previous map on either import: nondeterministic traversal/grouping
        // would change section order, allocated IDs or the encoded product bytes.
        const auto again = Import();
        EXPECT_EQ(again.Result.LocalIDs, products.Result.LocalIDs);
        EXPECT_EQ(again.Bytes, products.Bytes);
    }

    TEST_F(ModelSections, ReimportRetainsV12PartKeysSceneOverridesRemapsAndDeterministicProductBytes)
    {
        // Bug: replacing per-aiMesh IDs loses the third (same-material) part, or
        // sends its override to another section; reimport changes group identity.
        auto parsed = ParseModelCpu(Context.SourceFile);
        ASSERT_TRUE(std::holds_alternative<ImportedModelData>(parsed));
        const auto& source = std::get<ImportedModelData>(parsed);
        ASSERT_EQ(source.Meshes.size(), 4u);
        const std::array<std::string, 4> names {"Body-0", "Body-1", "Body-2", "Body"};
        for (size_t i = 0; i < names.size(); ++i)
            EXPECT_EQ(source.Meshes[i].Name, names[i]);
        const std::vector<std::string> oldKeys {"mesh:6:Body-0:material:3:Red",
                                                "mesh:6:Body-1:material:4:Blue",
                                                "mesh:6:Body-2:material:3:Red",
                                                "mesh:4:Body:material:6:Yellow"};
        // These literal keys are the previously published v12 identity contract.
        Context.PreviousLocalIDs = {
            {"model:root", 1}, {"material:3:Red", 2}, {"material:4:Blue", 3}, {"material:6:Yellow", 4}};
        for (size_t i = 0; i < oldKeys.size(); ++i)
            Context.PreviousLocalIDs.emplace(oldKeys[i], 20 + i);
        const AssetID bound {SourceAssetID::FromString("fedcba0987654321"), 1};
        Context.Settings["material.material:3:Red"] = bound.ToString();
        Context.ProductBytes.emplace(bound, std::get<std::vector<std::byte>>(EncodeMaterialProduct(MaterialData {})));
        const auto first = Import();
        ASSERT_EQ(first.Model.SectionIdentities.size(), 4u);
        for (size_t i = 0; i < 4; ++i)
        {
            const auto& identity = first.Model.SectionIdentities[i];
            EXPECT_EQ(identity.ObjectKey, oldKeys[i]);
            EXPECT_EQ(identity.OldMesh, AssetID(Context.SourceID, 20 + i));
            EXPECT_EQ(identity.Section, i < 3 ? i : 0);
            EXPECT_EQ(identity.Mesh, first.Model.Nodes[i < 3 ? 1 : 3].Meshes[0]);
        }
        const auto& mesh = first.Meshes.at(first.Model.Nodes[1].Meshes[0]);
        EXPECT_EQ(mesh.MaterialSlots[0].DefaultMaterial, bound);
        EXPECT_EQ(mesh.MaterialSlots[0].AuthoredMaterial, AssetID(Context.SourceID, 2));
        ProductModelComponent component;
        component.Model = {Context.SourceID, 1};
        const AssetID partChoice {SourceAssetID::FromString("aaaaaaaaaaaaaaaa"), 1};
        const AssetID slotChoice {SourceAssetID::FromString("bbbbbbbbbbbbbbbb"), 1};
        const AssetID boundChoice {SourceAssetID::FromString("cccccccccccccccc"), 1};
        component.Overrides = {
            {{Context.SourceID, 22}, partChoice}, {{Context.SourceID, 2}, slotChoice}, {bound, boundChoice}};
        const auto sceneFile = Root / "old.scene";
        auto       scene     = CreateRef<Scene>("old scene");
        scene->CreateEntityWithID(UUID(101), "Body", true).AddComponent<ProductModelComponent>(component);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SceneSerializer(scene).SerializeChecked(sceneFile)));
        std::ifstream     oldInput(sceneFile, std::ios::binary);
        const std::string oldBytes {std::istreambuf_iterator<char>(oldInput), std::istreambuf_iterator<char>()};
        const auto manifest = std::get<std::string>(EncodeLocalIDManifest({Context.SourceID, first.Result.LocalIDs}));
        Context.PreviousLocalIDs = std::get<LocalIDManifest>(DecodeLocalIDManifest(manifest, Context.SourceID)).IDs;
        Context.StagingDirectory = Root / "stage2";
        ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
        const auto second = Import();
        EXPECT_EQ(second.Result.LocalIDs, first.Result.LocalIDs);
        EXPECT_EQ(second.Bytes, first.Bytes);
        auto loaded = SceneSerializer::DeserializeChecked(sceneFile);
        ASSERT_TRUE(std::holds_alternative<Ref<Scene>>(loaded));
        const auto loadedScene = std::get<Ref<Scene>>(loaded);
        auto       restored    = loadedScene->GetEntityByUUID(UUID(101)).GetComponent<ProductModelComponent>();
        auto       instances   = std::get<ModelInstances>(FlattenModel(second.Model));
        RenderView view;
        for (const auto& instance : instances.Instances)
        {
            const auto& data    = second.Meshes.at(instance.Mesh);
            const auto& section = data.Sections[instance.Section];
            const auto& slot    = data.MaterialSlots[section.MaterialSlot];
            ExtractModelPart(view,
                             restored,
                             glm::mat4(1),
                             ModelStencilMode::Disabled,
                             SceneViewMode::Lit,
                             instance,
                             {section.LocalBounds, slot.DefaultMaterial, slot.AuthoredMaterial});
        }
        ASSERT_EQ(view.Draws.size(), 7u);
        const std::vector<AssetID> expected {slotChoice,
                                             {Context.SourceID, 3},
                                             partChoice,
                                             slotChoice,
                                             {Context.SourceID, 3},
                                             partChoice,
                                             {Context.SourceID, 4}};
        for (size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(view.Draws[i].Material, expected[i]);
            EXPECT_EQ(view.Draws[i].Section, i < 6 ? i % 3 : 0);
        }
        restored.SetOverride({Context.SourceID, 2}, {});
        EXPECT_EQ(restored.ResolveMaterial({Context.SourceID, 20}, bound, {Context.SourceID, 2}), boundChoice);
        restored.SetOverride(bound, {});
        EXPECT_EQ(restored.ResolveMaterial({Context.SourceID, 20}, bound, {Context.SourceID, 2}), bound);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            SceneSerializer(loadedScene).SerializeChecked(Root / "saved.scene")));
        std::ifstream saved(Root / "saved.scene", std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(saved), {}), oldBytes);
    }
    TEST_F(ModelSections, EditorCompatibilityViewsExposeEveryPartAndRefreshBindingsAfterReimport)
    {
        // Bug: an unchanged Properties panel sees only the first material/part,
        // or its synthetic part view retains the previous remap after reimport.
        class Provider final : public IAssetProvider
        {
        public:
            std::map<std::string, std::vector<std::byte>>    Files;
            std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord& record) const override
            {
                return Files.at(record.Location.RelativeFile.ToString());
            }
        };
        const auto first       = Import();
        auto       provider    = CreateRef<Provider>();
        const auto registryFor = [&](const Products& products, const std::string& generation) {
            std::vector<ProductRecord> records;
            for (const auto& product : products.Result.Products)
            {
                ProductRecord record;
                record.Metadata.assetID       = {Context.SourceID, product.LocalID};
                record.Metadata.Type          = product.Type;
                record.Metadata.Dependencies  = product.Dependencies;
                record.Location.RelativeFile  = AssetPath(generation + "/" + product.StagedFile.u8string());
                const auto& bytes             = products.Bytes.at(product.LocalID);
                record.Location.ByteSize      = bytes.size();
                record.Location.ContentDigest = ComputeContentDigest(bytes);
                provider->Files.emplace(record.Location.RelativeFile.ToString(), bytes);
                records.push_back(record);
            }
            return CreateRef<const AssetRegistry>(std::get<AssetRegistry>(AssetRegistry::FromProducts(records)));
        };
        AssetManager assets(provider, registryFor(first, "first"));
        const auto   modelID = AssetID(Context.SourceID, first.Result.LocalIDs.at("model:root"));
        const auto   model   = std::get<Ref<const ModelAsset>>(assets.Load<ModelAsset>(modelID));
        ASSERT_EQ(model->GetMeshData().Nodes[1].Meshes.size(), 1u);
        ASSERT_EQ(model->GetData().Nodes[1].Meshes.size(), 3u);
        EXPECT_EQ(model->GetData().Nodes[1].Meshes, model->GetData().Nodes[2].Meshes);
        for (size_t i = 0; i < first.Model.SectionIdentities.size(); ++i)
        {
            const auto& identity = first.Model.SectionIdentities[i];
            const auto  old      = std::get<Ref<const MeshAsset>>(assets.Load<MeshAsset>(identity.OldMesh));
            EXPECT_EQ(old->assetID, identity.OldMesh);
            EXPECT_EQ(old->GetData().Indices, (std::vector<uint32_t> {0, 1, 2}));
            ASSERT_EQ(old->GetData().Vertices.size(), 3u);
            for (size_t v = 0; v < 3; ++v)
                EXPECT_EQ(old->GetData().Vertices[v].Position, Positions[i * 3 + v]);
            const auto& shared  = first.Meshes.at(identity.Mesh);
            const auto& section = shared.Sections[identity.Section];
            const auto& slot    = shared.MaterialSlots[section.MaterialSlot];
            EXPECT_EQ(old->GetData().Material, slot.DefaultMaterial);
            EXPECT_EQ(old->GetData().AuthoredMaterial, slot.AuthoredMaterial);
            EXPECT_EQ(old->GetData().LocalBounds.Min, section.LocalBounds.Min);
            EXPECT_EQ(old->GetData().LocalBounds.Max, section.LocalBounds.Max);
            EXPECT_TRUE(std::holds_alternative<std::monostate>(ValidateMeshData(old->GetData())));
        }
        Context.PreviousLocalIDs = first.Result.LocalIDs;
        Context.StagingDirectory = Root / "stage2";
        ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
        const AssetID replacement {SourceAssetID::FromString("fedcba0987654321"), 1};
        Context.Settings["material.material:3:Red"] = replacement.ToString();
        Context.ProductBytes.emplace(replacement,
                                     std::get<std::vector<std::byte>>(EncodeMaterialProduct(MaterialData {})));
        const auto second = Import();
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(assets.SetRegistry(registryFor(second, "second"))));
        ASSERT_TRUE(std::holds_alternative<Ref<const ModelAsset>>(assets.Load<ModelAsset>(modelID)));
        for (size_t i : {0u, 2u})
        {
            const auto& identity = second.Model.SectionIdentities[i];
            const auto  old      = std::get<Ref<const MeshAsset>>(assets.Load<MeshAsset>(identity.OldMesh));
            EXPECT_EQ(old->GetData().Material, replacement);
            EXPECT_EQ(old->GetData().AuthoredMaterial,
                      AssetID(Context.SourceID, second.Result.LocalIDs.at("material:3:Red")));
        }
    }

    TEST_F(ModelSections, GyLightSectionDataAndDrawInputsAreBitIdenticalToCapturedStageOneImport)
    {
        // Bugs: changing attributes, triangle/part/slot order, scale or hierarchy
        // while merging parts. SHA-256 expectations below were captured by running
        // the actual ModelImporter and KMSH v2/KMOD v1 codecs from 00d8f82 on the
        // bundled FBX, with these fixed source and texture IDs (not the new encoder).
        Context.SourceID   = SourceAssetID::FromString("1111111111111111");
        Context.SourceFile = fs::path(KANS_TEST_ASSET_DIR) / "GY_Light/GY_Light.fbx";
        Context.SourceInputs.clear();
        Context.PreviousLocalIDs = {{"material:4:face", 1},
                                    {"material:4:hair", 2},
                                    {"material:4:skin", 3},
                                    {"material:7:clothes", 4},
                                    {"material:9:expretion", 5},
                                    {"mesh:6:甘雨:material:4:face", 6},
                                    {"mesh:6:甘雨:material:4:hair", 7},
                                    {"mesh:6:甘雨:material:4:skin", 8},
                                    {"mesh:6:甘雨:material:7:clothes", 9},
                                    {"mesh:6:甘雨:material:9:expretion", 10},
                                    {"model:root", 11}};
        auto discovered          = ModelImporter().DiscoverInputs(Context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(discovered));
        const auto  textureSource = SourceAssetID::FromString("2222222222222222");
        const auto& inputs        = std::get<ImportInputs>(discovered);
        for (const auto& path : inputs.SourceFiles)
            Context.SourceInputs.emplace(path, textureSource);
        uint64_t texture = 1;
        for (const auto& path : inputs.ProductSources)
            Context.ProductsBySource.emplace(path, AssetID(textureSource, texture++));
        const auto products = Import();
        ASSERT_EQ(products.Meshes.size(), 1u);
        ASSERT_EQ(products.Model.SectionIdentities.size(), 5u);
        const auto& mesh = products.Meshes.begin()->second;
        ASSERT_EQ(mesh.Sections.size(), 5u);
        ASSERT_EQ(mesh.MaterialSlots.size(), 5u);
        struct Capture
        {
            uint32_t                OldPart, Material, Vertices, Indices;
            const char*             VertexDigest;
            const char*             IndexDigest;
            std::array<uint32_t, 6> BoundsBits;
        };
        const std::array<Capture, 5>     captures {{
            {6,
             1,
             2378,
             8988,
             "78d051817e6c8e6069bda986dbafb6b7ee42e2c49ecc4f54fb2e75f81e9f3564",
             "454a41949cdd32c5ad3f131792eba565ec5e3755774ec46a5c943c34cdc98e64",
             {0xbf825f02u, 0xbfbbdd2du, 0x41844e23u, 0x3f825f09u, 0xbc3cbfdeu, 0x41971515u}},
            {7,
             2,
             3325,
             12822,
             "bcfe4368b8e0e228a970204a781586b61e07a630502e373c7ef6d0399c71b035",
             "adb9cf6bac0e9891945ebbd1d9e14cd364b199fa5499164498b1ed57f14e8fa3",
             {0xc002efeeu, 0xbfcc64cfu, 0x412402efu, 0x4002f6f3u, 0x405b153fu, 0x419f3eafu}},
            {9,
             4,
             11458,
             38085,
             "1b31c720269992169f6d19ca1d0b037bc445221ab56c3cb846417f1d6422bb4d",
             "ac1c485898da8f4f177b93664ac23eed73f6ac89496dfcbc4168a46f68001f9e",
             {0xc0d14ffbu, 0xc004f222u, 0x39dc1605u, 0x40d1539fu, 0x4063c96du, 0x418e7843u}},
            {8,
             3,
             834,
             3258,
             "fcbc6a7778ce10852ef0d7491b46dd19c4106cb8f3ca7cb474ee6aa94c5b00bd",
             "9c5dd2c22d4d2ed91082aef60fe2522ea724ed45eb26543eca5e820c735181cc",
             {0xc0a01e85u, 0xbf608858u, 0x4143f3e3u, 0x40a02227u, 0x3f3332c5u, 0x418e7843u}},
            {10,
             5,
             48,
             180,
             "c94d01c2f14377ffb4e93d2ccb0d8918b0d0fcf0a00135c452db8022d50f3e22",
             "c76a0b66bb9b7b498d8cf8e81e838c2097559dabe4b9026ff8d7d630632a96b0",
             {0xbf2c60e1u, 0xbf128a54u, 0x4188d721u, 0x3f2c0a9fu, 0xbee1150bu, 0x418ae178u}},
        }};
        const std::array<const char*, 5> materialDigests {{
            "682cd9db12da97556b6211f40eeafa9d5c56bbef18c5094a7bdf38ff00a620e5",
            "2c9769aff7d6302b63b4a64d9542dedc48819b28ffcc94ca70b766182506d9f3",
            "5dafa1070252809940ba912076a324917aa1a3a69cdc29d2f79529257d5d507f",
            "71ac92f6e951cefc4cc8cb3bda214ff2d260653ddd1530b71a987bcf5c42b872",
            "487381483a0b6f2ff9be9bccb184b6897736a6defb451820d34293a6fc3963c5",
        }};
        // Encode comparison streams explicitly in little endian, as in the old products.
        const auto integer = [](std::vector<std::byte>& bytes, uint64_t value, size_t size) {
            for (size_t i = 0; i < size; ++i)
                bytes.push_back(std::byte((value >> (i * 8)) & 255));
        };
        const auto floats = [&](std::vector<std::byte>& bytes, const auto& values) {
            for (float value : values)
            {
                uint32_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                integer(bytes, bits, 4);
            }
        };
        uint32_t base = 0, firstIndex = 0;
        for (size_t i = 0; i < captures.size(); ++i)
        {
            SCOPED_TRACE(i);
            const auto& captured = captures[i];
            const auto& section  = mesh.Sections[i];
            EXPECT_EQ(section.FirstIndex, firstIndex);
            EXPECT_EQ(section.IndexCount, captured.Indices);
            EXPECT_EQ(section.MaterialSlot, i);
            EXPECT_EQ(products.Model.SectionIdentities[i].OldMesh, AssetID(Context.SourceID, captured.OldPart));
            EXPECT_EQ(products.Model.SectionIdentities[i].Section, i);
            const auto& slot = mesh.MaterialSlots[section.MaterialSlot];
            EXPECT_EQ(slot.DefaultMaterial, AssetID(Context.SourceID, captured.Material));
            EXPECT_EQ(slot.AuthoredMaterial, slot.DefaultMaterial);
            EXPECT_EQ(ComputeContentDigest(products.Bytes.at(captured.Material)),
                      materialDigests[captured.Material - 1]);
            std::vector<std::byte> vertices, indices;
            ASSERT_LE(base + captured.Vertices, mesh.Vertices.size());
            for (uint32_t j = 0; j < captured.Vertices; ++j)
            {
                const auto& vertex = mesh.Vertices[base + j];
                floats(vertices, vertex.Position);
                floats(vertices, vertex.Normal);
                floats(vertices, vertex.UV);
                floats(vertices, vertex.Color);
                floats(vertices, vertex.Tangent);
            }
            ASSERT_LE(firstIndex + captured.Indices, mesh.Indices.size());
            for (uint32_t j = 0; j < captured.Indices; ++j)
            {
                const auto index = mesh.Indices[firstIndex + j];
                ASSERT_GE(index, base);
                ASSERT_LT(index, base + captured.Vertices);
                integer(indices, index - base, 4);
            }
            EXPECT_EQ(ComputeContentDigest(vertices), captured.VertexDigest);
            EXPECT_EQ(ComputeContentDigest(indices), captured.IndexDigest);
            for (int axis = 0; axis < 3; ++axis)
            {
                uint32_t low, high;
                std::memcpy(&low, &section.LocalBounds.Min[axis], 4);
                std::memcpy(&high, &section.LocalBounds.Max[axis], 4);
                EXPECT_EQ(low, captured.BoundsBits[axis]);
                EXPECT_EQ(high, captured.BoundsBits[axis + 3]);
            }
            base += captured.Vertices;
            firstIndex += captured.Indices;
        }
        EXPECT_EQ(base, mesh.Vertices.size());
        EXPECT_EQ(firstIndex, mesh.Indices.size());
        const auto oldView = ModelAsset(products.Model).GetData();
        ASSERT_EQ(oldView.Nodes.size(), 219u);
        std::vector<std::byte> nodes;
        for (const auto& node : oldView.Nodes)
        {
            integer(nodes, node.Name.size(), 4);
            for (unsigned char c : node.Name)
                nodes.push_back(std::byte(c));
            integer(nodes, uint64_t(node.Parent), 8);
            floats(nodes, node.LocalTransform);
            integer(nodes, node.Meshes.size(), 4);
            for (auto id : node.Meshes)
            {
                integer(nodes, id.GetSourceID().GetUUID(), 8);
                integer(nodes, id.GetLocalID(), 8);
            }
        }
        EXPECT_EQ(ComputeContentDigest(nodes), "b6a2f376bcf12bec0fbf313309a886d3e9b79ec59f2fcdd4437e8e8a3bb695b4");
        const auto instances = std::get<ModelInstances>(FlattenModel(products.Model));
        ASSERT_EQ(instances.Instances.size(), 5u);
        std::vector<std::byte> transforms;
        for (size_t i = 0; i < instances.Instances.size(); ++i)
        {
            const auto& draw = instances.Instances[i];
            EXPECT_EQ(draw.Part, AssetID(Context.SourceID, captures[i].OldPart));
            EXPECT_EQ(draw.Section, i);
            const auto normal = glm::transpose(glm::inverse(draw.Transform));
            for (const auto& matrix : {draw.Transform, normal})
                for (int column = 0; column < 4; ++column)
                    floats(transforms,
                           std::array<float, 4> {
                               matrix[column][0], matrix[column][1], matrix[column][2], matrix[column][3]});
        }
        EXPECT_EQ(ComputeContentDigest(transforms), "f2ad9b6b5e954d3e1c2ac486c731b9cc1c081c839b95edad9085a71208e22c32");
    }
} // namespace
