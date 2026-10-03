#include <cmath>
#include <gtest/gtest.h>
#include "Kans3D/Utilities/MeshUtils.h"

using SmoothFunction = void (*)(std::vector<Kans::Vertex>&, std::vector<glm::vec3>&);
class MeshUtilsNormals : public testing::TestWithParam<SmoothFunction>
{};

TEST_P(MeshUtilsNormals, SharedPositionReceivesNormalizedSum)
{
    std::vector<Kans::Vertex> vertices(4);
    vertices[0].Position = vertices[1].Position = vertices[2].Position = {0, 0, 0};
    vertices[3].Position                                               = {1, 0, 0};
    vertices[0].Normal                                                 = {1, 0, 0};
    vertices[1].Normal                                                 = {0, 1, 0};
    vertices[2].Normal                                                 = {0, 0, 1};
    vertices[3].Normal                                                 = {0, 0, 1};
    auto                   original                                    = vertices;
    std::vector<glm::vec3> normals;
    GetParam()(vertices, normals);
    ASSERT_EQ(normals.size(), vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        const auto expected = i < 3 ? glm::normalize(glm::vec3(1)) : glm::vec3(0, 0, 1);
        EXPECT_NEAR(normals[i].x, expected.x, 1e-5f) << i;
        EXPECT_NEAR(normals[i].y, expected.y, 1e-5f) << i;
        EXPECT_NEAR(normals[i].z, expected.z, 1e-5f) << i;
        EXPECT_EQ(vertices[i].Normal, original[i].Normal);
        EXPECT_EQ(vertices[i].Position, original[i].Position);
    }
}

TEST_P(MeshUtilsNormals, DistinctPositionsStaySeparate)
{
    std::vector<Kans::Vertex> vertices(3);
    for (int i = 0; i < 3; ++i)
    {
        vertices[i].Position = {i, 0, 0};
        vertices[i].Normal   = {0, 1, 0};
    }
    std::vector<glm::vec3> normals;
    GetParam()(vertices, normals);
    ASSERT_EQ(normals.size(), 3u);
    for (auto n : normals)
        EXPECT_EQ(n, glm::vec3(0, 1, 0));
}

INSTANTIATE_TEST_SUITE_P(ProjectUtils,
                         MeshUtilsNormals,
                         testing::Values(&Kans::Utils::MeshUtils::SmoothNormal,
                                         &Kans::Utils::MeshUtils::SmoothNormalHash),
                         [](const testing::TestParamInfo<SmoothFunction>& info) {
                             return info.index == 0 ? "SmoothNormal" : "SmoothNormalHash";
                         });
