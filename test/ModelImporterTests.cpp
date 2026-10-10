#include <algorithm>
#include <cstring>
#include <fstream>
#include <future>
#include <iterator>
#include <set>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ImportedModelData.h"
#include "Kans3D/Asset/Importer/ModelImporter.h"
#include "Kans3D/Asset/Loader/Loader.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;

    class ModelImporterTest : public ::testing::Test
    {
    protected:
        fs::path      Workspace;
        ImportContext Context;
        void          SetUp() override
        {
            Workspace = fs::temp_directory_path() / ("Kans3D-ModelImport-" + SourceAssetID::Generate().ToString());
            ASSERT_TRUE(fs::create_directory(Workspace));
            Context.SourceID         = SourceAssetID::Generate();
            Context.SourceFile       = Workspace / "model.obj";
            Context.StagingDirectory = Workspace / "stage";
            ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
        }
        void TearDown() override
        {
            std::error_code error;
            fs::remove_all(Workspace, error);
        }
        void Write(const fs::path& path, const std::string& bytes)
        {
            std::ofstream out(path, std::ios::binary);
            out << bytes;
            out.close();
            ASSERT_TRUE(out.good());
        }
        void ModelSource(bool reverse = false)
        {
            const auto header = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n";
            const auto a      = "o Body\nf 1 2 3\n";
            const auto b      = "o Weapon\nf 1 4 2\n";
            Write(Context.SourceFile, std::string(header) + (reverse ? std::string(b) + a : std::string(a) + b));
        }
        std::vector<std::byte> Read(const ProductDescriptor& product)
        {
            std::ifstream in(Context.StagingDirectory / product.StagedFile, std::ios::binary);
            EXPECT_TRUE(in.good());
            std::string            bytes {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            std::vector<std::byte> result(bytes.size());
            std::transform(
                bytes.begin(), bytes.end(), result.begin(), [](char c) { return static_cast<std::byte>(c); });
            return result;
        }
        // Resolves discovered inputs the way the build coordinator does.
        ImportInputs ResolveExternalInputs()
        {
            auto discovered = ModelImporter().DiscoverInputs(Context);
            EXPECT_TRUE(std::holds_alternative<ImportInputs>(discovered));
            if (!std::holds_alternative<ImportInputs>(discovered))
                return {};
            const auto inputs = std::get<ImportInputs>(discovered);
            for (const auto& file : inputs.SourceFiles)
                Context.SourceInputs.emplace(file, SourceAssetID::Generate());
            for (const auto& file : inputs.ProductSources)
                Context.ProductsBySource.emplace(file, AssetID(SourceAssetID::Generate(), 1));
            return inputs;
        }
        void InstancedGltf(bool duplicateMeshNames)
        {
            Context.SourceFile = Workspace / "model.gltf";
            std::string buffer(42, '\0');
            // Three little-endian positions followed by three uint16 indices.
            buffer[14] = char(0x80);
            buffer[15] = char(0x3f);
            buffer[30] = char(0x80);
            buffer[31] = char(0x3f);
            buffer[38] = 1;
            buffer[40] = 2;
            Write(Workspace / "geometry.bin", buffer);
            const std::string mesh = R"({"name":"Body","primitives":[{"attributes":{"POSITION":0},"indices":1}]})";
            Write(
                Context.SourceFile,
                R"({"asset":{"version":"2.0"},"buffers":[{"uri":"geometry.bin","byteLength":42}],)"
                R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
                R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
                R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[)" +
                    mesh + (duplicateMeshNames ? "," + mesh : "") +
                    R"(],"nodes":[{"name":"First","mesh":0,"translation":[1,0,0]},)"
                    R"({"name":"Second","mesh":)" +
                    (duplicateMeshNames ? "1" : "0") +
                    R"(,"translation":[2,0,0]}],"scenes":[{"nodes":[0,1]}],"scene":0})");
        }
    };

    TEST_F(ModelImporterTest, CompleteStagedSetLoadsAfterSourceDeletion)
    {
        ModelSource();
        auto imported = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(imported));
        const auto& result = std::get<ImportResult>(imported);
        EXPECT_TRUE(fs::remove(Context.SourceFile));
        size_t meshes = 0, materials = 0, models = 0;
        for (const auto& product : result.Products)
        {
            const auto       bytes = Read(product);
            AssetLoadContext input {
                AssetID(Context.SourceID, product.LocalID), product.Type, bytes.data(), bytes.size()};
            if (product.Type == AssetType::StaticMesh)
            {
                auto loaded = Loader<MeshAsset>().Load(input);
                ASSERT_TRUE(std::holds_alternative<Ref<MeshAsset>>(loaded));
                EXPECT_EQ(std::get<Ref<MeshAsset>>(loaded)->GetData().Material, product.Dependencies.at(0));
                ++meshes;
            }
            else if (product.Type == AssetType::Material)
            {
                EXPECT_TRUE(std::holds_alternative<Ref<MaterialDataAsset>>(Loader<MaterialDataAsset>().Load(input)));
                ++materials;
            }
            else if (product.Type == AssetType::Model)
            {
                EXPECT_TRUE(std::holds_alternative<Ref<ModelAsset>>(Loader<ModelAsset>().Load(input)));
                EXPECT_EQ(product.Dependencies.size(), 2u);
                ++models;
            }
        }
        EXPECT_EQ(meshes, 2u);
        EXPECT_GT(materials, 0u);
        EXPECT_EQ(models, 1u);
        EXPECT_EQ(fs::directory_iterator(Context.StagingDirectory) == fs::directory_iterator(), false);
    }

    TEST_F(ModelImporterTest, ReorderingSourceObjectsKeepsLocalIDsAndPreviousMapUnchanged)
    {
        ModelSource();
        auto first = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(first));
        Context.PreviousLocalIDs = std::get<ImportResult>(first).LocalIDs;
        const auto previous      = Context.PreviousLocalIDs;
        Context.StagingDirectory = Workspace / "stage2";
        ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
        ModelSource(true);
        auto second = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(second));
        EXPECT_EQ(std::get<ImportResult>(second).LocalIDs, previous);
        EXPECT_EQ(Context.PreviousLocalIDs, previous);
    }

    TEST_F(ModelImporterTest, TracksMtlBuildInputAndTextureProductDependency)
    {
        Write(Workspace / "model.mtl", "newmtl Surface\nKd 0.2 0.4 0.6\nmap_Kd texture.png\n");
        Write(Context.SourceFile, "mtllib model.mtl\no Body\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n");
        EXPECT_EQ(std::get<AssetError>(ModelImporter().Import(Context)).Code, AssetErrorCode::NotFound);
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
        // A missing texture is a warning; the material keeps its other data.
        auto missing = ModelImporter().DiscoverInputs(Context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(missing));
        EXPECT_TRUE(std::get<ImportInputs>(missing).ProductSources.empty());
        ASSERT_EQ(std::get<ImportInputs>(missing).Warnings.size(), 1u);
        Context.SourceInputs.emplace(Workspace / "model.mtl", SourceAssetID::Generate());
        auto untextured = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(untextured));
        for (const auto& product : std::get<ImportResult>(untextured).Products)
            if (product.Type == AssetType::Material)
            {
                const auto bytes = Read(product);
                EXPECT_TRUE(std::get<MaterialData>(DecodeMaterialProduct(bytes.data(), bytes.size())).Textures.empty());
            }
        Context.SourceInputs.clear();
        fs::remove_all(Context.StagingDirectory);
        fs::create_directory(Context.StagingDirectory);
        Write(Workspace / "texture.png", "not decoded by the model importer");
        const auto inputs = ResolveExternalInputs();
        EXPECT_EQ(inputs.SourceFiles, std::vector<fs::path> {Workspace / "model.mtl"});
        EXPECT_EQ(inputs.ProductSources, std::vector<fs::path> {Workspace / "texture.png"});
        auto imported = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(imported));
        const auto& result  = std::get<ImportResult>(imported);
        const auto  texture = Context.ProductsBySource.at(Workspace / "texture.png");
        bool       found   = false;
        for (const auto& product : result.Products)
            if (product.Type == AssetType::Material && !product.Dependencies.empty())
            {
                const auto bytes   = Read(product);
                auto       decoded = DecodeMaterialProduct(bytes.data(), bytes.size());
                ASSERT_TRUE(std::holds_alternative<MaterialData>(decoded));
                EXPECT_EQ(std::get<MaterialData>(decoded).Textures.at(0).Texture, texture);
                EXPECT_EQ(product.Dependencies.at(0), texture);
                found = true;
            }
        EXPECT_TRUE(found);
    }

    TEST_F(ModelImporterTest, EmissiveFactorAndDiffuseTextureReachStandardRecipe)
    {
        // Assimp's Ke must survive import, and legacy OBJ map_Kd must bind Standard BaseColorMap.
        Write(Workspace / "model.mtl", "newmtl Surface\nKd 0.2 0.4 0.6\nKe 0.25 0.5 0.75\nmap_Kd texture.png\n");
        Write(Context.SourceFile, "mtllib model.mtl\no Body\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n");
        Write(Workspace / "texture.png", "resolved by build coordinator");
        ResolveExternalInputs();
        auto imported = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(imported));
        bool found = false;
        for (const auto& product : std::get<ImportResult>(imported).Products)
        {
            if (product.Type != AssetType::Material)
                continue;
            const auto bytes   = Read(product);
            auto       decoded = DecodeMaterialProduct(bytes.data(), bytes.size());
            ASSERT_TRUE(std::holds_alternative<MaterialData>(decoded));
            const auto& data = std::get<MaterialData>(decoded);
            if (data.Name != "Surface")
                continue;
            found = true;
            EXPECT_EQ(data.Shader, StandardShaderID());
            ASSERT_TRUE(data.Bindings);
            EXPECT_EQ(data.Bindings->Textures.at(0).Binding, 1u);
            EXPECT_EQ(data.Textures.at(0).Texture, Context.ProductsBySource.at(Workspace / "texture.png"));
            auto packed = PackMaterialUniforms(data, GetStandardShader().Code, *data.Bindings);
            ASSERT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(packed));
            const auto& block = std::get<std::vector<MaterialUniformBlock>>(packed).at(0).Bytes;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                float value = 0;
                std::memcpy(&value, block.data() + 32 + channel * 4, 4);
                EXPECT_FLOAT_EQ(value, (channel + 1) * 0.25f);
            }
        }
        EXPECT_TRUE(found);
    }

    TEST_F(ModelImporterTest, BlackImportedEmissiveFactorPreservesEmissiveTexture)
    {
        // OBJ/FBX black Ke with an emissive map must not suppress the texture's previously visible emission.
        Write(Workspace / "model.mtl",
              "newmtl Surface\nKe 0 0 0\nmap_Ke emission.png\n"
              "newmtl Tinted\nKe 0.25 0.5 0.75\nmap_Ke emission.png\n");
        Write(Context.SourceFile,
              "mtllib model.mtl\no Body\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n"
              "o TintedBody\nusemtl Tinted\nf 1 3 2\n");
        Write(Workspace / "emission.png", "resolved by build coordinator");
        ResolveExternalInputs();
        auto imported = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(imported));
        std::set<std::string> found;
        for (const auto& product : std::get<ImportResult>(imported).Products)
        {
            if (product.Type != AssetType::Material)
                continue;
            const auto bytes   = Read(product);
            auto       decoded = DecodeMaterialProduct(bytes.data(), bytes.size());
            ASSERT_TRUE(std::holds_alternative<MaterialData>(decoded));
            const auto& data = std::get<MaterialData>(decoded);
            if (data.Name != "Surface" && data.Name != "Tinted")
                continue;
            found.insert(data.Name);
            ASSERT_TRUE(data.Bindings);
            ASSERT_EQ(data.Bindings->Textures.size(), 1u);
            EXPECT_EQ(data.Bindings->Textures[0].Binding, 5u);
            EXPECT_EQ(data.Textures.at(data.Bindings->Textures[0].TextureIndex).Texture,
                      Context.ProductsBySource.at(Workspace / "emission.png"));
            auto packed = PackMaterialUniforms(data, GetStandardShader().Code, *data.Bindings);
            ASSERT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(packed));
            const auto& block = std::get<std::vector<MaterialUniformBlock>>(packed).at(0).Bytes;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                float value = 0;
                std::memcpy(&value, block.data() + 32 + channel * 4, 4);
                // Nonblack factors must still modulate a texture instead of being replaced with white.
                EXPECT_FLOAT_EQ(value, data.Name == "Surface" ? 1 : (channel + 1) * 0.25f);
            }
        }
        EXPECT_EQ(found, (std::set<std::string> {"Surface", "Tinted"}));
    }

    TEST_F(ModelImporterTest, NonemptyStagingAndUnknownSettingsFailWithoutOverwritingFiles)
    {
        ModelSource();
        Write(Context.StagingDirectory / "sentinel", "keep");
        EXPECT_EQ(std::get<AssetError>(ModelImporter().Import(Context)).Code, AssetErrorCode::InvalidArgument);
        EXPECT_TRUE(fs::exists(Context.StagingDirectory / "sentinel"));
        EXPECT_EQ(std::distance(fs::directory_iterator(Context.StagingDirectory), fs::directory_iterator()), 1);
        EXPECT_TRUE(fs::remove(Context.StagingDirectory / "sentinel"));
        Context.Settings["unknown"] = "true";
        EXPECT_EQ(std::get<AssetError>(ModelImporter().Import(Context)).Code, AssetErrorCode::InvalidArgument);
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
    }

    TEST_F(ModelImporterTest, SupportsUnicodeSourceAndMaterialPaths)
    {
        Context.SourceFile = Workspace / fs::u8path(u8"模型.obj");
        Write(Workspace / fs::u8path(u8"材质.mtl"), u8"newmtl Surface\nKd 0.3 0.4 0.5\n");
        Write(Context.SourceFile, u8"mtllib 材质.mtl\no Body\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n");
        ResolveExternalInputs();
        auto imported = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(imported));
        EXPECT_TRUE(Context.SourceInputs.count(Workspace / fs::u8path(u8"材质.mtl")));
    }

    TEST_F(ModelImporterTest, MissingMtlFailsInsteadOfPublishingDefaultMaterial)
    {
        Write(Context.SourceFile, "mtllib missing.mtl\no Body\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n");
        auto imported = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<AssetError>(imported));
        EXPECT_EQ(std::get<AssetError>(imported).Code, AssetErrorCode::NotFound);
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
    }

    TEST_F(ModelImporterTest, PreservesInstancedGeometryAndNodeTransforms)
    {
        InstancedGltf(false);
        ResolveExternalInputs();
        auto imported = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(imported));
        const auto& result = std::get<ImportResult>(imported);
        EXPECT_EQ(std::count_if(result.Products.begin(),
                                result.Products.end(),
                                [](const auto& product) { return product.Type == AssetType::StaticMesh; }),
                  1);
        bool found = false;
        for (const auto& product : result.Products)
            if (product.Type == AssetType::Model)
            {
                auto bytes   = Read(product);
                auto decoded = DecodeModelProduct(bytes.data(), bytes.size());
                ASSERT_TRUE(std::holds_alternative<ModelData>(decoded));
                std::vector<ModelNodeData> instances;
                for (const auto& node : std::get<ModelData>(decoded).Nodes)
                    if (!node.Meshes.empty())
                        instances.push_back(node);
                ASSERT_EQ(instances.size(), 2u);
                EXPECT_EQ(instances[0].Meshes, instances[1].Meshes);
                EXPECT_EQ(instances[0].LocalTransform[3], 1.0f);
                EXPECT_EQ(instances[1].LocalTransform[3], 2.0f);
                found = true;
            }
        EXPECT_TRUE(found);
    }

    TEST_F(ModelImporterTest, DuplicateMeshNamesGetDistinctIdentitiesStableAcrossReimport)
    {
        InstancedGltf(true);
        ResolveExternalInputs();
        auto first = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(first)) << std::get<AssetError>(first).Message;
        const auto&        result = std::get<ImportResult>(first);
        std::set<uint64_t> meshes;
        for (const auto& product : result.Products)
            if (product.Type == AssetType::StaticMesh)
                meshes.insert(product.LocalID);
        EXPECT_EQ(meshes.size(), 2u);
        // The two "Body" meshes are told apart by the node that references them.
        std::set<std::string> meshKeys;
        for (const auto& [key, id] : result.LocalIDs)
            if (meshes.count(id))
                meshKeys.insert(key);
        ASSERT_EQ(meshKeys.size(), 2u);
        EXPECT_NE(meshKeys.begin()->find("First"), std::string::npos);
        EXPECT_NE(std::next(meshKeys.begin())->find("Second"), std::string::npos);

        Context.PreviousLocalIDs = result.LocalIDs;
        Context.StagingDirectory = Workspace / "stage2";
        ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
        auto second = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(second));
        EXPECT_EQ(std::get<ImportResult>(second).LocalIDs, result.LocalIDs);
    }

    TEST_F(ModelImporterTest, RemovedObjectsKeepReservedMappingAndDoNotEmitOldProducts)
    {
        ModelSource();
        auto first = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(first));
        Context.PreviousLocalIDs = std::get<ImportResult>(first).LocalIDs;
        const auto previous      = Context.PreviousLocalIDs;
        Context.StagingDirectory = Workspace / "stage2";
        ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
        Write(Context.SourceFile, "v 0 0 0\nv 1 0 0\nv 0 1 0\no Body\nf 1 2 3\n");
        auto second = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(second));
        const auto& result = std::get<ImportResult>(second);
        EXPECT_EQ(result.LocalIDs, previous);
        EXPECT_EQ(std::count_if(result.Products.begin(),
                                result.Products.end(),
                                [](const auto& product) { return product.Type == AssetType::StaticMesh; }),
                  1);
    }

    TEST_F(ModelImporterTest, ConcurrentCpuParsesKeepOwnedResultsAndFailuresSeparate)
    {
        ModelSource();
        Write(Workspace / "bad.obj", "not a model\n");
        auto valid         = std::async(std::launch::async, [&]() { return ParseModelCpu(Context.SourceFile); });
        auto invalid       = std::async(std::launch::async, [&]() { return ParseModelCpu(Workspace / "bad.obj"); });
        auto validResult   = valid.get();
        auto invalidResult = invalid.get();
        ASSERT_TRUE(std::holds_alternative<ImportedModelData>(validResult));
        ASSERT_TRUE(std::holds_alternative<AssetError>(invalidResult));
        EXPECT_EQ(std::get<ImportedModelData>(validResult).Meshes.size(), 2u);
        EXPECT_EQ(std::get<AssetError>(invalidResult).Code, AssetErrorCode::InvalidData);
    }

    TEST_F(ModelImporterTest, ImportsRealObjAndGltfAsCompleteProductSets)
    {
        for (const auto& file : {"nanosuit/nanosuit.obj", "modern_coffee_table/modern_coffee_table_01_4k.gltf"})
        {
            Context.SourceFile = fs::path(KANS_TEST_ASSET_DIR) / file;
            ResolveExternalInputs();
            auto imported = ModelImporter().Import(Context);
            ASSERT_TRUE(std::holds_alternative<ImportResult>(imported)) << std::get<AssetError>(imported).Message;
            const auto& result = std::get<ImportResult>(imported);
            EXPECT_GT(result.Products.size(), 2u);
            for (const auto& product : result.Products)
                EXPECT_TRUE(fs::is_regular_file(Context.StagingDirectory / product.StagedFile));
            std::error_code error;
            fs::remove_all(Context.StagingDirectory, error);
            ASSERT_TRUE(fs::create_directory(Context.StagingDirectory));
        }
    }
    TEST_F(ModelImporterTest, ClosedInputSetRejectsUndeclaredMaterialBeforeStaging)
    {
        Write(Workspace / "model.mtl", "newmtl Material\nKd 1 0 0\n");
        Write(Context.SourceFile, "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Material\nf 1 2 3\n");
        auto result = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_NE(std::get<AssetError>(result).Message.find("declared input set"), std::string::npos);
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
        Context.SourceInputs.emplace(Workspace / "model.mtl", SourceAssetID::Generate());
        EXPECT_TRUE(std::holds_alternative<ImportResult>(ModelImporter().Import(Context)));
    }

    TEST_F(ModelImporterTest, TextureReferencesResolveBackslashesForeignAbsolutePathsAndCase)
    {
        fs::create_directories(Workspace / "Textures");
        Write(Workspace / "Textures" / "Albedo.PNG", "albedo");
        Write(Workspace / "normal.png", "normal");
        Write(Workspace / "model.mtl",
              "newmtl Surface\nmap_Kd Textures\\Albedo.PNG\nmap_Bump C:\\Artist\\Desktop\\normal.png\n"
              "newmtl Other\nmap_Kd textures/albedo.png\n");
        Write(
            Context.SourceFile,
            "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\no A\nusemtl Surface\nf 1 2 3\no B\nusemtl Other\nf 1 3 2\n");
        auto discovered = ModelImporter().DiscoverInputs(Context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(discovered)) << std::get<AssetError>(discovered).Message;
        const auto& inputs = std::get<ImportInputs>(discovered);
        // The same file referenced with different case is one input.
        EXPECT_EQ(inputs.ProductSources,
                  (std::vector<fs::path> {Workspace / "Textures" / "Albedo.PNG", Workspace / "normal.png"}));
        // The bump map is non-color data; the opaque materials' color texture uses alpha as data.
        EXPECT_EQ(inputs.ProductSourceSettings.size(), 2u);
        EXPECT_EQ(inputs.ProductSourceSettings.at(Workspace / "normal.png").at("usage"), "normal");
        EXPECT_EQ(inputs.ProductSourceSettings.at(Workspace / "Textures" / "Albedo.PNG").at("alpha_mode"), "opaque");

        // A color texture also used by a transparent material gets no suggestion.
        Write(Workspace / "model.mtl",
              "newmtl Surface\nmap_Kd Textures/Albedo.PNG\nnewmtl Other\nd 0.5\nmap_Kd Textures/Albedo.PNG\n");
        auto mixed = ModelImporter().DiscoverInputs(Context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(mixed));
        EXPECT_TRUE(std::get<ImportInputs>(mixed).ProductSourceSettings.empty());

        Write(Workspace / "model.mtl", "newmtl Surface\nmap_Kd missing.png\n");
        Write(Context.SourceFile, "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n");
        auto missing = ModelImporter().DiscoverInputs(Context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(missing));
        EXPECT_NE(std::get<ImportInputs>(missing).Warnings.at(0).find("missing.png"), std::string::npos);
    }

    // Every model shipped with the editor imports its static geometry; content the
    // importer does not support yet (animation, skinning, embedded textures) is
    // reported as a warning instead of failing the model.
    TEST(ModelImporterAssets, BundledModelsImportWithWarningsForUnsupportedContent)
    {
        const fs::path root(KANS_TEST_ASSET_DIR);
        size_t         models = 0;
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;
            auto extension = entry.path().extension().u8string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            if (extension != ".obj" && extension != ".fbx" && extension != ".gltf" && extension != ".glb")
                continue;
            ++models;
            ImportContext context;
            context.SourceID   = SourceAssetID::Generate();
            context.SourceFile = entry.path();
            auto discovered    = ModelImporter().DiscoverInputs(context);
            ASSERT_TRUE(std::holds_alternative<ImportInputs>(discovered))
                << entry.path().u8string() << ": " << std::get<AssetError>(discovered).Message;
            for (const auto& file : std::get<ImportInputs>(discovered).SourceFiles)
                context.SourceInputs.emplace(file, SourceAssetID::Generate());
            for (const auto& file : std::get<ImportInputs>(discovered).ProductSources)
                context.ProductsBySource.emplace(file, AssetID(SourceAssetID::Generate(), 1));
            context.StagingDirectory =
                fs::temp_directory_path() / ("kans-bundled-" + SourceAssetID::Generate().ToString());
            fs::create_directory(context.StagingDirectory);
            auto            imported = ModelImporter().Import(context);
            std::error_code ignored;
            fs::remove_all(context.StagingDirectory, ignored);
            ASSERT_TRUE(std::holds_alternative<ImportResult>(imported))
                << entry.path().u8string() << ": " << std::get<AssetError>(imported).Message;
            EXPECT_TRUE(std::any_of(std::get<ImportResult>(imported).Products.begin(),
                                    std::get<ImportResult>(imported).Products.end(),
                                    [](const auto& product) { return product.Type == AssetType::Model; }));
        }
        EXPECT_GE(models, 10u);
    }

    // Model height in the parsed hierarchy, following parent transforms (parents come first).
    float ModelHeight(const ImportedModelData& model)
    {
        std::vector<glm::mat4> world(model.Nodes.size());
        float                  low = 1e30f, high = -1e30f;
        for (size_t i = 0; i < model.Nodes.size(); ++i)
        {
            const auto& node  = model.Nodes[i];
            const auto  local = glm::transpose(glm::make_mat4(node.LocalTransform.data()));
            world[i]          = node.Parent < 0 ? local : world[node.Parent] * local;
            for (uint32_t mesh : node.Meshes)
                for (const auto& vertex : model.Meshes[mesh].Vertices)
                {
                    const auto p = world[i] * glm::vec4(vertex.Position[0], vertex.Position[1], vertex.Position[2], 1);
                    low          = std::min(low, p.y);
                    high         = std::max(high, p.y);
                }
        }
        return high - low;
    }

    // FBX units are converted to engine meters (UnitScaleFactor is centimeters per unit).
    // This model is authored in centimeters at 19.9 m (an oversized MMD conversion).
    TEST(ModelImporterAssets, FbxUnitsAreConvertedToMeters)
    {
        auto parsed = ParseModelCpu(fs::path(KANS_TEST_ASSET_DIR) / "GY_Light" / "GY_Light.fbx");
        ASSERT_TRUE(std::holds_alternative<ImportedModelData>(parsed));
        EXPECT_NEAR(ModelHeight(std::get<ImportedModelData>(parsed)), 19.905f, 0.01f);
    }

    TEST_F(ModelImporterTest, ScaleSettingResizesTheWholeModel)
    {
        ModelSource();
        const auto rootScale = [&](const ImportResult& result) {
            for (const auto& product : result.Products)
                if (product.Type == AssetType::Model)
                {
                    const auto bytes = Read(product);
                    return std::get<ModelData>(DecodeModelProduct(bytes.data(), bytes.size())).Nodes[0].LocalTransform;
                }
            return std::array<float, 16> {};
        };
        auto normal = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(normal));
        const auto before        = rootScale(std::get<ImportResult>(normal)); // read before changing staging
        Context.StagingDirectory = Workspace / "scaled";
        fs::create_directory(Context.StagingDirectory);
        Context.Settings = {{"scale", "0.25"}};
        auto scaled      = ModelImporter().Import(Context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(scaled)) << std::get<AssetError>(scaled).Message;
        const auto after = rootScale(std::get<ImportResult>(scaled));
        for (size_t i = 0; i < 12; ++i)
            EXPECT_FLOAT_EQ(after[i], before[i] * 0.25f) << i;
        EXPECT_EQ(after[15], before[15]);
        // Identity does not depend on scale.
        EXPECT_EQ(std::get<ImportResult>(scaled).LocalIDs, std::get<ImportResult>(normal).LocalIDs);

        for (const char* bad : {"0", "-1", "abc", "1x", "", "inf"})
        {
            Context.Settings = {{"scale", bad}};
            EXPECT_TRUE(std::holds_alternative<AssetError>(ModelImporter().Import(Context))) << bad;
        }
    }
} // namespace
