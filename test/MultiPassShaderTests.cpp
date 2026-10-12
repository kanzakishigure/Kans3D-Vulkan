#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ShaderProgramSource.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Runtime/MaterialShaderInterface.h"
#include "Kans3D/Asset/Storage/ProductFiles.h"
#include "MaterialShaderTestUtilities.h"
#include "MultiPassTestUtilities.h"
namespace
{
    using namespace Kans;
    TEST(MultiPassShader, ImportAndCodecPreserveNamesTagsStagesAndEveryStateField)
    {
        // Bug: parsed passes or individual render-state fields are dropped by the codec.
        auto manifest = MultiPassText("Outline.shaderprog");
        manifest += "pass Shadow\nvertex=Surface.vert\nfragment=Surface.frag\ntag=ShadowCaster\n"
                    "cull=none\ndepth_test=off\ndepth_compare=greater_equal\ndepth_write=on\nblend=on\n"
                    "src_color=src_alpha\ndst_color=one_minus_src_alpha\nsrc_alpha=dst_alpha\n"
                    "dst_alpha=one_minus_dst_alpha\ncolor_write=ra\nendpass\n";
        auto imported = CompileEmbeddedShaderProgram(manifest, MultiPassSources());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(imported)) << std::get<AssetError>(imported).Message;
        const auto& data    = std::get<ShaderData>(imported);
        auto        encoded = EncodeShaderProduct(data.Code, data.ParameterDefaults, {}, {}, {}, data.Passes);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        const auto& bytes = std::get<std::vector<std::byte>>(encoded);
        EXPECT_EQ(bytes[4], std::byte {6});
        auto decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        const auto& shader = std::get<ShaderData>(decoded);
        ASSERT_EQ(shader.Passes.size(), 2u);
        EXPECT_EQ(shader.Code.Modules[0].Words, data.Code.Modules[0].Words);
        EXPECT_EQ(shader.Passes[0].Name, "Hull");
        EXPECT_EQ(shader.Passes[0].Tag, "Outline");
        EXPECT_EQ(shader.Passes[0].State.Cull, ShaderCullMode::Front);
        EXPECT_TRUE(shader.Passes[0].State.DepthTest);
        EXPECT_FALSE(shader.Passes[0].State.DepthWrite);
        EXPECT_EQ(shader.Passes[0].State.DepthCompare, ShaderDepthCompare::LessEqual);
        EXPECT_FALSE(shader.Passes[0].State.Blend);
        EXPECT_EQ(shader.Passes[0].State.ColorWriteMask, 7u);
        const auto& pass = shader.Passes[1];
        EXPECT_EQ(pass.Name, "Shadow");
        EXPECT_EQ(pass.Tag, "ShadowCaster");
        EXPECT_EQ(pass.State.Cull, ShaderCullMode::None);
        EXPECT_FALSE(pass.State.DepthTest);
        EXPECT_TRUE(pass.State.DepthWrite);
        EXPECT_EQ(pass.State.DepthCompare, ShaderDepthCompare::GreaterEqual);
        EXPECT_TRUE(pass.State.Blend);
        EXPECT_EQ(pass.State.SourceColor, ShaderBlendFactor::SourceAlpha);
        EXPECT_EQ(pass.State.DestinationColor, ShaderBlendFactor::OneMinusSourceAlpha);
        EXPECT_EQ(pass.State.SourceAlpha, ShaderBlendFactor::DestinationAlpha);
        EXPECT_EQ(pass.State.DestinationAlpha, ShaderBlendFactor::OneMinusDestinationAlpha);
        EXPECT_EQ(pass.State.ColorWriteMask, 9u);
        ASSERT_EQ(pass.Code.Modules.size(), 2u);
        ASSERT_EQ(pass.Reflection.size(), 2u);
        for (size_t i = 0; i < 2; ++i)
        {
            EXPECT_EQ(pass.Code.Modules[i].Stage, data.Passes[1].Code.Modules[i].Stage);
            EXPECT_EQ(pass.Code.Modules[i].EntryPoint, "main");
            EXPECT_EQ(pass.Code.Modules[i].Words, data.Passes[1].Code.Modules[i].Words);
        }
    }
    TEST(MultiPassShader, PublishedVersionFiveFixtureLoadsAsMainOnly)
    {
        // Bug: the version bump rejects real bytes published by the original v5 codec.
        auto file = ReadProductFile(std::filesystem::path(KANS_TEST_MULTIPASS_DIR) / "single-pass-v5.kshd");
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(file));
        const auto& bytes = std::get<std::vector<std::byte>>(file);
        ASSERT_EQ(bytes[4], std::byte {5});
        auto decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        const auto& data = std::get<ShaderData>(decoded);
        ASSERT_EQ(data.Code.Modules.size(), 2u);
        EXPECT_EQ(data.Code.Modules[0].Stage, ShaderProductStage::Vertex);
        EXPECT_EQ(data.Code.Modules[1].Stage, ShaderProductStage::Fragment);
        EXPECT_TRUE(data.Passes.empty());
        EXPECT_EQ(data.ParameterDefaults.at("Strength"), (std::vector<double> {1}));
        EXPECT_EQ(data.ParameterDefaults.at("Tint"), (std::vector<double> {1, .5, .25, 1}));
        EXPECT_TRUE(std::holds_alternative<std::monostate>(ValidateMaterialShaderInterface(data.Reflection)));
    }
    TEST(MultiPassShader, ParserRejectsMalformedDuplicateNestedAndIncompletePasses)
    {
        const std::string main = "KansShaderProgram 1\nvertex=Surface.vert\nfragment=Surface.frag\n";
        for (const auto& declaration : {"endpass\n",
                                        "pass Hull\n",
                                        "pass Hull\ntag=Outline\nendpass\n",
                                        "pass Hull\npass Nested\n",
                                        "pass Main\n",
                                        "pass Hull\nunknown=on\nendpass\n",
                                        "pass Hull\ncull=sideways\n",
                                        "pass Hull\ndepth_test=true\n",
                                        "pass Hull\ncolor_write=rr\n",
                                        "pass Hull\ncull=front\ncull=back\n",
                                        "pass Hull\nvertex=../escape.vert\n",
                                        "pass Hull\ntag=Bad Tag\n"})
        {
            SCOPED_TRACE(declaration);
            EXPECT_TRUE(std::holds_alternative<AssetError>(ParseShaderProgramSource(main + declaration)));
        }
        EXPECT_TRUE(std::holds_alternative<AssetError>(
            ParseShaderProgramSource(MultiPassText("Outline.shaderprog") +
                                     "pass Hull\nvertex=Surface.vert\nfragment=Surface.frag\ntag=Outline\nendpass\n")));
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseShaderProgramSource(
            "KansShaderProgram 1\ncompute=c.comp\npass Hull\nvertex=v.vert\nfragment=f.frag\ntag=Outline\nendpass\n")));
    }
    TEST(MultiPassShader, ImportRejectsChangedMaterialBindingsNamesTypesOffsetsAndSamplers)
    {
        const auto manifest = MultiPassText("Outline.shaderprog");
        for (const auto& parameters :
             {"layout(set=1,binding=2,std140) uniform Parameters {vec4 Tint;float OutlineWidth;} p;",
              "layout(set=1,binding=0,std140) uniform Parameters {ivec4 Tint;float OutlineWidth;} p;",
              "layout(set=1,binding=0,std140) uniform Parameters {float OutlineWidth;vec4 Tint;} p;",
              "layout(set=1,binding=0,std140) uniform Parameters {vec4 Renamed;float OutlineWidth;} p;",
              "layout(set=1,binding=0,std140) uniform Different {vec4 Tint;float OutlineWidth;} p;",
              "layout(set=1,binding=0,std140) uniform Parameters {vec4 Tint;float OutlineWidth;} p;\n"
              "layout(set=1,binding=1) uniform sampler2D Extra;"})
        {
            auto  sources = MultiPassSources();
            auto& hull    = sources.at("Hull.vert");
            hull.replace(
                hull.find("#include \"Parameters.glsl\""), std::strlen("#include \"Parameters.glsl\""), parameters);
            auto imported = CompileEmbeddedShaderProgram(manifest, sources);
            ASSERT_TRUE(std::holds_alternative<AssetError>(imported));
            const auto& error = std::get<AssetError>(imported);
            EXPECT_NE(error.Message.find("pass 'Hull'"), std::string::npos) << error.Message;
            EXPECT_NE(error.Message.find("set 1 binding"), std::string::npos) << error.Message;
        }
        // Existing sampler changed to a different name or descriptor count is incompatible too.
        ShaderData shader;
        shader.Code = CompileTestMaterialShader();
        for (auto replacement : {"u_Renamed", "u_Albedo[2]"})
        {
            auto fragment = std::string(TestMaterialFragment);
            fragment.replace(fragment.find("u_Albedo;"), std::strlen("u_Albedo"), replacement);
            if (std::string(replacement) == "u_Renamed")
                fragment.replace(
                    fragment.find("texture(u_Albedo,"), std::strlen("texture(u_Albedo,"), "texture(u_Renamed,");
            if (std::string(replacement).find('[') != std::string::npos)
                fragment.replace(
                    fragment.find("texture(u_Albedo,"), std::strlen("texture(u_Albedo,"), "texture(u_Albedo[0],");
            ShaderPass pass {"Hull", "Outline", CompileTestMaterialShader(TestMaterialVertex, fragment), {}, {}};
            EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeShaderProduct(shader.Code, {}, {}, {}, {}, {pass})));
        }
    }
    TEST(MultiPassShader, StrictMaterialSubsetImportsButChangedEngineAndVertexContractsFail)
    {
        auto  sources = MultiPassSources();
        auto& hull    = sources.at("Hull.vert");
        hull.replace(hull.find("#include \"Parameters.glsl\""),
                     std::strlen("#include \"Parameters.glsl\""),
                     "layout(set=1,binding=0,std140) uniform Parameters {layout(offset=16) float OutlineWidth;} p;");
        auto subset = CompileEmbeddedShaderProgram(MultiPassText("Outline.shaderprog"), sources);
        ASSERT_TRUE(std::holds_alternative<ShaderData>(subset));
        auto resources = MaterialShaderResources(std::get<ShaderData>(subset).Passes[0].Reflection);
        ASSERT_EQ(resources.size(), 1u);
        ASSERT_EQ(resources[0].Members.size(), 1u);
        EXPECT_EQ(resources[0].Members[0].Offset, 16u);
        for (const auto& source : {"#version 450\n#include <Kans3D/Engine.glsl>\nlayout(location=0) in vec2 Bad;void "
                                   "main(){gl_Position=vec4(Bad,0,1);}",
                                   "#version 450\nlayout(set=0,binding=0) uniform Wrong {mat4 VP;} frame;void "
                                   "main(){gl_Position=frame.VP*vec4(1);}",
                                   "#version 450\nvoid main(){gl_Position=vec4(1);}"})
        {
            auto invalid         = MultiPassSources();
            invalid["Hull.vert"] = source;
            auto imported        = CompileEmbeddedShaderProgram(MultiPassText("Outline.shaderprog"), invalid);
            ASSERT_TRUE(std::holds_alternative<AssetError>(imported));
            EXPECT_NE(std::get<AssetError>(imported).Message.find("Hull"), std::string::npos);
        }
        auto legacy = MultiPassSources();
        auto engine = *FindEngineShaderInclude("Kans3D/Engine.glsl");
        engine.erase(engine.find("    float DebugView;"), std::strlen("    float DebugView;\n"));
        auto& legacyVertex = legacy["Hull.vert"];
        legacyVertex.replace(
            legacyVertex.find("#include <Kans3D/Engine.glsl>"), std::strlen("#include <Kans3D/Engine.glsl>"), engine);
        auto legacyResult = CompileEmbeddedShaderProgram(MultiPassText("Outline.shaderprog"), legacy);
        ASSERT_TRUE(std::holds_alternative<AssetError>(legacyResult));
        EXPECT_NE(std::get<AssetError>(legacyResult).Message.find("set 0 contract"), std::string::npos);
    }
    TEST(MultiPassShader, DiscoveryIncludesExtraStagesAndTheirIncludesAndRequiresDeclaredInputs)
    {
        // Bug: discovery compiles only the top-level stages, leaving hull inputs unpublished.
        ImportContext context;
        context.AssetRoot  = std::filesystem::path(KANS_TEST_MULTIPASS_DIR);
        context.SourceFile = context.AssetRoot / "Outline.shaderprog";
        auto discovered    = ShaderImporter {}.DiscoverInputs(context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(discovered));
        auto files = std::get<ImportInputs>(discovered).SourceFiles;
        ASSERT_EQ(files.size(), 5u);
        for (const auto& name : {"Surface.vert", "Surface.frag", "Hull.vert", "Hull.frag", "Parameters.glsl"})
            EXPECT_NE(std::find(files.begin(), files.end(), context.AssetRoot / name), files.end());
        context.SourceID = SourceAssetID::Generate();
        context.Target   = "vulkan12";
        context.StagingDirectory =
            std::filesystem::temp_directory_path() / ("Kans-MultiPass-" + context.SourceID.ToString());
        for (const auto& file : files)
            context.SourceInputs.emplace(file, SourceAssetID::Generate());
        ASSERT_TRUE(std::filesystem::create_directory(context.StagingDirectory));
        struct Cleanup
        {
            std::filesystem::path Path;
            ~Cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove_all(Path, ignored);
            }
        } cleanup {context.StagingDirectory};
        auto published = ShaderImporter {}.Import(context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(published));
        const auto& products = std::get<ImportResult>(published).Products;
        ASSERT_EQ(products.size(), 1u);
        auto stored = ReadProductFile(context.StagingDirectory / products[0].StagedFile);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(stored));
        const auto& bytes   = std::get<std::vector<std::byte>>(stored);
        auto        decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        const auto& shader = std::get<ShaderData>(decoded);
        ASSERT_EQ(shader.Passes.size(), 1u);
        EXPECT_EQ(shader.Passes[0].Tag, "Outline");
        EXPECT_EQ(shader.Passes[0].State.Cull, ShaderCullMode::Front);
        EXPECT_EQ(shader.ParameterDefaults.at("OutlineWidth"), (std::vector<double> {.08}));
        context.SourceInputs.erase(context.AssetRoot / "Hull.frag");
        auto imported = ShaderImporter {}.Import(context);
        ASSERT_TRUE(std::holds_alternative<AssetError>(imported));
        EXPECT_NE(std::get<AssetError>(imported).Message.find("Hull.frag"), std::string::npos);
    }
    TEST(MultiPassShader, DecoderRejectsTruncatedStateInvalidEnumsAndIncompatibleBytecode)
    {
        auto shader = MultiPassShader();
        auto bytes  = std::get<std::vector<std::byte>>(EncodeShaderProduct(shader.Code, {}, {}, {}, {}, shader.Passes));
        // Locate the appended record via a separately encoded main-only product, not magic byte scanning.
        const auto main        = std::get<std::vector<std::byte>>(EncodeShaderProduct(shader.Code));
        const auto stateOffset = main.size() + 4 + 4 + 4 + 7; // length+Hull, length+Outline
        ASSERT_LT(stateOffset + 44, bytes.size());
        for (size_t length = main.size() - 1; length < bytes.size(); ++length)
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(bytes.data(), length))) << length;
        for (auto offset : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u})
        {
            auto corrupt                      = bytes;
            corrupt[stateOffset + offset * 4] = std::byte {255};
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(corrupt.data(), corrupt.size())))
                << offset;
        }
        // Encode a valid differently named member first, then transplant its v5 code into the valid pass envelope.
        auto badSource = MultiPassSources()["Parameters.glsl"];
        badSource.replace(badSource.find("vec4 Tint"), 9, "vec4 Evil");
        auto vertex = MultiPassSources()["Hull.vert"];
        vertex.replace(
            vertex.find("#include \"Parameters.glsl\""), std::strlen("#include \"Parameters.glsl\""), badSource);
        auto badCode = CompileTestMaterialShader(vertex, MultiPassSources()["Hull.frag"]);
        auto bad     = std::get<std::vector<std::byte>>(EncodeShaderProduct(badCode));
        bad.resize(bad.size() - 4);
        bad[4] = std::byte {5};
        bytes.resize(stateOffset + 40);
        const auto length = uint32_t(bad.size());
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(std::byte((length >> (8 * i)) & 255));
        bytes.insert(bytes.end(), bad.begin(), bad.end());
        auto decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<AssetError>(decoded));
        EXPECT_NE(std::get<AssetError>(decoded).Message.find("subset"), std::string::npos);
    }
} // namespace
