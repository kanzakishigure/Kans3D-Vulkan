#include <limits>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Loader/Loader.h"
#include "Kans3D/Asset/Product/ModelProduct.h"

namespace
{
    using namespace Kans;

    ModelData Model()
    {
        ModelData     data;
        ModelNodeData root;
        root.Name           = "Root";
        root.LocalTransform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        root.Meshes         = {AssetID(SourceAssetID::Generate(), 3)};
        data.Nodes.push_back(root);
        root.Name              = u8"实例";
        root.Parent            = 0;
        root.LocalTransform[3] = 2.125f;
        data.Nodes.push_back(root);
        return data;
    }

    template<typename T>
    void ExpectInvalid(const std::string& text, T decode)
    {
        auto result = decode(reinterpret_cast<const std::byte*>(text.data()), text.size());
        EXPECT_TRUE(std::holds_alternative<AssetError>(result));
    }

    TEST(ModelProduct, PreservesHierarchyTransformsInstancesAndOwnedLoaderData)
    {
        const auto data    = Model();
        auto       encoded = EncodeModelProduct(data);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        auto&            bytes = std::get<std::vector<std::byte>>(encoded);
        AssetLoadContext context {AssetID(SourceAssetID::Generate(), 4), AssetType::Model, bytes.data(), bytes.size()};
        auto             loaded = Loader<ModelAsset>().Load(context);
        ASSERT_TRUE(std::holds_alternative<Ref<ModelAsset>>(loaded));
        bytes.clear();
        const auto asset = std::get<Ref<ModelAsset>>(loaded);
        EXPECT_EQ(asset->assetID, context.ID);
        ASSERT_EQ(asset->GetData().Nodes.size(), 2u);
        EXPECT_EQ(asset->GetData().Nodes[1].Name, data.Nodes[1].Name);
        EXPECT_EQ(asset->GetData().Nodes[1].Parent, 0);
        EXPECT_EQ(asset->GetData().Nodes[1].LocalTransform, data.Nodes[1].LocalTransform);
        EXPECT_EQ(asset->GetData().Nodes[1].Meshes, asset->GetData().Nodes[0].Meshes);
    }

    TEST(ModelProduct, RejectsCyclesMultipleRootsMissingReferencesAndNonFiniteTransforms)
    {
        auto data            = Model();
        data.Nodes[1].Parent = 1;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeModelProduct(data)));
        data.Nodes[1].Parent = -1;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeModelProduct(data)));
        data                    = Model();
        data.Nodes[0].Meshes[0] = {};
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeModelProduct(data)));
        data                            = Model();
        data.Nodes[0].LocalTransform[0] = std::numeric_limits<float>::infinity();
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeModelProduct(data)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeModelProduct({})));
    }

    TEST(ModelProduct, RejectsMalformedProductFieldsAndVersions)
    {
        for (const auto& text :
             {"[",
              "magic: KMOD\nversion: 1\nnodes: {}",
              "magic: KMOD\nversion: 1\nnodes: []\n---\nmagic: KMOD\nversion: 1\nnodes: []",
              "magic: KMAT\nversion: 1\nnodes: []",
              "magic: KMOD\nversion: 1\nnodes: [{name: root, parent: -1, transform: [1], meshes: []}]"})
            ExpectInvalid(text, DecodeModelProduct);
        const std::string unsupported = "magic: KMOD\nversion: 2\nnodes: []";
        EXPECT_EQ(std::get<AssetError>(
                      DecodeModelProduct(reinterpret_cast<const std::byte*>(unsupported.data()), unsupported.size()))
                      .Code,
                  AssetErrorCode::UnsupportedVersion);
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeModelProduct(nullptr, 1)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Loader<ModelAsset>().Load({})));
    }

    TEST(MaterialProduct, PreservesParametersAndTextureIDsWithoutSourcePathsOrRenderer)
    {
        MaterialData original;
        original.Name      = u8"表面";
        original.BaseColor = {0.123456789f, 0.9f, 0.6f, 0.8f};
        original.Metallic  = 0.4f;
        original.Roughness = 0.65f;
        original.Textures  = {{12, 0, AssetID(SourceAssetID::Generate(), 1)},
                              {6, 1, AssetID(SourceAssetID::Generate(), 2)}};
        auto encoded       = EncodeMaterialProduct(original);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        auto&            bytes = std::get<std::vector<std::byte>>(encoded);
        AssetLoadContext context {
            AssetID(SourceAssetID::Generate(), 5), AssetType::Material, bytes.data(), bytes.size()};
        auto loaded = Loader<MaterialDataAsset>().Load(context);
        ASSERT_TRUE(std::holds_alternative<Ref<MaterialDataAsset>>(loaded));
        bytes.clear();
        auto        asset = std::get<Ref<MaterialDataAsset>>(loaded);
        const auto& data  = asset->GetData();
        EXPECT_EQ(asset->assetID, context.ID);
        EXPECT_EQ(data.Name, original.Name);
        EXPECT_EQ(data.BaseColor, original.BaseColor);
        EXPECT_EQ(data.Metallic, original.Metallic);
        EXPECT_EQ(data.Roughness, original.Roughness);
        ASSERT_EQ(data.Textures.size(), 2u);
        EXPECT_EQ(data.Textures[1].Texture, original.Textures[1].Texture);
        EXPECT_EQ(data.Textures[1].UVChannel, 1u);
        EXPECT_EQ(data.Textures[1].Semantic, 6u);
    }

    TEST(MaterialProduct, RejectsMalformedAndInvalidBindings)
    {
        MaterialData data;
        data.Textures = {{6, 0, {}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialProduct(data)));
        data.Textures[0].Texture   = AssetID(SourceAssetID::Generate(), 1);
        data.Textures[0].UVChannel = 8;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialProduct(data)));
        data.Textures.clear();
        data.Metallic = std::numeric_limits<float>::quiet_NaN();
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialProduct(data)));
        for (const auto& text :
             {"[",
              "magic: KMAT\nversion: 1",
              "magic: KMAT\nversion: 1\nname: a\nbase_color: [1,1,1,1]\nmetallic: .nan\nroughness: 1\ntextures: []"})
            ExpectInvalid(text, DecodeMaterialProduct);
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMaterialProduct(nullptr, 0)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Loader<MaterialDataAsset>().Load({})));
    }
    TEST(MaterialProduct, ShaderReferenceRoundTripsWithAndWithoutExplicitShader)
    {
        MaterialData data;
        data.Name   = "Rendering material";
        auto legacy = EncodeMaterialProduct(data);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(legacy));
        const auto& oldBytes = std::get<std::vector<std::byte>>(legacy);
        EXPECT_FALSE(std::get<MaterialData>(DecodeMaterialProduct(oldBytes.data(), oldBytes.size())).Shader.IsValid());
        data.Shader  = {SourceAssetID::Generate(), 4};
        auto encoded = EncodeMaterialProduct(data);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        const auto& bytes  = std::get<std::vector<std::byte>>(encoded);
        auto        loaded = Loader<MaterialDataAsset> {}.Load(
            {{SourceAssetID::Generate(), 1}, AssetType::Material, bytes.data(), bytes.size()});
        ASSERT_TRUE(std::holds_alternative<Ref<MaterialDataAsset>>(loaded));
        EXPECT_EQ(std::get<Ref<MaterialDataAsset>>(loaded)->GetData().Shader, data.Shader);
        EXPECT_NE(bytes, oldBytes);
    }
    TEST(MaterialProduct, PublishedVersionsOneToThreeRemainOpaqueAndSingleSided)
    {
        // Real legacy bytes, rather than encoding with today's writer, catch incompatible codec upgrades.
        for (unsigned version : {1u, 2u, 3u})
        {
            std::string text = "magic: KMAT\nversion: " + std::to_string(version) +
                               "\nname: old\nbase_color: [1,1,1,0.25]\nmetallic: 0\nroughness: 1\ntextures: []\n";
            if (version >= 2)
                text += "shader: 0000000000000123:0000000000000001\n";
            if (version == 3)
                text += "bindings: '4b4d4252010000000000000000000000'\n";
            auto decoded = DecodeMaterialProduct(reinterpret_cast<const std::byte*>(text.data()), text.size());
            ASSERT_TRUE(std::holds_alternative<MaterialData>(decoded)) << version;
            const auto& state = std::get<MaterialData>(decoded).Render;
            EXPECT_EQ(state.AlphaMode, MaterialAlphaMode::Opaque);
            EXPECT_FALSE(state.DoubleSided);
        }
    }
    TEST(MaterialProduct, VersionTwoRejectsMissingInvalidAndAliasedShaderReferences)
    {
        const std::string base =
            "magic: KMAT\nversion: 2\nname: material\nbase_color: [1,1,1,1]\nmetallic: 0\nroughness: 1\ntextures: []\n";
        ExpectInvalid(base, DecodeMaterialProduct);
        ExpectInvalid(base + "shader: invalid\n", DecodeMaterialProduct);
        MaterialData data;
        data.Shader = {SourceAssetID::Generate(), 0};
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialProduct(data)));
        data.Shader   = {SourceAssetID::Generate(), 1};
        data.Textures = {{1, 0, data.Shader}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialProduct(data)));
    }
    TEST(MaterialProduct, VersionThreePersistsOwnedRecipesAndRejectsMalformedPayloads)
    {
        MaterialData data;
        data.Shader   = {SourceAssetID::Generate(), 1};
        data.Textures = {{1, 0, {SourceAssetID::Generate(), 1}}};
        MaterialBindingRecipe recipe;
        recipe.Uniforms = {{0, 0, 0, {{MaterialParameter::BaseColor, {{0, 0}}}}, {}}};
        recipe.Textures = {{0, 1, 0, 0}};
        data.Bindings   = recipe;
        auto encoded    = EncodeMaterialProduct(data);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        auto bytes  = std::get<std::vector<std::byte>>(encoded);
        auto loaded = Loader<MaterialDataAsset>().Load(
            {{SourceAssetID::Generate(), 1}, AssetType::Material, bytes.data(), bytes.size()});
        ASSERT_TRUE(std::holds_alternative<Ref<MaterialDataAsset>>(loaded));
        const auto& restored = std::get<Ref<MaterialDataAsset>>(loaded)->GetData();
        ASSERT_TRUE(restored.Bindings.has_value());
        EXPECT_EQ(restored.Bindings->Textures[0].TextureIndex, 0u);
        EXPECT_EQ(std::get<std::vector<std::byte>>(EncodeMaterialProduct(restored)), bytes);
        bytes.clear();
        EXPECT_EQ(restored.Bindings->Uniforms[0].Parameters[0].Parameter, MaterialParameter::BaseColor);
        data.Bindings->Textures[0].TextureIndex = 1;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialProduct(data)));
        data.Bindings = MaterialBindingRecipe {};
        data.Shader   = {};
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialProduct(data)));
        const auto base = "magic: KMAT\nversion: 3\nname: a\nbase_color: [1,1,1,1]\nmetallic: 0\nroughness: "
                          "1\ntextures: []\nshader: " +
                          restored.Shader.ToString() + "\n";
        ExpectInvalid(base, DecodeMaterialProduct);
        for (const auto& value : {"[]",
                                  "''",
                                  "'gg00000000000000000000000000000000'",
                                  "'4b4d425201000000ffffffff00000000'",
                                  "'4b4d425201000000000000000000000000'"})
            ExpectInvalid(base + "bindings: " + value + "\n", DecodeMaterialProduct);
        auto empty   = base + "bindings: '4b4d4252010000000000000000000000'\n";
        auto decoded = DecodeMaterialProduct(reinterpret_cast<const std::byte*>(empty.data()), empty.size());
        ASSERT_TRUE(std::holds_alternative<MaterialData>(decoded));
        EXPECT_TRUE(std::get<MaterialData>(decoded).Bindings.has_value());
    }
} // namespace
