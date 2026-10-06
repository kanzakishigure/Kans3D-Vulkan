#include <limits>
#include <map>
#include <gtest/gtest.h>
#include <shaderc/shaderc.hpp>
#include "Kans3D/Asset/Product/MaterialBindingRecipe.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Runtime/MaterialBindings.h"

namespace
{
    using namespace Kans;
    class MaterialBindingsTest : public ::testing::Test
    {
    protected:
        MaterialData          Data;
        ShaderProgramCode     Code;
        MaterialBindingRecipe Recipe;
        void                  SetUp() override
        {
            Recipe.Uniforms = {{0,
                                0,
                                0,
                                {{MaterialParameter::BaseColor, {{0, 0}}},
                                 {MaterialParameter::Metallic, {{1, 0}}},
                                 {MaterialParameter::Roughness, {{2, 0}}}},
                                {}}};
            Recipe.Textures = {{0, 1, 0, 0}, {0, 1, 1, 0}};
            shaderc::Compiler       compiler;
            shaderc::CompileOptions options;
            options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
            auto compiled = compiler.CompileGlslToSpv(R"(#version 450
layout(std140,set=0,binding=0) uniform Parameters {vec4 color;float metallic;float roughness;} p;
layout(set=0,binding=1) uniform sampler2D images[2];
layout(location=0) out vec4 color;
void main(){color=p.color+vec4(p.metallic+p.roughness)+texture(images[1],vec2(0));})",
                                                      shaderc_fragment_shader,
                                                      "material.frag",
                                                      options);
            ASSERT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success);
            Code           = {{{ShaderProductStage::Fragment, "main", {compiled.cbegin(), compiled.cend()}}}};
            Data.Shader    = {SourceAssetID::Generate(), 1};
            Data.BaseColor = {0.25f, 0.5f, 0.75f, 1};
            Data.Metallic  = 0.4f;
            Data.Roughness = 0.6f;
            Data.Textures  = {{1, 1, {SourceAssetID::Generate(), 1}}};
            Data.Bindings  = Recipe;
        }
        std::variant<std::vector<MaterialUniformBlock>, AssetError> Pack(const MaterialBindingRecipe& recipe,
                                                                         uint64_t budget = 64 * 1024 * 1024)
        {
            return PackMaterialUniforms(Data, Code, recipe, budget);
        }
    };
    TEST_F(MaterialBindingsTest, ParametersArePackedIntoTheReflectedUniformLayout)
    {
        auto packed = Pack(Recipe);
        ASSERT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(packed));
        const auto& blocks = std::get<std::vector<MaterialUniformBlock>>(packed);
        ASSERT_EQ(blocks.size(), 1u);
        ASSERT_EQ(blocks[0].Bytes.size(), 24u);
        // BaseColor.r = 0.25f = 0x3e800000, little endian.
        EXPECT_EQ(blocks[0].Bytes[2], std::byte {0x80});
        EXPECT_EQ(blocks[0].Bytes[3], std::byte {0x3e});
    }
    TEST_F(MaterialBindingsTest, MissingDuplicateWrongShapeAndBudgetBindingsFail)
    {
        auto recipe = Recipe;
        recipe.Textures.pop_back();
        EXPECT_TRUE(std::holds_alternative<AssetError>(Pack(recipe)));
        recipe                     = Recipe;
        recipe.Textures[1].Element = 0;
        EXPECT_TRUE(std::holds_alternative<AssetError>(Pack(recipe)));
        recipe.Textures[1].Element = 2;
        EXPECT_TRUE(std::holds_alternative<AssetError>(Pack(recipe)));
        recipe                                     = Recipe;
        recipe.Uniforms[0].Parameters[0].Parameter = MaterialParameter::Metallic;
        EXPECT_TRUE(std::holds_alternative<AssetError>(Pack(recipe)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Pack(Recipe, 1)));
        recipe                          = Recipe;
        recipe.Textures[0].TextureIndex = 2;
        EXPECT_TRUE(std::holds_alternative<AssetError>(Pack(recipe)));
        EXPECT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(Pack(Recipe)));
    }
    TEST_F(MaterialBindingsTest, ConstantsOverlapRejectsAndConstantValuesArePacked)
    {
        auto recipe = Recipe;
        recipe.Uniforms[0].Values.push_back({{{0, 0}}, std::vector<float> {1, 1, 1, 1}});
        EXPECT_TRUE(std::holds_alternative<AssetError>(Pack(recipe)));
        recipe.Uniforms[0].Parameters.erase(recipe.Uniforms[0].Parameters.begin());
        auto constant = Pack(recipe);
        ASSERT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(constant));
        // 1.0f = 0x3f800000.
        EXPECT_EQ(std::get<std::vector<MaterialUniformBlock>>(constant)[0].Bytes[3], std::byte {0x3f});
    }
    TEST_F(MaterialBindingsTest, PersistedRecipeRoundTripsAndPacksIdentically)
    {
        auto encoded = EncodeMaterialBindingRecipe(Recipe);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        const auto& bytes = std::get<std::vector<std::byte>>(encoded);
        EXPECT_EQ(bytes[0], std::byte {'K'});
        auto decoded = DecodeMaterialBindingRecipe(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<MaterialBindingRecipe>(decoded));
        auto restored = Pack(std::get<MaterialBindingRecipe>(decoded));
        auto original = Pack(Recipe);
        ASSERT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(restored));
        ASSERT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(original));
        EXPECT_EQ(std::get<std::vector<MaterialUniformBlock>>(restored)[0].Bytes,
                  std::get<std::vector<MaterialUniformBlock>>(original)[0].Bytes);
        EXPECT_EQ(
            std::get<std::vector<std::byte>>(EncodeMaterialBindingRecipe(std::get<MaterialBindingRecipe>(decoded))),
            bytes);
        for (size_t size = 0; size < bytes.size(); ++size)
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMaterialBindingRecipe(bytes.data(), size)));
        auto corrupt = bytes;
        corrupt.push_back(std::byte {0});
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMaterialBindingRecipe(corrupt.data(), corrupt.size())));
        corrupt      = bytes;
        corrupt[4]   = std::byte {2};
        auto version = DecodeMaterialBindingRecipe(corrupt.data(), corrupt.size());
        ASSERT_TRUE(std::holds_alternative<AssetError>(version));
        EXPECT_EQ(std::get<AssetError>(version).Code, AssetErrorCode::UnsupportedVersion);
    }
    TEST_F(MaterialBindingsTest, RecipeTypedConstantsAndMalformedCounts)
    {
        auto recipe               = Recipe;
        recipe.Uniforms[0].Values = {{{{0, 0}}, std::vector<float> {0.25f, -0.0f}},
                                     {{{1, 0}}, std::vector<int32_t> {INT32_MIN, -1, INT32_MAX}},
                                     {{{2, 0}}, std::vector<uint32_t> {0, UINT32_MAX}}};
        auto encoded              = EncodeMaterialBindingRecipe(recipe);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        auto bytes   = std::get<std::vector<std::byte>>(encoded);
        auto decoded = DecodeMaterialBindingRecipe(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<MaterialBindingRecipe>(decoded));
        EXPECT_EQ(std::get<std::vector<int32_t>>(std::get<MaterialBindingRecipe>(decoded).Uniforms[0].Values[1].Values),
                  (std::vector<int32_t> {INT32_MIN, -1, INT32_MAX}));
        EXPECT_EQ(
            std::get<std::vector<std::byte>>(EncodeMaterialBindingRecipe(std::get<MaterialBindingRecipe>(decoded))),
            bytes);
        for (size_t i = 8; i < 12; ++i)
            bytes[i] = std::byte {255};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMaterialBindingRecipe(bytes.data(), bytes.size())));
        recipe.Textures[1].Element = 0;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialBindingRecipe(recipe)));
        recipe = Recipe;
        recipe.Uniforms[0].Parameters[0].Path.clear();
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialBindingRecipe(recipe)));
        recipe                                     = Recipe;
        recipe.Uniforms[0].Parameters[0].Parameter = MaterialParameter(99);
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialBindingRecipe(recipe)));
        recipe = Recipe;
        recipe.Uniforms[0].Values.push_back({{{0, 0}}, std::vector<float> {std::numeric_limits<float>::infinity()}});
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMaterialBindingRecipe(recipe)));
    }
} // namespace
