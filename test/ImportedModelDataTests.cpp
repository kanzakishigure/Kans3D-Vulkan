#include <fstream>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ImportedModelData.h"
#include "Kans3D/Asset/SourceAssetMetaData.h"

namespace
{
    class ImportedModelInput : public ::testing::Test
    {
    protected:
        std::filesystem::path Directory;
        void                  SetUp() override
        {
            Directory =
                std::filesystem::temp_directory_path() / ("Kans3D-Model-" + Kans::SourceAssetID::Generate().ToString());
            ASSERT_TRUE(std::filesystem::create_directory(Directory));
        }
        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(Directory, error);
        }
        void Write(const char* name, const char* bytes)
        {
            std::ofstream stream(Directory / name, std::ios::binary);
            stream << bytes;
            stream.close();
            ASSERT_TRUE(stream.good());
        }
    };
} // namespace

TEST_F(ImportedModelInput, RejectsMalformedSource)
{
    Write("broken.obj", "this is not a model\n");
    auto parsed = Kans::ParseModelCpu(Directory / "broken.obj");
    ASSERT_TRUE(std::holds_alternative<Kans::AssetError>(parsed));
    EXPECT_EQ(std::get<Kans::AssetError>(parsed).Code, Kans::AssetErrorCode::InvalidData);
}

TEST_F(ImportedModelInput, PreservesMaterialAndExternalTextureReference)
{
    Write("model.mtl", "newmtl Surface\nKd 0.2 0.4 0.6\nmap_Kd texture.png\n");
    Write("model.obj", "mtllib model.mtl\no Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n");
    auto parsed = Kans::ParseModelCpu(Directory / "model.obj");
    ASSERT_TRUE(std::holds_alternative<Kans::ImportedModelData>(parsed));
    const auto& data = std::get<Kans::ImportedModelData>(parsed);
    ASSERT_EQ(data.Meshes.size(), 1u);
    const auto& material = data.Materials.at(data.Meshes.front().MaterialIndex);
    EXPECT_EQ(material.Name, "Surface");
    EXPECT_NEAR(material.BaseColor[0], 0.2f, 1e-5f);
    ASSERT_FALSE(material.Textures.empty());
    EXPECT_EQ(material.Textures.front().Source, "texture.png");
    EXPECT_EQ(data.Meshes.front().Indices.size(), 3u);
}

TEST(ImportedModelData, OwnsRealModelGeometryWithoutRenderer)
{
    auto parsed = Kans::ParseModelCpu(std::filesystem::path(KANS_TEST_ASSET_DIR) / "nanosuit/nanosuit.obj");
    ASSERT_TRUE(std::holds_alternative<Kans::ImportedModelData>(parsed));
    auto data = std::get<Kans::ImportedModelData>(std::move(parsed));
    ASSERT_FALSE(data.Meshes.empty());
    ASSERT_FALSE(data.Nodes.empty());
    ASSERT_FALSE(data.Materials.empty());
    size_t triangles = 0;
    for (const auto& mesh : data.Meshes)
    {
        EXPECT_LT(mesh.MaterialIndex, data.Materials.size());
        EXPECT_FALSE(mesh.Vertices.empty());
        EXPECT_EQ(mesh.Indices.size() % 3, 0u);
        for (auto index : mesh.Indices)
            EXPECT_LT(index, mesh.Vertices.size());
        triangles += mesh.Indices.size() / 3;
    }
    EXPECT_GT(triangles, 0u);
    for (size_t i = 0; i < data.Nodes.size(); ++i)
    {
        EXPECT_LT(data.Nodes[i].Parent, static_cast<int64_t>(i));
        for (auto mesh : data.Nodes[i].Meshes)
            EXPECT_LT(mesh, data.Meshes.size());
    }
}

TEST(ImportedModelData, ReportsMissingSource)
{
    auto parsed = Kans::ParseModelCpu(std::filesystem::path(KANS_TEST_ASSET_DIR) / "does-not-exist.obj");
    ASSERT_TRUE(std::holds_alternative<Kans::AssetError>(parsed));
    EXPECT_EQ(std::get<Kans::AssetError>(parsed).Code, Kans::AssetErrorCode::NotFound);
}

TEST(ImportedModelData, ParsesGltfHierarchyWithoutRenderer)
{
    auto parsed = Kans::ParseModelCpu(std::filesystem::path(KANS_TEST_ASSET_DIR) /
                                      "modern_coffee_table/modern_coffee_table_01_4k.gltf");
    ASSERT_TRUE(std::holds_alternative<Kans::ImportedModelData>(parsed));
    const auto& data = std::get<Kans::ImportedModelData>(parsed);
    EXPECT_FALSE(data.Meshes.empty());
    EXPECT_EQ(data.Nodes.front().Parent, -1);
}
