#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Renderer/RenderPipeline.h"
#include "Kans3D/Scene/Entity.h"

namespace
{
    using namespace Kans;
    AssetID ID(uint64_t local)
    {
        static auto source = SourceAssetID::Generate();
        return {source, local};
    }
    TEST(RenderViewCpu, StableOpaqueMaskAndBackToFrontBlendUseTransformedBoundsAndCamera)
    {
        // Catches sorting opaque by distance, mask separation, near-first blend,
        // transform-origin distance, or unstable equal-distance blends.
        RenderView view;
        view.Scene.sceneCamera.Position     = {10, 0, 0};
        const MaterialAlphaMode modes[]     = {MaterialAlphaMode::Blend,
                                               MaterialAlphaMode::Mask,
                                               MaterialAlphaMode::Opaque,
                                               MaterialAlphaMode::Blend,
                                               MaterialAlphaMode::Mask,
                                               MaterialAlphaMode::Blend,
                                               MaterialAlphaMode::Opaque};
        const float             distances[] = {1, 9, 8, 5, 2, 5, 7};
        for (size_t i = 0; i < 7; ++i)
        {
            ViewShaderDraw draw {};
            draw.Mesh = ID(i + 1);
            // Bounds centre at +2, scaled by 2, then translated relative to camera.
            draw.LocalBounds        = {{1, -1, -1}, {3, 1, 1}};
            draw.Transform          = glm::translate(glm::mat4(1), glm::vec3(10 + distances[i] - 4, 0, 0)) *
                                      glm::scale(glm::mat4(1), glm::vec3(2));
            draw.Render.AlphaMode   = modes[i];
            draw.Render.AlphaCutoff = .35f;
            draw.Render.DoubleSided = true;
            draw.Ready              = true;
            view.Draws.push_back(draw);
        }
        auto unavailable  = view.Draws[0];
        unavailable.Ready = false;
        view.Draws.push_back(unavailable);
        ExtractDrawLists(view);
        std::vector<size_t> actual;
        for (const auto& draw : view.MaterialOrder)
            actual.push_back(draw.DrawIndex);
        EXPECT_EQ(actual, (std::vector<size_t> {1, 2, 4, 6, 3, 5, 0}));
        EXPECT_EQ(view.Opaque, (std::vector<size_t> {2, 6}));
        EXPECT_EQ(view.Mask, (std::vector<size_t> {1, 4}));
        EXPECT_EQ(view.Blend, (std::vector<size_t> {3, 5, 0}));
        ASSERT_EQ(view.MaterialOrder.size(), 7u);
        EXPECT_FLOAT_EQ(view.MaterialOrder[4].DistanceSquared, 25);
        EXPECT_FLOAT_EQ(view.MaterialOrder[4].Render.AlphaCutoff, .35f);
        EXPECT_TRUE(view.MaterialOrder[4].Render.DoubleSided);
        EXPECT_FLOAT_EQ(view.MaterialOrder[6].DistanceSquared, 1);
        view.Draws.clear();
        ExtractDrawLists(view);
        EXPECT_TRUE(view.MaterialOrder.empty());
        EXPECT_TRUE(view.Opaque.empty());
        EXPECT_TRUE(view.Mask.empty());
        EXPECT_TRUE(view.Blend.empty());
    }
    TEST(RenderViewCpu, ExtractsReadyPartsComposesTransformsAndKeepsOffscreenModels)
    {
        // Catches accidentally adding frustum culling or drawing unloaded parts,
        // reversed transforms, and ignoring per-part/authored material overrides.
        RenderView            view;
        ProductModelComponent model {ID(1)};
        model.SetOverride(ID(2), ID(9));
        model.SetOverride(ID(5), ID(10));
        ModelInstances instances;
        instances.Instances = {
            {ID(2), glm::translate(glm::mat4(1), {3, 0, 0})}, {ID(3), glm::mat4(1)}, {ID(4), glm::mat4(1)}};
        GpuMesh metadata;
        metadata.MaterialSlots = {{"slot", "slot", ID(6), ID(5)}};
        metadata.Sections      = {{0, 3, 0, {{-1, -1, -1}, {1, 1, 1}}}};
        const auto transform   = glm::translate(glm::mat4(1), {1000, 0, 0}) * glm::scale(glm::mat4(1), glm::vec3(2));
        ExtractMeshDraws(view, {{ID(2), {ID(9)}}, transform}, SceneViewMode::Normals, nullptr);
        EXPECT_TRUE(view.Draws.empty());
        for (const auto& instance : instances.Instances)
        {
            const auto material = model.ResolveMaterial(instance.Mesh, ID(6), ID(5), instance.Part);
            ExtractMeshDraws(
                view,
                {{instance.Mesh, {material}}, transform * instance.Transform, 0, 0, ModelStencilMode::WriteReference},
                SceneViewMode::Normals,
                instance.Mesh == ID(3) ? nullptr : &metadata);
        }
        ASSERT_EQ(view.Draws.size(), 2u);
        EXPECT_EQ(view.Draws[0].Mesh, ID(2));
        EXPECT_EQ(view.Draws[1].Mesh, ID(4));
        EXPECT_EQ(view.Draws[0].Material, ID(9));
        EXPECT_EQ(view.Draws[1].Material, ID(10));
        EXPECT_EQ(glm::vec3(view.Draws[0].Transform[3]), glm::vec3(1006, 0, 0));
        EXPECT_EQ(view.Draws[0].ViewMode, SceneViewMode::Normals);
        EXPECT_EQ(view.Draws[0].Stencil, ModelStencilMode::WriteReference);
        EXPECT_FALSE(view.Draws[0].Ready); // Material readiness is independently prepared.
    }
    TEST(RenderViewCpu, SceneSnapshotKeepsHierarchyLightsSpriteGridAndShellIntentWithoutDevice)
    {
        // No device exists in this CPU suite. Catches accidental resource creation
        // during extraction, local instead of world transforms and legacy flags lost.
        if (!Log::GetCoreLogger())
            Log::Init();
        auto scene                                         = CreateRef<Scene>();
        auto parent                                        = scene->CreateEntity("parent");
        parent.GetComponent<TransformComponent>().Position = {2, 3, 4};
        auto  model                                        = scene->CreateEntity("model");
        model.AddComponent<MeshComponent>(MeshComponent {ID(1), {ID(9)}});
        model.GetComponent<TransformComponent>().Position = {1, 0, 0};
        ASSERT_TRUE(scene->SetParent(model, parent, {}, false));
        auto sprite = scene->CreateEntity("sprite");
        // Default component has no texture; no GPU-backed construction.
        auto& spriteComponent                              = sprite.AddComponent<SpriteRendererComponent>();
        spriteComponent.Color                              = {.1f, .2f, .3f, .4f};
        sprite.GetComponent<TransformComponent>().Position = {0, 5, 0};
        auto light                                         = scene->CreateEntity("light");
        light.GetComponent<TransformComponent>().Position  = {-1, 0, 0};
        ASSERT_TRUE(scene->SetParent(light, parent, {}, false));
        auto& point                    = light.AddComponent<PointLightComponent>();
        point.Ambient_Intensity        = {7, 8, 9};
        point.Diffuse_Intensity        = {4, 5, 6};
        point.Specular_Intensity       = {1, 2, 3};
        auto  directionalEntity        = scene->CreateEntity("directional");
        auto& directional              = directionalEntity.AddComponent<DirLightComponent>();
        directional.Direction          = {-.1f, -.9f, .2f};
        directional.Ambient_Intensity  = {3, 2, 1};
        directional.Diffuse_Intensity  = {6, 5, 4};
        directional.Specular_Intensity = {9, 8, 7};
        RenderScene    render(scene);
        RenderResource settings {};
        settings.Piplinestate.EnableStencil     = true;
        settings.Piplinestate.EnableOutline     = true;
        settings.Piplinestate.EnableDebugNormal = true;
        render.SetRenderResource(settings);
        RenderView view;
        view.Scene.sceneCamera.Position   = {9, 8, 7};
        view.Scene.sceneCamera.viewMatrix = glm::mat4(2);
        view.Scene.sceneCamera.camera     = Camera(glm::mat4(3));
        view.Viewport                     = {800, 600};
        view.DebugMode                    = SceneViewMode::Wireframe;
        render.Extract(view);
        EXPECT_EQ(view.Scene.sceneCamera.Position, glm::vec3(9, 8, 7));
        EXPECT_EQ(view.Scene.sceneCamera.viewMatrix, glm::mat4(2));
        EXPECT_EQ(view.Scene.sceneCamera.camera.GetProjectionMatrix(), glm::mat4(3));
        EXPECT_EQ(view.Viewport, glm::uvec2(800, 600));
        EXPECT_EQ(view.DebugMode, SceneViewMode::Wireframe);
        EXPECT_EQ(view.Scene.pointLight.Position, glm::vec3(1, 3, 4));
        EXPECT_EQ(view.Scene.pointLight.Ambient_Intensity, glm::vec3(7, 8, 9));
        EXPECT_EQ(view.Scene.pointLight.Diffuse_Intensity, glm::vec3(4, 5, 6));
        EXPECT_EQ(view.Scene.pointLight.Specular_Intensity, glm::vec3(1, 2, 3));
        EXPECT_EQ(view.Scene.dirLight.Dirction, glm::vec3(-.1f, -.9f, .2f));
        EXPECT_EQ(view.Scene.dirLight.Ambient_Intensity, glm::vec3(3, 2, 1));
        EXPECT_EQ(view.Scene.dirLight.Diffuse_Intensity, glm::vec3(6, 5, 4));
        EXPECT_EQ(view.Scene.dirLight.Specular_Intensity, glm::vec3(9, 8, 7));
        ASSERT_EQ(view.Meshes.size(), 1u);
        EXPECT_EQ(view.Meshes[0].Mesh.Mesh, ID(1));
        ASSERT_EQ(view.Meshes[0].Mesh.Materials.size(), 1u);
        EXPECT_EQ(view.Meshes[0].Mesh.Materials[0], ID(9));
        EXPECT_EQ(view.Meshes[0].EntityID, model.GetUUID());
        EXPECT_EQ(glm::vec3(view.Meshes[0].Transform[3]), glm::vec3(3, 3, 4));
        EXPECT_EQ(view.Meshes[0].Stencil, ModelStencilMode::WriteReference);
        ASSERT_EQ(view.Geometry.size(), 3u);
        EXPECT_EQ(view.Geometry[0].ShaderName, "StencilShader");
        EXPECT_EQ(view.Geometry[0].Mode, ModelGeometryMode::StencilOutline);
        EXPECT_FLOAT_EQ(view.Geometry[0].Transform[0][0], 1.007f);
        EXPECT_EQ(view.Geometry[1].ShaderName, "OutLineShader");
        EXPECT_EQ(view.Geometry[1].Mode, ModelGeometryMode::BackFaces);
        EXPECT_EQ(view.Geometry[2].ShaderName, "DebugNormalShader");
        ASSERT_EQ(view.Sprites.size(), 1u);
        EXPECT_EQ(view.Sprites[0].Color, spriteComponent.Color);
        EXPECT_EQ(glm::vec3(view.Sprites[0].Transform[3]), glm::vec3(0, 5, 0));
        ASSERT_EQ(view.Grids.size(), 1u);
        EXPECT_FLOAT_EQ(view.Grids[0].Size, .025f);
        EXPECT_FLOAT_EQ(view.Grids[0].Scale, 16.025f);
        EXPECT_TRUE(view.Grids[0].State.DepthWrite);
        EXPECT_FALSE(view.Grids[0].State.Blend);
        ASSERT_EQ(view.ImmediateOrder.size(), 2u);
        EXPECT_EQ(view.ImmediateOrder[0].Kind, ViewImmediateKind::Sprites);
        EXPECT_EQ(view.ImmediateOrder[1].Kind, ViewImmediateKind::Grid);
        settings.Piplinestate.EnableStencil     = false;
        settings.Piplinestate.EnableOutline     = false;
        settings.Piplinestate.EnableDebugNormal = false;
        settings.Piplinestate.EnableGridShader  = false;
        render.SetRenderResource(settings);
        render.Extract(view);
        EXPECT_TRUE(view.Geometry.empty());
        EXPECT_TRUE(view.Grids.empty());
        EXPECT_EQ(view.Meshes[0].Stencil, ModelStencilMode::Disabled);
    }
    TEST(RenderViewCpu, PipelineRegistryProvidesForwardAndRejectsUnknownAndDuplicateNames)
    {
        // Catches silently selecting another pipeline or accepting an empty factory.
        EXPECT_NE(dynamic_cast<ForwardPipeline*>(RenderPipelineRegistry::Create("Forward").get()), nullptr);
        EXPECT_THROW(RenderPipelineRegistry::Create("missing"), std::invalid_argument);
        EXPECT_THROW(RenderPipelineRegistry::Register("Forward", [] { return std::make_unique<ForwardPipeline>(); }),
                     std::invalid_argument);
        EXPECT_THROW(RenderPipelineRegistry::Register("bad", {}), std::invalid_argument);
    }
} // namespace
