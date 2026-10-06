#include <cmath>
#include <gtest/gtest.h>
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"

namespace
{
    using namespace Kans;

    std::array<float, 16> Translation(float x, float y, float z)
    {
        // Row-major, as stored in Model Products.
        return {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1};
    }

    glm::vec3 Origin(const glm::mat4& transform) { return glm::vec3(transform * glm::vec4(0, 0, 0, 1)); }

    TEST(RenderAssetCacheCpu, FlattenModelComposesParentTransformsInAnyNodeOrder)
    {
        const AssetID mesh {SourceAssetID::Generate(), 1};
        ModelData     data;
        // Child listed before its parent.
        data.Nodes.push_back({"Child", 1, Translation(0, 2, 0), {mesh}});
        data.Nodes.push_back({"Root", -1, Translation(1, 0, 0), {}});
        data.Nodes.push_back({"Sibling", 1, Translation(0, 0, 3), {mesh, mesh}});
        auto flattened = FlattenModel(data);
        ASSERT_TRUE(std::holds_alternative<ModelInstances>(flattened));
        const auto& instances = std::get<ModelInstances>(flattened).Instances;
        ASSERT_EQ(instances.size(), 3u);
        EXPECT_EQ(instances[0].Mesh, mesh);
        EXPECT_EQ(Origin(instances[0].Transform), glm::vec3(1, 2, 0));
        EXPECT_EQ(Origin(instances[1].Transform), glm::vec3(1, 0, 3));
        EXPECT_EQ(Origin(instances[2].Transform), glm::vec3(1, 0, 3));
    }

    TEST(RenderAssetCacheCpu, FlattenModelRejectsCyclesAndOutOfRangeParents)
    {
        ModelData cycle;
        cycle.Nodes.push_back({"A", 1, Translation(0, 0, 0), {}});
        cycle.Nodes.push_back({"B", 0, Translation(0, 0, 0), {}});
        EXPECT_TRUE(std::holds_alternative<AssetError>(FlattenModel(cycle)));
        ModelData self;
        self.Nodes.push_back({"A", 0, Translation(0, 0, 0), {}});
        EXPECT_TRUE(std::holds_alternative<AssetError>(FlattenModel(self)));
        ModelData range;
        range.Nodes.push_back({"A", 5, Translation(0, 0, 0), {}});
        EXPECT_TRUE(std::holds_alternative<AssetError>(FlattenModel(range)));
        EXPECT_TRUE(std::get<ModelInstances>(FlattenModel(ModelData {})).Instances.empty());
    }

    TEST(RenderAssetCacheCpu, MaterialMapsFactorsAndPrefersPbrTextureSemantics)
    {
        const AssetID diffuse {SourceAssetID::Generate(), 1}, baseColor {SourceAssetID::Generate(), 2},
            bump {SourceAssetID::Generate(), 3}, normal {SourceAssetID::Generate(), 4},
            metallicRoughness {SourceAssetID::Generate(), 5}, occlusion {SourceAssetID::Generate(), 6},
            emissive {SourceAssetID::Generate(), 7}, secondUV {SourceAssetID::Generate(), 8};
        MaterialData data;
        data.BaseColor = {0.5f, 0.25f, 1, 0.75f};
        data.Metallic  = 0.3f;
        data.Roughness = 0.6f;
        // Engine MaterialTextureSemantic values; fallbacks are listed before preferred ones.
        data.Textures       = {{1, 0, diffuse},
                               {12, 0, baseColor},
                               {5, 0, bump},
                               {6, 0, normal},
                               {18, 0, secondUV},
                               {22, 0, metallicRoughness},
                               {17, 0, occlusion},
                               {4, 0, emissive},
                               {16, 1, secondUV}};
        const auto material = MakeGpuMaterial(data);
        EXPECT_EQ(material.BaseColor, glm::vec4(0.5f, 0.25f, 1, 0.75f));
        EXPECT_FLOAT_EQ(material.Metallic, 0.3f);
        EXPECT_FLOAT_EQ(material.Roughness, 0.6f);
        EXPECT_EQ(material.AlbedoTexture, baseColor);
        EXPECT_EQ(material.NormalTexture, normal);
        EXPECT_EQ(material.MetallicRoughnessTexture, metallicRoughness);
        EXPECT_EQ(material.OcclusionTexture, occlusion);
        EXPECT_EQ(material.EmissiveTexture, emissive);

        // OBJ-style materials: diffuse and bump only; non-zero UV channels are ignored.
        data.Textures     = {{1, 0, diffuse}, {5, 0, bump}, {16, 1, secondUV}};
        const auto legacy = MakeGpuMaterial(data);
        EXPECT_EQ(legacy.AlbedoTexture, diffuse);
        EXPECT_EQ(legacy.NormalTexture, bump);
        EXPECT_FALSE(legacy.MetallicRoughnessTexture.IsValid());
        EXPECT_FALSE(legacy.OcclusionTexture.IsValid());
        EXPECT_FALSE(legacy.EmissiveTexture.IsValid());

        // Assimp 5 reports glTF metallic-roughness as Unknown.
        data.Textures = {{18, 0, metallicRoughness}};
        EXPECT_EQ(MakeGpuMaterial(data).MetallicRoughnessTexture, metallicRoughness);
    }
} // namespace
