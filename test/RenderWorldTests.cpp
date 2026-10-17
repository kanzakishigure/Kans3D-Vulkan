#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Renderer/RenderPipeline.h"
#include "Kans3D/Renderer/RenderWorld.h"
#include "Kans3D/Renderer/Resource/InternalShader.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/ScenePlaySession.h"
#include "Kans3D/Scene/SceneSerializer.h"
#include "Kans3D/Scene/ScriptableEntity.h"
#include "Kans3D/Script/ScriptAgent.h"
#include "../KansEditor/src/SceneHistory.h"

namespace
{
    using namespace Kans;
    class RenderWorldTest : public testing::Test
    {
    protected:
        const SourceAssetID Source = SourceAssetID::FromString("9876543210abcdef");
        const AssetID       MeshID {Source, 2}, Material {Source, 3}, Override {Source, 4};
        Ref<Scene>          Current = CreateRef<Scene>();
        Entity              Root, Part;
        MeshData            Mesh;
        ModelData           Prefab;
        void                SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            Mesh.MaterialSlots = {{"one", "one", Material, Material}};
            Mesh.Sections      = {{0, 3, 0, {{-1, -1, -1}, {0, 1, 1}}}, {3, 3, 0, {{0, -1, -1}, {1, 1, 1}}}};
            PrefabNodeData node;
            node.Key            = "part";
            node.Name           = "sectioned";
            node.Mesh           = MeshID;
            node.LocalTransform = {1, 0, 0, 2, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            Prefab.Entities     = {node};
            Root                = Current->CreateEntityWithID(UUID(100), "root", false);
            PrefabInstanceComponent instance;
            instance.Source              = CreateRef<PrefabAsset>(Prefab);
            instance.MeshSources[MeshID] = CreateRef<MeshAsset>(Mesh);
            Root.AddComponent<PrefabInstanceComponent>(instance);
            Sync();
            Part = Current->GetEntityByUUID(Scene::PrefabEntityUUID(Root.GetUUID(), "part"));
            ASSERT_TRUE(Part);
            Current->GetRenderWorld().SetValidationEnabled(true);
        }
        void                                       Sync() { Current->GetRenderWorld().Synchronize(); }
        const std::vector<RenderWorld::DrawProxy>& Draws() { return Current->GetRenderWorld().GetEntityDraws(Part); }
        void                                       ExpectX(float x)
        {
            ASSERT_EQ(Draws().size(), 2u);
            EXPECT_EQ(glm::vec3(Draws()[0].Transform[3]), glm::vec3(x, 0, 0));
            EXPECT_EQ(Draws()[0].WorldBounds.Min, glm::vec3(x - 1, -1, -1));
            EXPECT_EQ(Draws()[1].WorldBounds.Max, glm::vec3(x + 1, 1, 1));
        }
    };
    TEST_F(RenderWorldTest, RenderSceneViewsShareSceneWorldAndSettingsAcrossViewSwitches)
    {
        // Bug: proxies are frame-local, leak across scenes, or scene switching drops legacy visual settings.
        RenderScene    first(Current), second(Current);
        RenderResource settings;
        settings.Piplinestate.EnableStencil = true;
        first.SetRenderResource(settings);
        first.PrepareEnvironment("world-environment");
        EXPECT_TRUE(second.GetRenderResource().Piplinestate.EnableStencil);
        EXPECT_EQ(second.GetEnvironmentPath(), std::filesystem::path("world-environment"));
        Part.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 6; });
        RenderView a, b;
        first.Extract(a);
        second.Extract(b);
        ASSERT_EQ(a.Draws.size(), 2u);
        ASSERT_EQ(b.Draws.size(), 2u);
        EXPECT_EQ(a.Draws[0].Transform, b.Draws[0].Transform);
        EXPECT_EQ(a.Draws[0].Transform[3].x, 6);
        first.SetLogicScene(CreateRef<Scene>());
        first.Extract(a);
        second.Extract(b);
        EXPECT_TRUE(a.Draws.empty());
        EXPECT_EQ(b.Draws.size(), 2u);
        EXPECT_TRUE(first.GetRenderResource().Piplinestate.EnableStencil);
        EXPECT_EQ(first.GetEnvironmentPath(), std::filesystem::path("world-environment"));
    }
    TEST_F(RenderWorldTest, InspectorStyleNoOpPatchesDoNotDirtyParentSubtrees)
    {
        // Bug: an open inspector notifies every frame even when all widget values are unchanged.
        Root.PatchComponent<TransformComponent>([](auto&) {});
        Part.PatchComponent<MeshComponent>([](auto&) {});
        Sync();
        const auto stats = Current->GetRenderWorld().GetStats();
        EXPECT_EQ(stats.EntitiesVisited, 0u);
        EXPECT_EQ(stats.BoundsUpdated, 0u);
        EXPECT_EQ(stats.PrefabNodesVisited, 0u);
        EXPECT_EQ(stats.Created + stats.Updated + stats.Removed, 0u);
    }
    TEST_F(RenderWorldTest, DirectPatchAndWorldTransformReachBothSections)
    {
        // Bug: existing component edits emit no update, or only the first section updates.
        Part.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 7; });
        Sync();
        ExpectX(7);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Updated, 2u);
        Current->SetWorldTransform(Part, glm::translate(glm::mat4(1), glm::vec3(11, 0, 0)));
        Sync();
        ExpectX(11);
    }
    TEST_F(RenderWorldTest, SectionSelectionRemovesOldProxyAndCreatesNewSectionIdentity)
    {
        // Bug: section edits retain a removed section or count a different section as an update.
        Part.PatchComponent<MeshComponent>([](auto& m) { m.Section = 1; });
        Sync();
        ASSERT_EQ(Draws().size(), 1u);
        EXPECT_EQ(Draws()[0].Section, 1u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Removed, 1u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Updated, 1u);
        Part.PatchComponent<MeshComponent>([](auto& m) { m.Section = 0; });
        Sync();
        ASSERT_EQ(Draws().size(), 1u);
        EXPECT_EQ(Draws()[0].Section, 0u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Created, 1u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Removed, 1u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Updated, 0u);
    }
    TEST_F(RenderWorldTest, ParentMoveAndReparentDirtyEntireSubtree)
    {
        // Bug: only the parent's proxy is dirtied, or SetParent(false) forgets descendants.
        auto ancestor = Current->CreateEntity("ancestor");
        Current->SetParent(Root, ancestor, {}, false);
        ancestor.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 5; });
        Sync();
        ExpectX(7);
        EXPECT_EQ(Current->GetWorldBounds(ancestor).Min, glm::vec3(6, -1, -1));
        auto other = Current->CreateEntity("other");
        other.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 9; });
        ASSERT_TRUE(Current->SetParent(Root, other, {}, false));
        Sync();
        ExpectX(11);
        EXPECT_FALSE(Current->GetWorldBounds(ancestor).IsValid());
        ASSERT_TRUE(Current->SetParent(Root, ancestor, {}, true));
        Sync();
        ExpectX(11);
    }
    TEST_F(RenderWorldTest, PrefabReconcileAndMaterialOverrideRefreshEverySection)
    {
        // Bug: reconcile assigns raw references; material slot override changes ECS but not proxies.
        TransformComponent transform;
        transform.Position.x = 8;
        Current->SetPrefabTransformOverride(Root, "part", transform);
        Current->SetPrefabMaterialOverride(Root, "part", 0, Override);
        Sync();
        ExpectX(8);
        EXPECT_EQ(Draws()[0].Material, Override);
        EXPECT_EQ(Draws()[1].Material, Override);
        Current->ClearPrefabOverride(Root, "part");
        Sync();
        ExpectX(2);
        EXPECT_EQ(Draws()[0].Material, Material);
        EXPECT_EQ(Draws()[1].Material, Material);
    }
    TEST_F(RenderWorldTest, UndoRedoAndPlayStopRefreshAlreadySynchronizedWorld)
    {
        // Bug: registry clear/restore preserves stale proxies with the same UUID.
        SceneHistory history;
        history.Reset(*Current);
        Root.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 5; });
        ASSERT_TRUE(history.Record(*Current));
        Sync();
        ExpectX(7);
        ASSERT_TRUE(history.Undo(*Current));
        Sync();
        Root = Current->GetEntityByUUID(UUID(100));
        Part = Current->GetEntityByUUID(Scene::PrefabEntityUUID(UUID(100), "part"));
        ExpectX(2);
        ASSERT_TRUE(history.Redo(*Current));
        Sync();
        Root = Current->GetEntityByUUID(UUID(100));
        Part = Current->GetEntityByUUID(Scene::PrefabEntityUUID(UUID(100), "part"));
        ExpectX(7);
        ScenePlaySession play;
        play.Play(*Current);
        Root.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 20; });
        Sync();
        ExpectX(22);
        play.Stop(*Current);
        Sync();
        Part = Current->GetEntityByUUID(Scene::PrefabEntityUUID(UUID(100), "part"));
        ExpectX(7);
    }
    class MovingScript : public ScriptableEntity
    {
        void OnUpdate(TimeStep) override
        {
            PatchComponent<TransformComponent>([](auto& t) { t.Position.x += 4; });
        }
    };
    TEST_F(RenderWorldTest, NativeScriptWritesReachWorld)
    {
        // Bug: ScriptableEntity writes bypass the common mutation path.
        Root.AddComponent<NativeScriptComponent>().Bind<MovingScript>();
        Current->OnUpdate(.1f);
        Sync();
        ExpectX(6);
    }
    TEST_F(RenderWorldTest, ManagedScriptBindingSettersNotifyExistingProxies)
    {
        // Bug: the registered managed translation/rotation setters assign raw component references.
        ScriptAgent::SetTranslation(Part, {9, 0, 0});
        Sync();
        ExpectX(9);
        ScriptAgent::SetRotation(Part, {.1f, .2f, .3f});
        Sync();
        ASSERT_EQ(Draws().size(), 2u);
        EXPECT_EQ(Draws()[0].Transform, Current->GetWorldTransform(Part));
        EXPECT_EQ(Draws()[1].WorldBounds.Min,
                  Mesh.Sections[1].LocalBounds.Transformed(Current->GetWorldTransform(Part)).Min);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Updated, 2u);
    }
    TEST_F(RenderWorldTest, DuplicateAndDestroyedEntityNeverLeaveProxies)
    {
        // Bug: copy skips notifications, or destruction leaves draws until a full extraction.
        const auto oldHandle = entt::entity(Part);
        const auto copied    = Current->DuplicateEntity(Root);
        Sync();
        auto copyPart = Current->GetEntityByUUID(Scene::PrefabEntityUUID(copied.GetUUID(), "part"));
        ASSERT_TRUE(copyPart);
        ASSERT_EQ(Current->GetRenderWorld().GetEntityDraws(copyPart).size(), 2u);
        Current->DestroyEntity(Root);
        Sync();
        EXPECT_TRUE(Current->GetRenderWorld().GetEntityDraws(oldHandle).empty());
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Removed, 2u);
        EXPECT_EQ(Current->GetRenderWorld().GetEntityDraws(copyPart).size(), 2u);
        copyPart.RemoveComponent<MeshComponent>();
        Sync();
        EXPECT_TRUE(Current->GetRenderWorld().GetEntityDraws(copyPart).empty());
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Removed, 2u);
    }
    TEST_F(RenderWorldTest, ReplacedMeshMetadataRefreshesSectionBoundsAndDefaults)
    {
        // Bug: a replacement CPU mesh is retained by proxies despite prefab reconciliation.
        Mesh.Sections[1].LocalBounds.Max.x    = 12;
        Mesh.MaterialSlots[0].DefaultMaterial = Override;
        Root.PatchComponent<PrefabInstanceComponent>([&](auto& p) {
            p.MeshSources[MeshID] = CreateRef<MeshAsset>(Mesh);
            p.ExpandedSource.reset();
        });
        Sync();
        ASSERT_EQ(Draws().size(), 2u);
        EXPECT_EQ(Draws()[1].WorldBounds.Max.x, 14);
        EXPECT_EQ(Draws()[0].Material, Override);
        EXPECT_EQ(Current->GetWorldBounds(Root).Max.x, 14);
    }
    TEST_F(RenderWorldTest, UnchangedFramesVisitNothingAndSingleEditIsIndependentOfSceneSize)
    {
        // Bug: per-frame full ECS extraction, CapturePrefabOverrides or UpdateBounds scan.
        Current->GetRenderWorld().SetValidationEnabled(false);
        for (int i = 0; i < 2000; ++i)
        {
            auto root         = Current->CreateEntity("unrelated");
            auto instance     = Root.GetComponent<PrefabInstanceComponent>();
            instance.Expanded = false;
            instance.ExpandedSource.reset();
            instance.Nodes.clear();
            instance.DrawOrder = 0;
            root.AddComponent<PrefabInstanceComponent>(instance);
        }
        Sync();
        EXPECT_EQ(Current->GetRenderWorld().CollectDraws().size(), 4002u);
        Sync();
        const auto idle = Current->GetRenderWorld().GetStats();
        EXPECT_EQ(idle.PrefabNodesVisited, 0u);
        EXPECT_EQ(idle.EntitiesVisited, 0u);
        EXPECT_EQ(idle.PrefabsVisited, 0u);
        EXPECT_EQ(idle.BoundsUpdated, 0u);
        EXPECT_EQ(idle.Created + idle.Updated + idle.Removed, 0u);
        Part.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 3; });
        Sync();
        const auto changed = Current->GetRenderWorld().GetStats();
        EXPECT_EQ(changed.PrefabNodesVisited, 1u);
        EXPECT_EQ(changed.EntitiesVisited, 1u);
        EXPECT_EQ(changed.PrefabsVisited, 0u);
        EXPECT_EQ(changed.BoundsUpdated, 2u);
        EXPECT_EQ(changed.Created, 0u);
        EXPECT_EQ(changed.Updated, 2u);
        EXPECT_EQ(changed.Removed, 0u);
    }
    TEST_F(RenderWorldTest, SinglePartEditDoesNotReconcileOrScanUnchangedPrefabSiblings)
    {
        // Bug: capturing one part edit scans/reconciles every node of its large prefab.
        Current->GetRenderWorld().SetValidationEnabled(false);
        const auto prototype = Prefab.Entities[0];
        for (int i = 0; i < 2000; ++i)
        {
            auto node = prototype;
            node.Key  = "extra-" + std::to_string(i);
            Prefab.Entities.push_back(node);
        }
        Root.PatchComponent<PrefabInstanceComponent>([&](auto& p) { p.Source = CreateRef<PrefabAsset>(Prefab); });
        Sync();
        Sync();
        Part.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 3; });
        Sync();
        const auto stats = Current->GetRenderWorld().GetStats();
        EXPECT_EQ(stats.EntitiesVisited, 1u);
        EXPECT_EQ(stats.PrefabsVisited, 0u);
        EXPECT_EQ(stats.PrefabNodesVisited, 1u);
        EXPECT_EQ(stats.BoundsUpdated, 2u);
        EXPECT_EQ(stats.Created, 0u);
        EXPECT_EQ(stats.Updated, 2u);
        EXPECT_EQ(stats.Removed, 0u);
        ExpectX(3);
    }
    TEST_F(RenderWorldTest, DebugValidationNamesEntityForForgottenTransformMaterialLightAndSpriteNotification)
    {
        // Bug: the validation checks only the dirty set, hiding exactly the missing notifications.
        auto& transform      = Part.GetComponent<TransformComponent>();
        transform.Position.x = 99;
        try
        {
            Sync();
            FAIL() << "missing notification accepted";
        }
        catch (const std::logic_error& e)
        {
            EXPECT_NE(std::string(e.what()).find(std::to_string(uint64_t(Part.GetUUID()))), std::string::npos);
        }
        Part.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 98; });
        Sync();
        Part.GetComponent<MeshComponent>().Materials[0] = Override;
        EXPECT_THROW(Sync(), std::logic_error);
        Part.PatchComponent<MeshComponent>([&](auto& m) { m.Materials[0] = Material; });
        Sync();
        auto light = Current->CreateEntity("light");
        light.AddComponent<PointLightComponent>();
        auto sprite = Current->CreateEntity("sprite");
        sprite.AddComponent<SpriteRendererComponent>();
        Sync();
        light.GetComponent<PointLightComponent>().Diffuse_Intensity.x = 9;
        EXPECT_THROW(Sync(), std::logic_error);
        light.PatchComponent<PointLightComponent>([](auto& l) { l.Diffuse_Intensity.x = 1; });
        Sync();
        sprite.GetComponent<SpriteRendererComponent>().Color.x = .3f;
        EXPECT_THROW(Sync(), std::logic_error);
    }
    TEST_F(RenderWorldTest, LightAndSpriteEditsAndRemovalReachCachedExtraction)
    {
        // Bug: only mesh changes subscribe to the notification path.
        auto light = Current->CreateEntity("point");
        light.AddComponent<PointLightComponent>();
        auto dir = Current->CreateEntity("directional");
        dir.AddComponent<DirLightComponent>();
        auto sprite = Current->CreateEntity("sprite");
        sprite.AddComponent<SpriteRendererComponent>();
        Sync();
        light.PatchComponent<PointLightComponent>([](auto& l) { l.Diffuse_Intensity = {3, 4, 5}; });
        dir.PatchComponent<DirLightComponent>([](auto& l) { l.Direction = {1, 0, 0}; });
        sprite.PatchComponent<SpriteRendererComponent>([](auto& s) { s.Color = {.2f, .3f, .4f, .5f}; });
        sprite.PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 7; });
        Sync();
        RenderView view;
        view.Scene.sceneCamera.Position = {0, 0, 0};
        Current->GetRenderWorld().Extract(view, {});
        ASSERT_EQ(view.Sprites.size(), 1u);
        EXPECT_EQ(view.Sprites[0].Color, glm::vec4(.2f, .3f, .4f, .5f));
        EXPECT_EQ(view.Sprites[0].Transform[3].x, 7);
        ASSERT_EQ(view.Scene.pointLights.size(), 1u);
        EXPECT_EQ(view.Scene.pointLights[0].Diffuse_Intensity, glm::vec3(3, 4, 5));
        EXPECT_EQ(view.Scene.dirLight.Dirction, glm::vec3(1, 0, 0));
        light.RemoveComponent<PointLightComponent>();
        dir.RemoveComponent<DirLightComponent>();
        sprite.RemoveComponent<SpriteRendererComponent>();
        Sync();
        Current->GetRenderWorld().Extract(view, {});
        EXPECT_TRUE(view.Sprites.empty());
        EXPECT_TRUE(view.Scene.pointLights.empty());
        EXPECT_FALSE(view.Scene.HasDirectionalLight);
    }
    TEST_F(RenderWorldTest, SpriteMembershipUpdatesPreserveLegacyOrderWithoutScanningUnchangedSprites)
    {
        // Bug: one sprite removal rescans the entire group, or swap/removal/re-add changes alpha submission order.
        Current->GetRenderWorld().SetValidationEnabled(false);
        entt::registry oracle;
        // The fixture already has two non-sprite transforms. Other component pools do not own this group.
        oracle.emplace<TransformComponent>(oracle.create());
        oracle.emplace<TransformComponent>(oracle.create());
        std::vector<Entity>       entities;
        std::vector<entt::entity> reference;
        for (int i = 0; i < 2000; ++i)
        {
            SpriteRendererComponent sprite;
            sprite.Color.x = float(i + 1);
            auto entity    = Current->CreateEntity("sprite");
            entity.AddComponent<SpriteRendererComponent>(sprite);
            entities.push_back(entity);
            auto handle = oracle.create();
            oracle.emplace<TransformComponent>(handle);
            oracle.emplace<SpriteRendererComponent>(handle, sprite);
            reference.push_back(handle);
        }
        // Independent stage-3 oracle: iterate the original EnTT owning group at extraction time.
        const auto compare = [&] {
            auto group = oracle.group<TransformComponent>(entt::get<SpriteRendererComponent>);
            Sync();
            RenderView view;
            Current->GetRenderWorld().Extract(view, {});
            ASSERT_EQ(view.Sprites.size(), group.size());
            size_t index = 0;
            for (auto handle : group)
            {
                EXPECT_EQ(view.Sprites[index].Color, group.get<SpriteRendererComponent>(handle).Color);
                EXPECT_EQ(view.Sprites[index].Transform, group.get<TransformComponent>(handle).GetTransform());
                ++index;
            }
        };
        compare();
        ASSERT_EQ(Current->GetRenderWorld().GetStats().SpriteOrderVisits, 2000u);
        entities[17].RemoveComponent<SpriteRendererComponent>();
        oracle.remove<SpriteRendererComponent>(reference[17]);
        compare();
        EXPECT_EQ(Current->GetRenderWorld().GetStats().EntitiesVisited, 1u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().SpriteOrderVisits, 1u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Removed, 1u);
        SpriteRendererComponent replacement;
        replacement.Color.x = 3000;
        entities[17].AddComponent<SpriteRendererComponent>(replacement);
        oracle.emplace<SpriteRendererComponent>(reference[17], replacement);
        compare();
        EXPECT_EQ(Current->GetRenderWorld().GetStats().SpriteOrderVisits, 1u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Created, 1u);
        Current->DestroyEntity(entities[333]);
        oracle.destroy(reference[333]);
        compare();
        EXPECT_EQ(Current->GetRenderWorld().GetStats().SpriteOrderVisits, 1u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().Removed, 1u);
        // Multiple structural edits between synchronizations must also preserve unaffected members.
        entities[100].RemoveComponent<SpriteRendererComponent>();
        oracle.remove<SpriteRendererComponent>(reference[100]);
        entities[100].AddComponent<SpriteRendererComponent>(replacement);
        oracle.emplace<SpriteRendererComponent>(reference[100], replacement);
        entities[300].RemoveComponent<SpriteRendererComponent>();
        oracle.remove<SpriteRendererComponent>(reference[300]);
        entities[20].PatchComponent<TransformComponent>([](auto& t) { t.Position.x = 5; });
        oracle.patch<TransformComponent>(reference[20], [](auto& t) { t.Position.x = 5; });
        compare();
        Sync();
        EXPECT_EQ(Current->GetRenderWorld().GetStats().SpriteOrderVisits, 0u);
        EXPECT_EQ(Current->GetRenderWorld().GetStats().EntitiesVisited, 0u);
        Current->GetRenderWorld().SetValidationEnabled(true);
        Sync();
    }
    TEST(RenderFrustum, AllSixVulkanPlanesKeepStraddlingAndRejectFullyOutsideBoxes)
    {
        // Bug: test the centre instead of the positive AABB vertex; use OpenGL's -w near plane.
        const Frustum frustum(glm::mat4(1));
        const Bounds  outside[]    = {{{-3, -.1f, .4f}, {-2, .1f, .6f}},
                                      {{2, -.1f, .4f}, {3, .1f, .6f}},
                                      {{-.1f, -3, .4f}, {.1f, -2, .6f}},
                                      {{-.1f, 2, .4f}, {.1f, 3, .6f}},
                                      {{-.1f, -.1f, -.5f}, {.1f, .1f, -.1f}},
                                      {{-.1f, -.1f, 1.1f}, {.1f, .1f, 2}}};
        const Bounds  straddling[] = {{{-3, -.1f, .4f}, {-.9f, .1f, .6f}},
                                      {{.9f, -.1f, .4f}, {3, .1f, .6f}},
                                      {{-.1f, -3, .4f}, {.1f, -.9f, .6f}},
                                      {{-.1f, .9f, .4f}, {.1f, 3, .6f}},
                                      {{-.1f, -.1f, -2}, {.1f, .1f, .1f}},
                                      {{-.1f, -.1f, .9f}, {.1f, .1f, 3}}};
        for (int i = 0; i < 6; ++i)
        {
            SCOPED_TRACE(i);
            EXPECT_FALSE(frustum.Intersects(outside[i]));
            EXPECT_TRUE(frustum.Intersects(straddling[i]));
        }
        EXPECT_TRUE(frustum.Intersects({}));
        EXPECT_TRUE(frustum.Intersects({{-1, -1, 0}, {1, 1, 1}}));
    }
    TEST(RenderFrustum, ForwardConvertsEngineProjectionAndUsesProjectionTimesView)
    {
        // Bug: use the raw GL projection with a 0..1 near plane, transpose incorrectly, or reverse V/P.
        RenderView view;
        view.Scene.sceneCamera.Position   = {5, 2, 10};
        view.Scene.sceneCamera.viewMatrix = glm::lookAt(glm::vec3(5, 2, 10), glm::vec3(5, 2, 0), glm::vec3(0, 1, 0));
        view.Scene.sceneCamera.camera     = Camera(glm::perspective(glm::radians(60.f), 1.f, 1.f, 20.f));
        const glm::vec3 centers[]         = {{5, 2, 8.7f}, {5, 2, 9.5f}, {5, 2, -11}, {5, 2, 11}, {100, 2, 0}};
        for (auto center : centers)
        {
            ViewShaderDraw draw {};
            draw.Ready       = true;
            draw.Transform   = glm::translate(glm::mat4(1), center);
            draw.LocalBounds = {{-.01f, -.01f, -.01f}, {.01f, .01f, .01f}};
            view.Draws.push_back(draw);
        }
        ForwardPipeline().Prepare(view);
        EXPECT_EQ(view.Opaque, (std::vector<size_t> {0}));
        ASSERT_EQ(view.MaterialOrder.size(), 1u);
        EXPECT_EQ(view.MaterialOrder[0].DrawIndex, 0u);
        EXPECT_EQ(view.Draws.size(), 5u); // Camera visibility never deletes the world/selection input.
    }
    TEST(RenderFrustum, CullingPreservesOpaqueMaskSubmissionAndStableBlendTies)
    {
        // Bug: erase/reinsert reorders survivors, use origin for distance, or retain culled draws in a list.
        RenderView view;
        view.Scene.sceneCamera.Position = {0, 0, 0};
        const MaterialAlphaMode modes[] = {MaterialAlphaMode::Opaque,
                                           MaterialAlphaMode::Blend,
                                           MaterialAlphaMode::Mask,
                                           MaterialAlphaMode::Blend,
                                           MaterialAlphaMode::Opaque,
                                           MaterialAlphaMode::Mask,
                                           MaterialAlphaMode::Blend,
                                           MaterialAlphaMode::Blend};
        const float             z[]     = {.2f, .3f, .5f, .8f, .6f, .7f, .8f, .2f};
        for (size_t i = 0; i < 8; ++i)
        {
            ViewShaderDraw draw {};
            draw.Ready            = true;
            draw.Transform        = glm::mat4(1);
            draw.Render.AlphaMode = modes[i];
            draw.LocalBounds      = {{-.01f, -.01f, z[i] - .01f}, {.01f, .01f, z[i] + .01f}};
            if (i == 1 || i == 4)
                draw.Transform = glm::translate(glm::mat4(1), glm::vec3(50, 0, 0));
            view.Draws.push_back(draw);
        }
        const Frustum frustum(glm::mat4(1));
        ExtractDrawLists(view, &frustum);
        EXPECT_EQ(view.Opaque, (std::vector<size_t> {0}));
        EXPECT_EQ(view.Mask, (std::vector<size_t> {2, 5}));
        EXPECT_EQ(view.Blend, (std::vector<size_t> {3, 6, 7}));
        std::vector<size_t> actual;
        for (const auto& d : view.MaterialOrder)
            actual.push_back(d.DrawIndex);
        EXPECT_EQ(actual, (std::vector<size_t> {0, 2, 5, 3, 6, 7}));
    }
} // namespace
