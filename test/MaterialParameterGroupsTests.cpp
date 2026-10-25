#include <algorithm>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Asset/Importer/ShaderImporter.h"
#include "Kans3D/Asset/Importer/ShaderProgramSource.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "MaterialShaderTestUtilities.h"

namespace
{
    using namespace Kans;
    std::variant<ShaderData, AssetError> GroupedShader(const std::string& groups)
    {
        return CompileEmbeddedShaderProgram("KansShaderProgram 1\nvertex=v.vert\nfragment=f.frag\n" + groups,
                                            {{"v.vert", TestMaterialVertex}, {"f.frag", TestMaterialFragment}});
    }
    TEST(MaterialParameterGroups, DeclarationOrderAndMixedMembersSurviveImportProductAndSchema)
    {
        // A map or sorting by binding silently rearranges the editor's authored layout.
        auto compiled = GroupedShader("group Z Surface = Steps u_Albedo Tint\ngroup A Detail = Offsets\n");
        ASSERT_TRUE(std::holds_alternative<ShaderData>(compiled));
        auto described = DescribeMaterial(&std::get<ShaderData>(compiled));
        ASSERT_TRUE(std::holds_alternative<MaterialSchema>(described));
        const auto& schema = std::get<MaterialSchema>(described);
        ASSERT_EQ(schema.Groups.size(), 2u);
        EXPECT_EQ(schema.Groups[0].Name, "Z Surface");
        EXPECT_EQ(schema.Groups[0].Members, (std::vector<std::string> {"Steps", "u_Albedo", "Tint"}));
        EXPECT_EQ(schema.Groups[1].Name, "A Detail");
        EXPECT_EQ(schema.Groups[1].Members, (std::vector<std::string> {"Offsets"}));
        // Ungrouped fields remain editable.
        EXPECT_NE(std::find_if(schema.Parameters.begin(),
                               schema.Parameters.end(),
                               [](const auto& p) { return p.Name == "Strength"; }),
                  schema.Parameters.end());
    }
    TEST(MaterialParameterGroups, RepeatedMembersWithinAndAcrossGroupsAreErrors)
    {
        for (const auto& declarations : {"group Surface = Tint Tint\n", "group Surface = Tint\ngroup Other = Tint\n"})
            EXPECT_TRUE(std::holds_alternative<AssetError>(ParseShaderProgramGroups(declarations)));
    }
    TEST(MaterialParameterGroups, UnknownAndEngineMembersAreImportErrors)
    {
        // Typos and engine uniforms must not create empty controls in a material group.
        for (const auto& member : {"Tnit", "CameraPosition"})
            EXPECT_TRUE(
                std::holds_alternative<AssetError>(GroupedShader("group Surface = " + std::string(member) + "\n")));
    }
    TEST(MaterialParameterGroups, ProductDecoderRejectsDuplicateUnknownAndTruncatedMembers)
    {
        auto compiled = GroupedShader("group Surface = u_Albedo Strength\n");
        ASSERT_TRUE(std::holds_alternative<ShaderData>(compiled));
        const auto& shader  = std::get<ShaderData>(compiled);
        auto        encoded = EncodeShaderProduct(shader.Code, {}, {}, {}, shader.Groups);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        const auto& bytes = std::get<std::vector<std::byte>>(encoded);
        // Corrupt only group metadata; the valid SPIR-V still contains the original member.
        for (const auto& name : {"u_Albedo", "NotKnown"})
        {
            auto corrupt = bytes;
            std::memcpy(corrupt.data() + corrupt.size() - 12, name, 8);
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(corrupt.data(), corrupt.size())));
        }
        for (size_t size = bytes.size() - 12; size < bytes.size(); ++size)
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(bytes.data(), size)));
    }
    TEST(MaterialParameterGroups, StandardGroupsCoverEveryEditableMemberInAuthoredOrder)
    {
        const std::vector<MaterialParameterGroup> expected {
            {"Surface", {"BaseColor", "BaseColorMap", "Metallic", "Roughness", "MetallicRoughnessMap"}},
            {"Normal", {"NormalMap", "NormalStrength"}},
            {"Occlusion", {"OcclusionMap", "OcclusionStrength"}},
            {"Emission", {"EmissiveColor", "EmissiveIntensity", "EmissiveMap"}},
            {"UV", {"UVTiling", "UVOffset"}},
            {"Advanced", {"VertexColorStrength"}}};
        auto described = DescribeMaterial(nullptr);
        ASSERT_TRUE(std::holds_alternative<MaterialSchema>(described));
        const auto& schema = std::get<MaterialSchema>(described);
        ASSERT_EQ(schema.Groups.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(schema.Groups[i].Name, expected[i].Name);
            EXPECT_EQ(schema.Groups[i].Members, expected[i].Members);
        }
    }
    TEST(MaterialParameterGroups, SchemaRejectsCallerSuppliedUnknownOrRepeatedMetadata)
    {
        // DescribeMaterial also accepts CPU ShaderData; bypassing the codec must not skip validation.
        ShaderData shader;
        shader.Code   = CompileTestMaterialShader();
        shader.Groups = {{"Surface", {"Tint"}}, {"Detail", {"Tint"}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DescribeMaterial(&shader)));
        shader.Groups = {{"Surface", {"Missing"}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DescribeMaterial(&shader)));
    }
} // namespace
