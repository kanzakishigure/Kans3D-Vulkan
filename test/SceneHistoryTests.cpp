#include <gtest/gtest.h>
#include "../KansEditor/src/SceneHistory.h"
#include "Kans3D/Scene/Entity.h"

namespace
{
    using namespace Kans;

    class SceneHistoryTest : public ::testing::Test
    {
    protected:
        Ref<Scene>   Current = CreateRef<Scene>("History");
        SceneHistory History;
        UUID         CameraID;

        void SetUp() override
        {
            Entity camera = Current->CreateEntity("Camera");
            camera.AddComponent<CameraComponent>();
            CameraID = camera.GetUUID();
            Current->CreateEntity("Light").AddComponent<PointLightComponent>().Diffuse_Intensity = {1, 1, 1};
            History.Reset(*Current);
        }
        Entity Camera() { return Current->GetEntityByUUID(CameraID); }
    };

    TEST_F(SceneHistoryTest, SnapshotsRestoreEntitiesComponentsAndHierarchyWithTheirIDs)
    {
        Entity child = Current->CreateEntity("Child");
        ASSERT_TRUE(Current->SetParent(child, Camera()));
        child.AddComponent<ProductModelComponent>().Model = {SourceAssetID::Generate(), 3};
        const UUID childID  = child.GetUUID();
        const auto snapshot = Current->Capture();

        Current->DestroyEntity(Camera());
        Current->CreateEntity("Other");
        EXPECT_NE(Current->Capture(), snapshot);

        Current->Restore(snapshot);
        EXPECT_EQ(Current->Capture(), snapshot);
        Entity restored = Current->GetEntityByUUID(childID);
        ASSERT_TRUE(restored);
        EXPECT_EQ(Current->GetParent(restored).GetUUID(), CameraID);
        EXPECT_TRUE(restored.HasComponent<ProductModelComponent>());
        EXPECT_TRUE(Camera().HasComponent<CameraComponent>());
        EXPECT_EQ(Current->GetRootEntities().size(), 2u);
    }

    TEST_F(SceneHistoryTest, RecordsOnlyChangesAndUndoesAndRedoesThem)
    {
        EXPECT_FALSE(History.Record(*Current)); // Nothing changed.
        EXPECT_FALSE(History.IsModified());
        Camera().GetComponent<TransformComponent>().Position = {1, 2, 3};
        ASSERT_TRUE(History.Record(*Current));
        Camera().GetComponent<TagComponent>().Tag = "Main";
        ASSERT_TRUE(History.Record(*Current));
        EXPECT_TRUE(History.IsModified());
        EXPECT_EQ(History.UndoLabels(), (std::vector<std::string> {"Rename Camera to Main", "Move Camera"}));

        ASSERT_TRUE(History.Undo(*Current));
        EXPECT_EQ(Camera().GetComponent<TagComponent>().Tag, "Camera");
        EXPECT_EQ(Camera().GetComponent<TransformComponent>().Position, glm::vec3(1, 2, 3));
        ASSERT_TRUE(History.Undo(*Current));
        EXPECT_EQ(Camera().GetComponent<TransformComponent>().Position, glm::vec3(0));
        EXPECT_FALSE(History.Undo(*Current));
        EXPECT_FALSE(History.IsModified()); // Back at the saved state.

        ASSERT_TRUE(History.Redo(*Current));
        EXPECT_EQ(Camera().GetComponent<TransformComponent>().Position, glm::vec3(1, 2, 3));
        EXPECT_EQ(History.RedoLabels(), (std::vector<std::string> {"Rename Camera to Main"}));
        // A new edit discards what could be redone.
        Current->CreateEntity("New");
        ASSERT_TRUE(History.Record(*Current));
        EXPECT_FALSE(History.CanRedo());
        EXPECT_EQ(History.UndoLabels().front(), "Create New");
        History.MarkSaved();
        EXPECT_FALSE(History.IsModified());
    }

    TEST_F(SceneHistoryTest, StepNamesDescribeTheChange)
    {
        auto before = Current->Capture();
        Entity child = Current->CreateEntity("Child");
        EXPECT_EQ(DescribeChange(before, Current->Capture()), "Create Child");
        before = Current->Capture();
        ASSERT_TRUE(Current->SetParent(child, Camera()));
        EXPECT_EQ(DescribeChange(before, Current->Capture()), "Reparent Child");
        before = Current->Capture();
        Camera().GetComponent<TransformComponent>().Scale = {2, 2, 2};
        EXPECT_EQ(DescribeChange(before, Current->Capture()), "Scale Camera");
        before = Current->Capture();
        Camera().AddComponent<ScriptComponent>();
        EXPECT_EQ(DescribeChange(before, Current->Capture()), "Change components of Camera");
        before = Current->Capture();
        Current->DestroyEntity(Camera());
        EXPECT_EQ(DescribeChange(before, Current->Capture()), "Delete 2 entities"); // With its child.
    }

    TEST_F(SceneHistoryTest, KeepsAtMostTheLimitOfSteps)
    {
        for (size_t i = 0; i < SceneHistory::Limit + 10; ++i)
        {
            Camera().GetComponent<TransformComponent>().Position.x = float(i + 1);
            ASSERT_TRUE(History.Record(*Current));
        }
        EXPECT_EQ(History.UndoLabels().size(), SceneHistory::Limit);
    }
} // namespace
