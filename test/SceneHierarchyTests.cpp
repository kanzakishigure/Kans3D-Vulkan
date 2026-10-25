#include <fstream>
#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/SceneSerializer.h"

namespace
{
    using namespace Kans;

    std::vector<std::string> Names(const std::vector<Entity>& entities)
    {
        std::vector<std::string> names;
        for (auto entity : entities)
            names.push_back(entity.GetComponent<TagComponent>().Tag);
        return names;
    }

    bool Near(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 1e-4f; }

    class SceneHierarchyTest : public ::testing::Test
    {
    protected:
        Ref<Scene> Current = CreateRef<Scene>("Hierarchy");
        Entity     A       = Current->CreateEntity("A");
        Entity     B       = Current->CreateEntity("B");
        Entity     C       = Current->CreateEntity("C");
    };

    TEST_F(SceneHierarchyTest, RootsKeepCreationOrderAndParentingMovesEntities)
    {
        EXPECT_EQ(Names(Current->GetRootEntities()), (std::vector<std::string> {"A", "B", "C"}));
        ASSERT_TRUE(Current->SetParent(C, A));
        ASSERT_TRUE(Current->SetParent(B, A, C, true));
        EXPECT_EQ(Names(Current->GetRootEntities()), (std::vector<std::string> {"A"}));
        EXPECT_EQ(Names(Current->GetChildren(A)), (std::vector<std::string> {"B", "C"}));
        EXPECT_EQ(Current->GetParent(B), A);
        EXPECT_TRUE(Current->IsDescendantOf(C, A));
        // Cycles are rejected and leave the hierarchy unchanged.
        EXPECT_FALSE(Current->SetParent(A, C));
        EXPECT_FALSE(Current->SetParent(A, A));
        EXPECT_EQ(Current->GetParent(C), A);
        // Back to the top level, before A.
        ASSERT_TRUE(Current->SetParent(C, {}, A, true));
        EXPECT_EQ(Names(Current->GetRootEntities()), (std::vector<std::string> {"C", "A"}));
        EXPECT_FALSE(Current->GetParent(C));
    }

    TEST_F(SceneHierarchyTest, WorldTransformsComposeAndReparentingKeepsWorldPlacement)
    {
        A.GetComponent<TransformComponent>().Position = {10, 0, 0};
        A.GetComponent<TransformComponent>().Scale    = {2, 2, 2};
        B.GetComponent<TransformComponent>().Position = {1, 2, 3};
        ASSERT_TRUE(Current->SetParent(B, A, {}, false));
        // Without keeping the world placement, the local transform is now relative to A.
        EXPECT_TRUE(Near(glm::vec3(Current->GetWorldTransform(B)[3]), {12, 4, 6}));
        // Keeping it changes the local transform instead.
        ASSERT_TRUE(Current->SetParent(B, {}));
        EXPECT_TRUE(Near(B.GetComponent<TransformComponent>().Position, {12, 4, 6}));
        ASSERT_TRUE(Current->SetParent(B, A));
        EXPECT_TRUE(Near(B.GetComponent<TransformComponent>().Position, {1, 2, 3}));
        EXPECT_TRUE(Near(B.GetComponent<TransformComponent>().Scale, {1, 1, 1}));
        Current->SetWorldTransform(B, glm::translate(glm::mat4(1.0f), {10, 0, 0}));
        EXPECT_TRUE(Near(B.GetComponent<TransformComponent>().Position, {0, 0, 0}));
    }

    TEST_F(SceneHierarchyTest, DeletingRemovesChildrenAndDuplicatingCopiesThem)
    {
        ASSERT_TRUE(Current->SetParent(B, A));
        ASSERT_TRUE(Current->SetParent(C, B));
        B.AddComponent<PointLightComponent>().Diffuse_Intensity = {0.5f, 0.5f, 0.5f};
        const UUID cID = C.GetUUID();

        Entity copy = Current->DuplicateEntity(B);
        EXPECT_NE(copy.GetUUID(), B.GetUUID());
        EXPECT_EQ(Current->GetParent(copy), A);
        EXPECT_EQ(Names(Current->GetChildren(A)), (std::vector<std::string> {"B", "B"}));
        ASSERT_TRUE(copy.HasComponent<PointLightComponent>());
        EXPECT_EQ(copy.GetComponent<PointLightComponent>().Diffuse_Intensity.x, 0.5f);
        const auto copiedChildren = Current->GetChildren(copy);
        ASSERT_EQ(copiedChildren.size(), 1u);
        EXPECT_NE(copiedChildren[0].GetUUID(), cID);

        Current->DestroyEntity(B);
        EXPECT_FALSE(Current->GetEntityByUUID(cID));
        EXPECT_EQ(Current->GetChildren(A).size(), 1u);
        EXPECT_EQ(Current->GetChildren(A)[0], copy);
    }

    TEST_F(SceneHierarchyTest, SerializationKeepsHierarchyAndOrderAndOldScenesLoadFlat)
    {
        ASSERT_TRUE(Current->SetParent(A, C));
        ASSERT_TRUE(Current->SetParent(B, C, A, false));
        B.GetComponent<TransformComponent>().Position = {1, 2, 3};
        const auto directory = std::filesystem::temp_directory_path() / ("kans-hierarchy-" + SourceAssetID::Generate().ToString());
        std::filesystem::create_directories(directory);
        const auto file = directory / "scene.kans";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SceneSerializer(Current).SerializeChecked(file)));
        auto loaded = SceneSerializer::DeserializeChecked(file);
        ASSERT_TRUE(std::holds_alternative<Ref<Scene>>(loaded)) << std::get<AssetError>(loaded).Message;
        auto scene = std::get<Ref<Scene>>(loaded);
        EXPECT_EQ(Names(scene->GetRootEntities()), (std::vector<std::string> {"C"}));
        auto parent = scene->GetRootEntities()[0];
        EXPECT_EQ(Names(scene->GetChildren(parent)), (std::vector<std::string> {"B", "A"}));
        EXPECT_TRUE(Near(scene->GetChildren(parent)[0].GetComponent<TransformComponent>().Position, {1, 2, 3}));

        // Scenes written before hierarchies had no Parent fields.
        std::ofstream(directory / "old.kans") << "Version: 1\nScene: Old\nEntities:\n"
                                                 "  - Entity: 7\n    TagComponent: {Tag: X}\n"
                                                 "    TransformComponent: {Position: [0, 0, 0], Rotation: [0, 0, 0], Scale: [1, 1, 1]}\n";
        auto old = SceneSerializer::DeserializeChecked(directory / "old.kans");
        ASSERT_TRUE(std::holds_alternative<Ref<Scene>>(old));
        EXPECT_EQ(Names(std::get<Ref<Scene>>(old)->GetRootEntities()), (std::vector<std::string> {"X"}));

        // A parent that does not exist, or a cycle, is rejected.
        std::ofstream(directory / "bad.kans") << "Version: 1\nScene: Bad\nEntities:\n"
                                                 "  - Entity: 7\n    Parent: 8\n    TagComponent: {Tag: X}\n"
                                                 "    TransformComponent: {Position: [0, 0, 0], Rotation: [0, 0, 0], Scale: [1, 1, 1]}\n"
                                                 "  - Entity: 8\n    Parent: 7\n    TagComponent: {Tag: Y}\n"
                                                 "    TransformComponent: {Position: [0, 0, 0], Rotation: [0, 0, 0], Scale: [1, 1, 1]}\n";
        EXPECT_TRUE(std::holds_alternative<AssetError>(SceneSerializer::DeserializeChecked(directory / "bad.kans")));
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
} // namespace
