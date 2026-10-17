#include <gtest/gtest.h>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/ScenePlaySession.h"
#include "Kans3D/Scene/ScriptableEntity.h"
#include "Kans3D/Script/ScriptEngine.h"

namespace
{
    using namespace Kans;

    class TrackingScript : public ScriptableEntity
    {
    public:
        inline static std::vector<float> Updates;
        inline static int DestroyCalls = 0;
        inline static int Destructors = 0;

        ~TrackingScript() override { ++Destructors; }

    protected:
        void OnUpdate(TimeStep ts) override
        {
            Updates.push_back(ts.GetSeconds());
            GetComponent<TransformComponent>().Position.x += ts.GetSeconds();
        }
        void OnDestory() override { ++DestroyCalls; }
    };

    class ScenePlaySessionTest : public ::testing::Test
    {
    protected:
        Ref<Scene>       Current = CreateRef<Scene>("Play session");
        ScenePlaySession Session;

        void SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            ASSERT_FALSE(ScriptEngine::IsInitialized());
            TrackingScript::Updates.clear();
            TrackingScript::DestroyCalls = 0;
            TrackingScript::Destructors = 0;
        }
        void TearDown() override { Session.Discard(*Current); }

        Entity AddTrackingScript()
        {
            Entity entity = Current->CreateEntity("Native script");
            entity.AddComponent<NativeScriptComponent>().Bind<TrackingScript>();
            return entity;
        }
    };

    TEST_F(ScenePlaySessionTest, TransitionsFromPlayToPauseAndResumesWithoutResetting)
    {
        Session.Play(*Current);
        EXPECT_EQ(Session.GetState(), ScenePlaySession::State::Playing);
        EXPECT_TRUE(Session.IsActive());
        ASSERT_TRUE(Session.Update(*Current, 0.25f));
        Session.Pause();
        Session.Pause();
        EXPECT_EQ(Session.GetState(), ScenePlaySession::State::Paused);
        EXPECT_TRUE(Session.IsActive());
        EXPECT_FALSE(Session.Update(*Current, 0.5f));
        Session.Play(*Current);
        EXPECT_EQ(Session.GetState(), ScenePlaySession::State::Playing);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 0.25f);
        EXPECT_EQ(Session.GetFrameCount(), 1u);
        Session.Stop(*Current);
        EXPECT_EQ(Session.GetState(), ScenePlaySession::State::Edit);
        EXPECT_FALSE(Session.IsActive());
    }

    TEST_F(ScenePlaySessionTest, StopRestoresTransformsComponentsHierarchyAndEntityIDs)
    {
        Entity parent = Current->CreateEntity("Parent");
        Entity child = Current->CreateEntity("Child");
        Entity sibling = Current->CreateEntity("Sibling");
        Entity removed = Current->CreateEntity("Removed");
        Entity removedChild = Current->CreateEntity("Removed child");
        ASSERT_TRUE(Current->SetParent(child, parent, {}, false));
        ASSERT_TRUE(Current->SetParent(sibling, parent, child, false));
        ASSERT_TRUE(Current->SetParent(removedChild, removed, {}, false));
        parent.GetComponent<TransformComponent>().Position = {1, 2, 3};
        child.GetComponent<TransformComponent>().Rotation = {0.1f, 0.2f, 0.3f};
        sibling.GetComponent<TransformComponent>().Scale = {2, 3, 4};
        parent.AddComponent<CameraComponent>().Primary = false;
        child.AddComponent<SpriteRendererComponent>().Color = {0.2f, 0.3f, 0.4f, 0.5f};
        child.AddComponent<ScriptComponent>("EditScript");
        sibling.AddComponent<PrefabInstanceComponent>().Prefab = {SourceAssetID::Generate(), 7};
        auto& light = removed.AddComponent<PointLightComponent>();
        light.Diffuse_Intensity = {1, 2, 3};
        light.Specular_Intensity = {4, 5, 6};
        light.Ambient_Intensity = {0.1f, 0.2f, 0.3f};
        const UUID parentID = parent.GetUUID();
        const UUID childID = child.GetUUID();
        const UUID siblingID = sibling.GetUUID();
        const UUID removedID = removed.GetUUID();
        const UUID removedChildID = removedChild.GetUUID();
        const auto snapshot = Current->Capture();

        Session.Play(*Current);
        parent.GetComponent<TransformComponent>().Position = {10, 20, 30};
        child.GetComponent<TransformComponent>().Rotation = {1, 2, 3};
        sibling.GetComponent<TransformComponent>().Scale = {5, 6, 7};
        parent.GetComponent<TagComponent>().Tag = "Runtime parent";
        parent.RemoveComponent<CameraComponent>();
        parent.AddComponent<PointLightComponent>();
        child.GetComponent<ScriptComponent>().ClassName = "RuntimeScript";
        child.GetComponent<ScriptComponent>().IsLoaded = true;
        child.RemoveComponent<SpriteRendererComponent>();
        sibling.GetComponent<PrefabInstanceComponent>().Prefab = {};
        ASSERT_TRUE(Current->SetParent(child, {}, parent, false));
        Current->DestroyEntity(removed);
        Entity created = Current->CreateEntity("Runtime entity");
        const UUID createdID = created.GetUUID();
        ASSERT_TRUE(Current->SetParent(created, parent, sibling, false));
        Current->SetName("Runtime scene");
        Session.Pause();
        Session.Stop(*Current);

        EXPECT_EQ(Current->Capture(), snapshot);
        EXPECT_FALSE(Current->GetEntityByUUID(createdID));
        ASSERT_TRUE(Current->GetEntityByUUID(removedID));
        ASSERT_TRUE(Current->GetEntityByUUID(removedChildID));
        EXPECT_EQ(Current->GetParent(Current->GetEntityByUUID(removedChildID)).GetUUID(), removedID);
        Entity restoredParent = Current->GetEntityByUUID(parentID);
        ASSERT_TRUE(restoredParent);
        const auto children = Current->GetChildren(restoredParent);
        ASSERT_EQ(children.size(), 2u);
        EXPECT_EQ(children[0].GetUUID(), siblingID);
        EXPECT_EQ(children[1].GetUUID(), childID);
        EXPECT_FALSE(children[1].GetComponent<ScriptComponent>().IsLoaded);
        const auto roots = Current->GetRootEntities();
        ASSERT_EQ(roots.size(), 2u);
        EXPECT_EQ(roots[0].GetUUID(), parentID);
        EXPECT_EQ(roots[1].GetUUID(), removedID);
    }

    TEST_F(ScenePlaySessionTest, UpdateRunsScriptsOnlyWhilePlaying)
    {
        Entity entity = AddTrackingScript();
        EXPECT_FALSE(Session.Update(*Current, 0.5f));
        EXPECT_TRUE(TrackingScript::Updates.empty());
        Session.Play(*Current);
        EXPECT_TRUE(Session.Update(*Current, 0.25f));
        EXPECT_EQ(TrackingScript::Updates, (std::vector<float> {0.25f}));
        EXPECT_FLOAT_EQ(entity.GetComponent<TransformComponent>().Position.x, 0.25f);
        Session.Pause();
        EXPECT_FALSE(Session.Update(*Current, 0.5f));
        EXPECT_EQ(TrackingScript::Updates.size(), 1u);
        Session.Play(*Current);
        EXPECT_TRUE(Session.Update(*Current, 0.5f));
        EXPECT_EQ(TrackingScript::Updates, (std::vector<float> {0.25f, 0.5f}));
        Session.Stop(*Current);
        EXPECT_FALSE(Session.Update(*Current, 1.0f));
        EXPECT_EQ(TrackingScript::Updates.size(), 2u);
    }

    TEST_F(ScenePlaySessionTest, StepRunsExactlyOneFixedFrameWhilePaused)
    {
        Entity entity = AddTrackingScript();
        Session.Step();
        Session.Play(*Current);
        Session.Step();
        Session.Pause();
        EXPECT_FALSE(Session.Update(*Current, 1.0f));
        Session.Step();
        Session.Step();
        EXPECT_TRUE(Session.Update(*Current, 10.0f));
        EXPECT_EQ(Session.GetState(), ScenePlaySession::State::Paused);
        ASSERT_EQ(TrackingScript::Updates.size(), 1u);
        EXPECT_FLOAT_EQ(TrackingScript::Updates[0], ScenePlaySession::FixedStep);
        EXPECT_FLOAT_EQ(entity.GetComponent<TransformComponent>().Position.x, ScenePlaySession::FixedStep);
        EXPECT_EQ(Session.GetFrameCount(), 1u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), ScenePlaySession::FixedStep);
        EXPECT_FALSE(Session.Update(*Current, 10.0f));
        Session.Step();
        EXPECT_TRUE(Session.Update(*Current, 0.0f));
        EXPECT_FALSE(Session.Update(*Current, 10.0f));
        EXPECT_EQ(TrackingScript::Updates.size(), 2u);
        EXPECT_EQ(Session.GetFrameCount(), 2u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 2.0f * ScenePlaySession::FixedStep);
    }

    TEST_F(ScenePlaySessionTest, CountersTrackSimulatedUpdatesAndResetBetweenSessions)
    {
        Session.Play(*Current);
        ASSERT_TRUE(Session.Update(*Current, 0.25f));
        ASSERT_TRUE(Session.Update(*Current, 0.5f));
        EXPECT_EQ(Session.GetFrameCount(), 2u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 0.75f);
        Session.Pause();
        EXPECT_FALSE(Session.Update(*Current, 3.0f));
        Session.Step();
        ASSERT_TRUE(Session.Update(*Current, 3.0f));
        EXPECT_EQ(Session.GetFrameCount(), 3u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 0.75f + ScenePlaySession::FixedStep);
        Session.Play(*Current);
        ASSERT_TRUE(Session.Update(*Current, 0.0f));
        EXPECT_EQ(Session.GetFrameCount(), 4u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 0.75f + ScenePlaySession::FixedStep);
        Session.Stop(*Current);
        EXPECT_EQ(Session.GetFrameCount(), 0u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 0.0f);
        Session.Play(*Current);
        ASSERT_TRUE(Session.Update(*Current, 0.125f));
        EXPECT_EQ(Session.GetFrameCount(), 1u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 0.125f);
        Session.Discard(*Current);
        EXPECT_EQ(Session.GetFrameCount(), 0u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 0.0f);
    }

    TEST_F(ScenePlaySessionTest, DoublePlayKeepsOriginalSnapshotAndCountersAndDoubleStopDoesNothing)
    {
        Entity entity = Current->CreateEntity("Edit");
        const UUID id = entity.GetUUID();
        const auto snapshot = Current->Capture();
        Session.Play(*Current);
        entity.GetComponent<TransformComponent>().Position = {1, 2, 3};
        ASSERT_TRUE(Session.Update(*Current, 0.25f));
        Session.Play(*Current);
        EXPECT_EQ(Session.GetFrameCount(), 1u);
        EXPECT_FLOAT_EQ(Session.GetPlayTime(), 0.25f);
        Session.Pause();
        Session.Play(*Current);
        Session.Stop(*Current);
        EXPECT_EQ(Current->Capture(), snapshot);

        Entity restored = Current->GetEntityByUUID(id);
        restored.GetComponent<TransformComponent>().Position = {7, 8, 9};
        Session.Stop(*Current);
        EXPECT_EQ(Current->GetEntityByUUID(id), restored);
        EXPECT_EQ(restored.GetComponent<TransformComponent>().Position, glm::vec3(7, 8, 9));
    }

    TEST_F(ScenePlaySessionTest, PendingStepIsClearedByResumeStopAndDiscard)
    {
        Session.Play(*Current);
        Session.Pause();
        Session.Step();
        Session.Play(*Current);
        Session.Pause();
        EXPECT_FALSE(Session.Update(*Current, 1.0f));
        Session.Step();
        Session.Stop(*Current);
        Session.Play(*Current);
        Session.Pause();
        EXPECT_FALSE(Session.Update(*Current, 1.0f));
        Session.Step();
        Session.Discard(*Current);
        Session.Play(*Current);
        Session.Pause();
        EXPECT_FALSE(Session.Update(*Current, 1.0f));
    }

    TEST_F(ScenePlaySessionTest, DiscardKeepsRuntimeEditsAndAllowsSwitchingScenes)
    {
        Entity removed = Current->CreateEntity("Removed");
        const UUID removedID = removed.GetUUID();
        Session.Play(*Current);
        Current->DestroyEntity(removed);
        Entity created = AddTrackingScript();
        const UUID createdID = created.GetUUID();
        created.GetComponent<TransformComponent>().Position = {7, 8, 9};
        auto& native = created.GetComponent<NativeScriptComponent>();
        native.Instance = native.InstantiateFunction();
        const auto runtimeSnapshot = Current->Capture();
        Session.Pause();
        Session.Discard(*Current);
        EXPECT_EQ(Session.GetState(), ScenePlaySession::State::Edit);
        EXPECT_FALSE(Session.IsActive());
        EXPECT_EQ(Current->Capture(), runtimeSnapshot);
        EXPECT_FALSE(Current->GetEntityByUUID(removedID));
        EXPECT_TRUE(Current->GetEntityByUUID(createdID));
        EXPECT_EQ(native.Instance, nullptr);
        EXPECT_EQ(TrackingScript::DestroyCalls, 1);
        EXPECT_EQ(TrackingScript::Destructors, 1);
        Session.Discard(*Current);
        Session.Stop(*Current);
        EXPECT_EQ(Current->Capture(), runtimeSnapshot);
        EXPECT_EQ(TrackingScript::Destructors, 1);

        auto next = CreateRef<Scene>("Next");
        Session.Play(*next);
        EXPECT_TRUE(Session.Update(*next, 0.25f));
        Session.Stop(*next);
        EXPECT_FALSE(Session.IsActive());
    }

    TEST_F(ScenePlaySessionTest, OperationsOnAnotherSceneDoNotAffectActiveSession)
    {
        auto other = CreateRef<Scene>("Other");
        other->CreateEntity("Other entity");
        const auto snapshot = other->Capture();
        Session.Play(*Current);
        Session.Pause();
        Session.Step();
        Session.Play(*other);
        Session.Stop(*other);
        Session.Discard(*other);
        EXPECT_EQ(Session.GetState(), ScenePlaySession::State::Paused);
        EXPECT_FALSE(Session.Update(*other, 1.0f));
        EXPECT_TRUE(Session.Update(*Current, 1.0f));
        EXPECT_EQ(other->Capture(), snapshot);
        EXPECT_EQ(Session.GetFrameCount(), 1u);
    }

    TEST_F(ScenePlaySessionTest, RestoreReleasesLiveNativeScriptInstances)
    {
        Entity entity = Current->CreateEntity("Edit");
        const UUID id = entity.GetUUID();
        const auto snapshot = Current->Capture();
        auto& native = entity.AddComponent<NativeScriptComponent>();
        native.Bind<TrackingScript>();
        native.Instance = native.InstantiateFunction();
        Current->Restore(snapshot);
        EXPECT_EQ(TrackingScript::DestroyCalls, 1);
        EXPECT_EQ(TrackingScript::Destructors, 1);
        EXPECT_EQ(Current->Capture(), snapshot);
        EXPECT_FALSE(Current->GetEntityByUUID(id).HasComponent<NativeScriptComponent>());
        Current->Restore(snapshot);
        EXPECT_EQ(TrackingScript::Destructors, 1);
    }

    TEST_F(ScenePlaySessionTest, StopReleasesLiveNativeScriptInstancesBeforeRestore)
    {
        const auto snapshot = Current->Capture();
        Session.Play(*Current);
        Entity entity = AddTrackingScript();
        auto& native = entity.GetComponent<NativeScriptComponent>();
        native.Instance = native.InstantiateFunction();
        Session.Stop(*Current);
        EXPECT_EQ(TrackingScript::DestroyCalls, 1);
        EXPECT_EQ(TrackingScript::Destructors, 1);
        EXPECT_EQ(Current->Capture(), snapshot);
        Session.Stop(*Current);
        EXPECT_EQ(TrackingScript::Destructors, 1);
    }
} // namespace
