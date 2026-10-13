#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ShaderImporter.h"
#include "Kans3D/Asset/Importer/ShaderProgramSource.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/Asset/Storage/ProductFiles.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Renderer/RenderView.h"
#include "Kans3D/Scene/Entity.h"
namespace
{
    using namespace Kans;
    TEST(LightingHdr, NewlyCreatedLightComponentsHaveDefinedRadianceAndSpecularDefaults)
    {
        // Bug: the newly consumed specular multiplier or ambient data comes from uninitialized component memory.
        const PointLightComponent point;
        const DirLightComponent   directional;
        EXPECT_EQ(point.Diffuse_Intensity, glm::vec3(1));
        EXPECT_EQ(point.Specular_Intensity, glm::vec3(1));
        EXPECT_EQ(point.Ambient_Intensity, glm::vec3(0));
        EXPECT_EQ(directional.Direction, (glm::vec3 {0, -1, 0}));
        EXPECT_EQ(directional.Diffuse_Intensity, glm::vec3(1));
        EXPECT_EQ(directional.Specular_Intensity, glm::vec3(1));
        EXPECT_EQ(directional.Ambient_Intensity, glm::vec3(0));
    }
    TEST(LightingHdr, DistinctDiffuseRadianceAndZeroUnusedSlotsPreserveLegacyPrefix)
    {
        // Bugs: repeat a single light, use ambient radiance, or leave stale slots.
        SceneInfo scene {};
        scene.sceneCamera.Position = {3, 4, 5};
        scene.sceneCamera.camera.SetExposure(2.5f);
        scene.pointLights = {{{1, 2, 3}, {2, 4, 6}, {.5f, 1, 2}, {99, 99, 99}},
                             {{4, 5, 6}, {8, 10, 12}, {2, 3, 4}, {77, 77, 77}}};
        auto frame        = MakeLightingUniforms(scene);
        EXPECT_EQ(frame.LightingInfo, (glm::vec4 {2, 2.5f, 0, 0}));
        for (unsigned i = 0; i < 2; ++i)
        {
            EXPECT_EQ(frame.LightPositions[i], glm::vec4(scene.pointLights[i].Position, 1));
            EXPECT_EQ(frame.LightColors[i], glm::vec4(scene.pointLights[i].Diffuse_Intensity, 0));
            EXPECT_EQ(frame.LightDiffuse[i], frame.LightColors[i]);
            EXPECT_EQ(frame.PointRadiance[i], frame.LightColors[i]);
            EXPECT_EQ(frame.PointSpecular[i], glm::vec4(scene.pointLights[i].Specular_Intensity, 0));
        }
        for (unsigned i = 2; i < EnginePointLightLimit; ++i)
        {
            EXPECT_EQ(frame.PointRadiance[i], glm::vec4(0));
            if (i < 4)
                EXPECT_EQ(frame.LightColors[i], glm::vec4(0));
        }
        scene.pointLights.resize(1);
        frame = MakeLightingUniforms(scene);
        EXPECT_EQ(frame.LightingInfo.x, 1);
        EXPECT_EQ(frame.PointRadiance[1], glm::vec4(0));
        EXPECT_EQ(offsetof(EngineFrameUniforms, PointPositions), 288u);
        EXPECT_EQ(offsetof(EngineFrameUniforms, LightDiffuse), 208u);
    }
    TEST(LightingHdr, ExtractSelectsNearestSixteenWithUuidTiesAndWorldPositions)
    {
        // Bugs: keep the last light, select by local transform, nondeterministic ties or stale extraction.
        if (!Log::GetCoreLogger())
            Log::Init();
        auto scene                                         = CreateRef<Scene>();
        auto parent                                        = scene->CreateEntityWithID(UUID(100), "parent", false);
        parent.GetComponent<TransformComponent>().Position = {10, 0, 0};
        for (unsigned i = 0; i < 20; ++i)
        {
            auto entity                                        = scene->CreateEntityWithID(UUID(i + 1), "light", false);
            entity.GetComponent<TransformComponent>().Position = {float(i / 2), 0, 0};
            auto& light                                        = entity.AddComponent<PointLightComponent>();
            light.Diffuse_Intensity                            = glm::vec3(float(i + 1));
            light.Specular_Intensity                           = glm::vec3(1);
            light.Ambient_Intensity                            = glm::vec3(99);
        }
        auto far                                        = scene->CreateEntityWithID(UUID(200), "parented", false);
        far.GetComponent<TransformComponent>().Position = {0, 0, 0};
        far.AddComponent<PointLightComponent>().Diffuse_Intensity = glm::vec3(200);
        ASSERT_TRUE(scene->SetParent(far, parent, {}, false));
        RenderScene render(scene);
        RenderView  view;
        view.Scene.sceneCamera.Position = {0, 0, 0};
        render.Extract(view);
        ASSERT_EQ(view.Scene.pointLights.size(), 16u);
        for (unsigned i = 0; i < 16; ++i)
        {
            EXPECT_EQ(view.Scene.pointLights[i].Diffuse_Intensity, glm::vec3(float(i + 1)));
            EXPECT_EQ(view.Scene.pointLights[i].Position, (glm::vec3 {float(i / 2), 0, 0}));
        }
        view.Scene.sceneCamera.Position = {10, 0, 0};
        render.Extract(view);
        EXPECT_EQ(view.Scene.pointLights.front().Diffuse_Intensity, glm::vec3(200));
        EXPECT_EQ(view.Scene.pointLights.front().Position, (glm::vec3 {10, 0, 0}));
    }
    TEST(LightingHdr, DirectionalPackingNormalizesTravelDirectionAndSeparatesAmbient)
    {
        // Bug: collected directional data never reaches the shader or ambient becomes direct radiance.
        SceneInfo scene {};
        scene.HasDirectionalLight = true;
        scene.dirLight            = {{0, 0, -4}, {2, 3, 4}, {.1f, .2f, .3f}, {.4f, .5f, .6f}};
        const auto frame          = MakeLightingUniforms(scene);
        EXPECT_EQ(frame.DirectionalDirection, (glm::vec4 {0, 0, -1, 1}));
        EXPECT_EQ(frame.DirectionalRadiance, (glm::vec4 {2, 3, 4, 0}));
        EXPECT_EQ(frame.DirectionalAmbient, (glm::vec4 {.4f, .5f, .6f, 0}));
        EXPECT_EQ(frame.DirectionalSpecular, (glm::vec4 {.1f, .2f, .3f, 0}));
    }
    TEST(LightingHdr, ExtractionSelectsDirectionalUuidAndClearsRemovedLights)
    {
        // Bug: collection drops directional lights, uses iteration order, or retains last frame's lights.
        if (!Log::GetCoreLogger())
            Log::Init();
        auto  scene          = CreateRef<Scene>();
        auto  first          = scene->CreateEntityWithID(UUID(2), "first", false);
        auto  second         = scene->CreateEntityWithID(UUID(1), "second", false);
        auto& a              = first.AddComponent<DirLightComponent>();
        a.Direction          = {0, 0, -1};
        a.Diffuse_Intensity  = glm::vec3(2);
        a.Specular_Intensity = glm::vec3(1);
        a.Ambient_Intensity  = glm::vec3(.2f);
        auto& b              = second.AddComponent<DirLightComponent>();
        b.Direction          = {0, -1, 0};
        b.Diffuse_Intensity  = glm::vec3(3);
        b.Specular_Intensity = glm::vec3(2);
        b.Ambient_Intensity  = glm::vec3(.3f);
        RenderScene render(scene);
        RenderView  view;
        render.Extract(view);
        EXPECT_TRUE(view.Scene.HasDirectionalLight);
        EXPECT_EQ(view.Scene.dirLight.Diffuse_Intensity, glm::vec3(3));
        scene->DestroyEntity(second);
        render.Extract(view);
        EXPECT_EQ(view.Scene.dirLight.Diffuse_Intensity, glm::vec3(2));
        scene->DestroyEntity(first);
        render.Extract(view);
        EXPECT_FALSE(view.Scene.HasDirectionalLight);
        EXPECT_EQ(MakeLightingUniforms(view.Scene).DirectionalAmbient, glm::vec4(0));
    }
    TEST(LightingHdr, OutputSpaceDefaultsAndCodecRetainExplicitHdrAndRejectInvalidFields)
    {
        // Bugs: default existing sources to HDR, drop the declaration during import/codec, accept bad values.
        const std::string main = "KansShaderProgram 1\nvertex=v.vert\nfragment=f.frag\n";
        EXPECT_EQ(std::get<ShaderOutputSpace>(ParseShaderOutputSpace(main)), ShaderOutputSpace::Display);
        EXPECT_EQ(std::get<ShaderOutputSpace>(ParseShaderOutputSpace(main + "output_space=linear_hdr\n")),
                  ShaderOutputSpace::LinearHDR);
        for (const auto& field : {"output_space=bogus\n", "output_space=display\noutput_space=linear_hdr\n"})
            EXPECT_TRUE(std::holds_alternative<AssetError>(ParseShaderProgramSource(main + field)));
        const auto& standard = GetStandardShader();
        EXPECT_EQ(standard.OutputSpace, ShaderOutputSpace::LinearHDR);
        auto bytes = std::get<std::vector<std::byte>>(
            EncodeShaderProduct(standard.Code, {}, {}, {}, {}, {}, standard.OutputSpace));
        EXPECT_EQ(std::get<ShaderData>(DecodeShaderProduct(bytes.data(), bytes.size())).OutputSpace,
                  ShaderOutputSpace::LinearHDR);
        bytes.back() = std::byte {255};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(bytes.data(), bytes.size())));
    }
    TEST(LightingHdr, PublishedVersionSixFramePrefixLoadsUnmodified)
    {
        // Bug: reject real v6 bytes or require appended members in old bytecode.
        auto bytes = std::get<std::vector<std::byte>>(
            ReadProductFile(std::filesystem::path(KANS_TEST_MULTIPASS_DIR) / "lights-prefix-v6.kshd"));
        ASSERT_EQ(bytes[4], std::byte {6});
        auto decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        const auto& data = std::get<ShaderData>(decoded);
        EXPECT_EQ(data.OutputSpace, ShaderOutputSpace::Display);
        EXPECT_TRUE(std::holds_alternative<std::monostate>(ValidateMaterialShaderInterface(data.Reflection)));
        for (const auto& stage : data.Reflection)
            for (const auto& binding : stage.Bindings)
                if (binding.Set == 0 && binding.Binding == 0)
                {
                    ASSERT_EQ(binding.Members.size(), 7u);
                    EXPECT_EQ(binding.Members[4].Offset, 208u);
                    EXPECT_EQ(binding.Members[6].Offset, 276u);
                }
    }
} // namespace
