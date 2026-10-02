#include <gtest/gtest.h>

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <filesystem>
#include <cmath>
#include <cstring>

TEST(ModelLoading, GeneratesSmoothNormalsAcrossSharedEdge)
{
    // Two triangles meet at 90 degrees. Their shared vertices should have
    // the normalized average of +Y and +Z when the smoothing angle is 175.
    const char* obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 4 2\n";
    Assimp::Importer importer;
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 175.0f);
    const aiScene* scene = importer.ReadFileFromMemory(obj, std::strlen(obj), 0, "obj");
    ASSERT_NE(scene, nullptr) << importer.GetErrorString();
    ASSERT_EQ(scene->mNumMeshes, 1u);
    ASSERT_FALSE(scene->mMeshes[0]->HasNormals());
    scene = importer.ApplyPostProcessing(aiProcess_GenSmoothNormals);
    ASSERT_NE(scene, nullptr) << importer.GetErrorString();
    const auto* mesh = scene->mMeshes[0];
    ASSERT_TRUE(mesh->HasNormals());
    ASSERT_EQ(mesh->mNumFaces, 2u);
    unsigned shared = 0;
    for (unsigned i = 0; i < mesh->mNumVertices; ++i)
    {
        const auto& n = mesh->mNormals[i];
        const auto& p = mesh->mVertices[i];
        EXPECT_TRUE(std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z));
        EXPECT_NEAR(n.Length(), 1.0f, 1e-5f);
        EXPECT_NEAR(n.x, 0.0f, 1e-5f);
        if (p.y == 0 && p.z == 0)
        {
            ++shared;
            EXPECT_NEAR(n.y, std::sqrt(0.5f), 1e-5f);
            EXPECT_NEAR(n.z, std::sqrt(0.5f), 1e-5f);
        }
        else
        {
            EXPECT_NEAR(n.y, p.z == 1 ? 1.0f : 0.0f, 1e-5f);
            EXPECT_NEAR(n.z, p.y == 1 ? 1.0f : 0.0f, 1e-5f);
        }
    }
    EXPECT_GT(shared, 0u);
}

namespace
{
	std::filesystem::path AssetPath(const char* relativePath)
	{
		return std::filesystem::path(KANS_TEST_ASSET_DIR) / relativePath;
	}
}

TEST(ModelLoading, ImportsObjWithoutCreatingGpuResources)
{
	const auto path = AssetPath("nanosuit/nanosuit.obj");
	ASSERT_TRUE(std::filesystem::exists(path)) << path;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(
		path.string(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

	ASSERT_NE(scene, nullptr) << importer.GetErrorString();
	ASSERT_NE(scene->mRootNode, nullptr);
	EXPECT_GT(scene->mNumMeshes, 0u);
	EXPECT_GT(scene->mNumMaterials, 0u);

	uint64_t vertexCount = 0;
	uint64_t indexCount = 0;
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
	{
		ASSERT_NE(scene->mMeshes[i], nullptr);
		vertexCount += scene->mMeshes[i]->mNumVertices;
		indexCount += scene->mMeshes[i]->mNumFaces * 3u;
	}
	EXPECT_GT(vertexCount, 0u);
	EXPECT_GT(indexCount, 0u);
}

TEST(ModelLoading, ImportsGltfWithoutCreatingGpuResources)
{
	const auto path = AssetPath("modern_coffee_table/modern_coffee_table_01_4k.gltf");
	ASSERT_TRUE(std::filesystem::exists(path)) << path;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(
		path.string(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

	ASSERT_NE(scene, nullptr) << importer.GetErrorString();
	ASSERT_NE(scene->mRootNode, nullptr);
	EXPECT_GT(scene->mNumMeshes, 0u);
	EXPECT_GT(scene->mNumMaterials, 0u);
}

TEST(ModelLoading, MissingFileReturnsNull)
{
	const auto path = AssetPath("does-not-exist/model.obj");

	Assimp::Importer importer;
	EXPECT_EQ(importer.ReadFile(path.string(), aiProcess_Triangulate), nullptr);
}
