#include <fstream>
#include <limits>
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/SceneSerializer.h"

namespace
{
    using namespace Kans;
    class SceneProductModelTest : public ::testing::Test
    {
    protected:
        std::filesystem::path Root;
        ProductModelComponent Component;
        Ref<Scene> Current;
        UUID EntityID = 101;
        void SetUp() override
        {
            Root = std::filesystem::temp_directory_path() / ("kans-scene-" + SourceAssetID::Generate().ToString());
            std::filesystem::create_directories(Root);
            Component.Model = {SourceAssetID::Generate(), 1};
            AssetID authored {SourceAssetID::Generate(), 2}, replacement {SourceAssetID::Generate(), 3};
            Component.Overrides = {{authored, replacement}};
            Current = CreateRef<Scene>("CPU scene");
            auto entity = Current->CreateEntityWithID(EntityID, "Logical Product", true);
            entity.AddComponent<ProductModelComponent>(Component);
            entity.GetComponent<TransformComponent>().Position = {2.5f, -3, 4};
        }
        void TearDown() override
        {
            Current.reset();
            std::error_code error;
            std::filesystem::remove_all(Root, error);
            EXPECT_FALSE(error);
        }
        std::string Read(const std::filesystem::path& file)
        {
            std::ifstream input(file, std::ios::binary);
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }
        static bool Same(const ProductModelComponent& a, const ProductModelComponent& b)
        {
            if (a.Model != b.Model || a.Overrides.size() != b.Overrides.size())
                return false;
            for (size_t i = 0; i < a.Overrides.size(); ++i)
                if (a.Overrides[i].Authored != b.Overrides[i].Authored ||
                    a.Overrides[i].Replacement != b.Overrides[i].Replacement)
                    return false;
            return true;
        }
        Ref<Scene> SaveAndLoad(const std::string& name)
        {
            const auto file = Root / name;
            EXPECT_TRUE(std::holds_alternative<std::monostate>(SceneSerializer(Current).SerializeChecked(file)));
            auto loaded = SceneSerializer::DeserializeChecked(file);
            EXPECT_TRUE(std::holds_alternative<Ref<Scene>>(loaded));
            return std::holds_alternative<Ref<Scene>>(loaded) ? std::get<Ref<Scene>>(loaded) : nullptr;
        }
        void Write(const std::filesystem::path& file, const std::string& contents)
        {
            std::ofstream output(file, std::ios::binary | std::ios::trunc);
            output << contents;
            ASSERT_TRUE(output.good());
        }
    };

    TEST_F(SceneProductModelTest, CameraExposurePersistsAndLegacyScenesUseDefault)
    {
        // Bug: the editor API changes exposure in memory but save/reload silently resets it.
        auto camera = Current->CreateEntityWithID(UUID(202), "HDR camera", false);
        camera.AddComponent<CameraComponent>().SceneCamera.SetExposure(2.5f);
        auto loaded = SaveAndLoad("exposure.scene");
        ASSERT_TRUE(loaded);
        EXPECT_FLOAT_EQ(loaded->GetEntityByUUID(UUID(202)).GetComponent<CameraComponent>().SceneCamera.GetExposure(),
                        2.5f);
        auto document = YAML::Load(Read(Root / "exposure.scene"));
        for (auto entity : document["Entities"])
            if (entity["CameraComponent"])
                entity["CameraComponent"].remove("Exposure");
        YAML::Emitter emitter;
        emitter << document;
        Write(Root / "legacy-exposure.scene", emitter.c_str());
        auto legacy = SceneSerializer::DeserializeChecked(Root / "legacy-exposure.scene");
        ASSERT_TRUE(std::holds_alternative<Ref<Scene>>(legacy));
        EXPECT_FLOAT_EQ(std::get<Ref<Scene>>(legacy)
                            ->GetEntityByUUID(UUID(202))
                            .GetComponent<CameraComponent>()
                            .SceneCamera.GetExposure(),
                        .8f);
    }
    TEST_F(SceneProductModelTest, ModelAndOverridesRoundTripWithoutGpuContext)
    {
        auto candidate = SaveAndLoad("logical.scene");
        ASSERT_TRUE(candidate);
        EXPECT_NE(candidate, Current);
        EXPECT_EQ(candidate->GetName(), "CPU scene");
        auto entity = candidate->GetEntityByUUID(EntityID);
        ASSERT_TRUE(entity);
        ASSERT_TRUE(entity.HasComponent<ProductModelComponent>());
        EXPECT_TRUE(Same(Component, entity.GetComponent<ProductModelComponent>()));
        EXPECT_EQ(entity.GetComponent<TransformComponent>().Position, glm::vec3(2.5f, -3, 4));
        const auto serialized = Read(Root / "logical.scene");
        EXPECT_NE(serialized.find(Component.Model.ToString()), std::string::npos);
        EXPECT_NE(serialized.find(Component.Overrides[0].Replacement.ToString()), std::string::npos);
    }

    TEST_F(SceneProductModelTest, UnassignedReferencesCanBeSavedWhileAuthoring)
    {
        auto& component = Current->GetEntityByUUID(EntityID).GetComponent<ProductModelComponent>();
        component.Model = {};
        component.Overrides.push_back({});
        auto candidate = SaveAndLoad("authoring.scene");
        ASSERT_TRUE(candidate);
        const auto& loaded = candidate->GetEntityByUUID(EntityID).GetComponent<ProductModelComponent>();
        EXPECT_FALSE(loaded.Model.IsValid());
        ASSERT_EQ(loaded.Overrides.size(), 2u);
        EXPECT_FALSE(loaded.Overrides[1].Authored.IsValid());
        EXPECT_FALSE(loaded.Overrides[1].Replacement.IsValid());
    }

    TEST_F(SceneProductModelTest, PartOverridesTakePrecedenceOverMaterialSlotOverrides)
    {
        const AssetID material   = Component.Overrides[0].Authored;
        const AssetID slotChoice = Component.Overrides[0].Replacement;
        const AssetID body {SourceAssetID::Generate(), 10}, helmet {SourceAssetID::Generate(), 11};
        const AssetID partChoice {SourceAssetID::Generate(), 12}, other {SourceAssetID::Generate(), 13};
        // Both parts use the overridden material slot.
        EXPECT_EQ(Component.ResolveMaterial(body, material), slotChoice);
        EXPECT_EQ(Component.ResolveMaterial(helmet, material), slotChoice);
        EXPECT_EQ(Component.ResolveMaterial(body, other), other);

        auto component = Component;
        component.SetOverride(helmet, partChoice);
        EXPECT_EQ(component.ResolveMaterial(helmet, material), partChoice);
        EXPECT_EQ(component.ResolveMaterial(body, material), slotChoice);
        component.SetOverride(helmet, other); // replaces, does not duplicate
        EXPECT_EQ(component.Overrides.size(), 2u);
        EXPECT_EQ(component.ResolveMaterial(helmet, material), other);
        component.SetOverride(helmet, {}); // restores
        EXPECT_EQ(component.Overrides.size(), 1u);
        EXPECT_EQ(component.ResolveMaterial(helmet, material), slotChoice);

        // Incomplete overrides are ignored.
        component.Overrides.push_back({body, {}});
        EXPECT_EQ(component.ResolveMaterial(body, material), slotChoice);
        EXPECT_EQ(Component.ResolveMaterial(AssetID {}, AssetID {}), AssetID {});
    }

    TEST_F(SceneProductModelTest, DuplicateOverrideFailsValidationAndPreservesSavedScene)
    {
        EXPECT_TRUE(std::holds_alternative<std::monostate>(ValidateProductModelComponent(Component)));
        const auto file = Root / "saved.scene";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SceneSerializer(Current).SerializeChecked(file)));
        const auto previous = Read(file);

        auto invalid = Component;
        invalid.Overrides.push_back({Component.Overrides[0].Authored, {SourceAssetID::Generate(), 4}});
        EXPECT_TRUE(std::holds_alternative<AssetError>(ValidateProductModelComponent(invalid)));
        auto entity = Current->GetEntityByUUID(EntityID);
        entity.GetComponent<ProductModelComponent>() = invalid;
        EXPECT_TRUE(std::holds_alternative<AssetError>(SceneSerializer(Current).SerializeChecked(file)));
        EXPECT_EQ(Read(file), previous);

        entity.GetComponent<ProductModelComponent>()          = Component;
        entity.GetComponent<TransformComponent>().Position[0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_TRUE(std::holds_alternative<AssetError>(SceneSerializer(Current).SerializeChecked(file)));
        EXPECT_EQ(Read(file), previous);
    }

    TEST_F(SceneProductModelTest, EarlierDrawUniformsFieldIsAcceptedAndIgnored)
    {
        const auto file = Root / "earlier.scene";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SceneSerializer(Current).SerializeChecked(file)));
        auto       text   = Read(file);
        const auto marker = std::string("Overrides:");
        const auto at     = text.find(marker);
        ASSERT_NE(at, std::string::npos);
        const auto lineStart = text.rfind('\n', at) + 1;
        const auto indent    = text.substr(lineStart, at - lineStart);
        text.insert(lineStart, indent + "DrawUniforms: []\n");
        Write(file, text);
        auto loaded = SceneSerializer::DeserializeChecked(file);
        ASSERT_TRUE(std::holds_alternative<Ref<Scene>>(loaded)) << text;
        EXPECT_TRUE(Same(
            Component, std::get<Ref<Scene>>(loaded)->GetEntityByUUID(EntityID).GetComponent<ProductModelComponent>()));
    }

    TEST_F(SceneProductModelTest, SyntacticallyValidYamlWithBrokenProductIdentityRejects)
    {
        const auto file = Root / "identity.scene";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SceneSerializer(Current).SerializeChecked(file)));
        const auto previous = Read(file);
        for (const auto& identity :
             {Component.Model, Component.Overrides[0].Authored, Component.Overrides[0].Replacement})
        {
            auto       corrupted = previous;
            const auto token     = identity.ToString();
            size_t     offset = 0, replaced = 0;
            while ((offset = corrupted.find(token, offset)) != std::string::npos)
            {
                corrupted.replace(offset, token.size(), "invalid-product-identity");
                offset += std::string("invalid-product-identity").size();
                ++replaced;
            }
            ASSERT_GT(replaced, 0u);
            ASSERT_NO_THROW(YAML::Load(corrupted));
            Write(file, corrupted);
            EXPECT_TRUE(std::holds_alternative<AssetError>(SceneSerializer::DeserializeChecked(file)));
        }
        Write(file, previous);
        EXPECT_TRUE(std::holds_alternative<Ref<Scene>>(SceneSerializer::DeserializeChecked(file)));
    }
} // namespace
