#include <cstring>
#include <fstream>
#include <sstream>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Asset/Importer/ShaderProgramSource.h"
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "MaterialShaderTestUtilities.h"

namespace
{
    using namespace Kans;

    std::variant<MaterialSource, AssetError> Parse(const std::string& text)
    {
        return ParseMaterialSource(reinterpret_cast<const std::byte*>(text.data()), text.size());
    }

    MaterialSource ParseOk(const std::string& text)
    {
        auto parsed = Parse(text);
        EXPECT_TRUE(std::holds_alternative<MaterialSource>(parsed)) << std::get<AssetError>(parsed).Message;
        return std::holds_alternative<MaterialSource>(parsed) ? std::get<MaterialSource>(parsed) : MaterialSource {};
    }

    float FloatAt(const std::vector<std::byte>& bytes, size_t offset)
    {
        float value = 0;
        std::memcpy(&value, bytes.data() + offset, 4);
        return value;
    }

    class MaterialSourceTest : public testing::Test
    {
    protected:
        void SetUp() override { Shader.Code = CompileTestMaterialShader(); }

        ShaderData Shader;
        AssetID    ShaderID {SourceAssetID::Generate(), 1};
        AssetID    TextureID {SourceAssetID::Generate(), 1};
    };

    TEST_F(MaterialSourceTest, ParsesNamedValuesAndSerializesRoundTrip)
    {
        const auto source = ParseOk("magic: KansMaterial\nversion: 2\nname: Toon\nshader: " + ShaderID.ToString() +
                                    "\nparameters:\n  Tint: [1, 0.5, 0.25, 1]\n  Strength: 0.1\n  Steps: 3\n"
                                    "  Offsets: [[0, 1], [2, 3]]\n  Flag: true\ntextures:\n  u_Albedo: " +
                                    TextureID.ToString() + "\n");
        EXPECT_EQ(source.Name, "Toon");
        EXPECT_EQ(source.Shader, ShaderID);
        EXPECT_EQ(source.Parameters.at("Tint"), (std::vector<double> {1, 0.5, 0.25, 1}));
        EXPECT_EQ(source.Parameters.at("Offsets"), (std::vector<double> {0, 1, 2, 3}));
        EXPECT_EQ(source.Parameters.at("Flag"), (std::vector<double> {1}));
        EXPECT_EQ(source.Textures.at("u_Albedo"), TextureID);

        const auto text = SerializeMaterialSource(source);
        // Floats are written in their shortest exact form.
        EXPECT_NE(text.find("Strength: 0.1\n"), std::string::npos) << text;
        auto again = ParseOk(text);
        EXPECT_EQ(again.Name, source.Name);
        EXPECT_EQ(again.Shader, source.Shader);
        EXPECT_EQ(again.Parameters, source.Parameters);
        EXPECT_EQ(again.Textures, source.Textures);

        // The engine PBR material has an empty shader.
        EXPECT_FALSE(ParseOk("magic: KansMaterial\nversion: 2\nname: P\nshader: ''\n").Shader.IsValid());
        EXPECT_FALSE(ParseOk(SerializeMaterialSource({})).Shader.IsValid());
    }

    TEST_F(MaterialSourceTest, RejectsMalformedSources)
    {
        for (const std::string text : {
                 "",
                 "magic: Other\nversion: 2\n",
                 "magic: KansMaterial\nversion: 2\nunknown: 1\n",
                 "magic: KansMaterial\nversion: 2\nshader: not-an-id\n",
                 "magic: KansMaterial\nversion: 2\nparameters: [1, 2]\n",
                 "magic: KansMaterial\nversion: 2\nparameters: {A: text}\n",
                 "magic: KansMaterial\nversion: 2\nparameters: {A: [[[1]]]}\n",
                 "magic: KansMaterial\nversion: 2\ntextures: {A: broken}\n",
             })
            EXPECT_TRUE(std::holds_alternative<AssetError>(Parse(text))) << text;
        auto old = Parse("magic: KansMaterial\nversion: 1\n");
        ASSERT_TRUE(std::holds_alternative<AssetError>(old));
        EXPECT_EQ(std::get<AssetError>(old).Code, AssetErrorCode::UnsupportedVersion);
    }

    TEST_F(MaterialSourceTest, ShaderSchemaListsMaterialParametersAndSamplers)
    {
        auto described = DescribeMaterial(&Shader);
        ASSERT_TRUE(std::holds_alternative<MaterialSchema>(described)) << std::get<AssetError>(described).Message;
        const auto& schema = std::get<MaterialSchema>(described);
        ASSERT_EQ(schema.Parameters.size(), 4u);
        EXPECT_EQ(schema.Parameters[0].Name, "Tint");
        EXPECT_EQ(schema.Parameters[0].ValueCount(), 4u);
        EXPECT_EQ(schema.Parameters[2].Type, ShaderUniformType::Signed);
        EXPECT_EQ(schema.Parameters[3].Name, "Offsets");
        EXPECT_EQ(schema.Parameters[3].ValueCount(), 4u);
        EXPECT_EQ(schema.Textures, (std::vector<std::string> {"u_Albedo"}));

        auto pbr = DescribeMaterial(nullptr);
        ASSERT_TRUE(std::holds_alternative<MaterialSchema>(pbr));
        EXPECT_EQ(std::get<MaterialSchema>(pbr).Parameters.size(), 3u);
        EXPECT_EQ(std::get<MaterialSchema>(pbr).Textures.size(), 5u);
    }

    TEST_F(MaterialSourceTest, ShaderMaterialResolvesToAPackedRecipe)
    {
        MaterialSource source;
        source.Name                   = "Toon";
        source.Shader                 = ShaderID;
        source.Parameters["Tint"]     = {1, 0.5, 0.25, 1};
        source.Parameters["Steps"]    = {3};
        source.Parameters["Offsets"]  = {0, 1, 2, 3};
        source.Textures["u_Albedo"]   = TextureID;
        auto resolved                 = ResolveMaterialSource(source, &Shader);
        ASSERT_TRUE(std::holds_alternative<MaterialData>(resolved)) << std::get<AssetError>(resolved).Message;
        const auto& data = std::get<MaterialData>(resolved);
        EXPECT_EQ(data.Shader, ShaderID);
        ASSERT_EQ(data.Textures.size(), 1u);
        EXPECT_EQ(data.Textures[0].Texture, TextureID);
        ASSERT_TRUE(data.Bindings);
        ASSERT_EQ(data.Bindings->Textures.size(), 1u);
        EXPECT_EQ(data.Bindings->Textures[0].Set, MaterialDescriptorSet);
        EXPECT_EQ(data.Bindings->Textures[0].Binding, 1u);

        auto packed = PackMaterialUniforms(data, Shader.Code, *data.Bindings);
        ASSERT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(packed));
        const auto& bytes = std::get<std::vector<MaterialUniformBlock>>(packed).at(0).Bytes;
        // std140: Tint 0, Strength 16 (unset: zero), Steps 20, Offsets 32 with a 16-byte stride.
        EXPECT_EQ(FloatAt(bytes, 4), 0.5f);
        EXPECT_EQ(FloatAt(bytes, 16), 0.0f);
        int32_t steps = 0;
        std::memcpy(&steps, bytes.data() + 20, 4);
        EXPECT_EQ(steps, 3);
        EXPECT_EQ(FloatAt(bytes, 48), 2.0f);
        EXPECT_EQ(FloatAt(bytes, 52), 3.0f);
    }

    TEST_F(MaterialSourceTest, ResolveReportsUnknownNamesShapesAndTypes)
    {
        MaterialSource source;
        source.Shader  = ShaderID;
        const auto Error = [&](const MaterialSource& value) {
            auto resolved = ResolveMaterialSource(value, &Shader);
            return std::holds_alternative<AssetError>(resolved) ? std::get<AssetError>(resolved).Message : "";
        };
        auto unknown                  = source;
        unknown.Parameters["Missing"] = {1};
        EXPECT_NE(Error(unknown).find("Missing"), std::string::npos);
        auto shape               = source;
        shape.Parameters["Tint"] = {1, 2};
        EXPECT_NE(Error(shape).find("needs 4 values"), std::string::npos);
        auto type                 = source;
        type.Parameters["Steps"] = {1.5};
        EXPECT_NE(Error(type).find("wrong type"), std::string::npos);
        auto texture                 = source;
        texture.Textures["u_Other"] = TextureID;
        EXPECT_NE(Error(texture).find("u_Other"), std::string::npos);
        // The Shader must match the source.
        EXPECT_TRUE(std::holds_alternative<AssetError>(ResolveMaterialSource(source, nullptr)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(ResolveMaterialSource(MaterialSource {}, &Shader)));
    }

    TEST_F(MaterialSourceTest, PbrMaterialMapsNamedParametersAndTextureSlots)
    {
        MaterialSource source;
        source.Name                    = "Metal";
        source.Parameters["metallic"]  = {0.75};
        source.Textures["normal"]      = TextureID;
        auto resolved                  = ResolveMaterialSource(source, nullptr);
        ASSERT_TRUE(std::holds_alternative<MaterialData>(resolved)) << std::get<AssetError>(resolved).Message;
        const auto& data = std::get<MaterialData>(resolved);
        EXPECT_FALSE(data.Shader.IsValid());
        EXPECT_FALSE(data.Bindings);
        EXPECT_EQ(data.Metallic, 0.75f);
        EXPECT_EQ(data.Roughness, 1.0f);
        EXPECT_EQ(data.BaseColor, (std::array<float, 4> {1, 1, 1, 1}));
        ASSERT_EQ(data.Textures.size(), 1u);
        EXPECT_EQ(data.Textures[0].Semantic, uint32_t(MaterialTextureSemantic::Normal));

        source.Textures["albedo"] = TextureID;
        EXPECT_TRUE(std::holds_alternative<AssetError>(ResolveMaterialSource(source, nullptr)));
    }

    TEST_F(MaterialSourceTest, ShaderDefaultsFillUnsetParameters)
    {
        Shader.ParameterDefaults = {{"Tint", {0.5, 0.5, 0.5, 1}}, {"Strength", {2}}};
        auto described           = DescribeMaterial(&Shader);
        ASSERT_TRUE(std::holds_alternative<MaterialSchema>(described)) << std::get<AssetError>(described).Message;
        EXPECT_EQ(std::get<MaterialSchema>(described).Defaults.at("Strength"), (std::vector<double> {2}));

        MaterialSource source;
        source.Shader               = ShaderID;
        source.Parameters["Tint"]   = {1, 0, 0, 1};
        auto resolved               = ResolveMaterialSource(source, &Shader);
        ASSERT_TRUE(std::holds_alternative<MaterialData>(resolved)) << std::get<AssetError>(resolved).Message;
        const auto& data   = std::get<MaterialData>(resolved);
        auto        packed = PackMaterialUniforms(data, Shader.Code, *data.Bindings);
        ASSERT_TRUE(std::holds_alternative<std::vector<MaterialUniformBlock>>(packed));
        const auto& bytes = std::get<std::vector<MaterialUniformBlock>>(packed).at(0).Bytes;
        // The source wins over the default; the unset Strength takes its default.
        EXPECT_EQ(FloatAt(bytes, 0), 1.0f);
        EXPECT_EQ(FloatAt(bytes, 16), 2.0f);

        // Defaults must name a parameter with the right number of values.
        Shader.ParameterDefaults = {{"Missing", {1}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DescribeMaterial(&Shader)));
        Shader.ParameterDefaults = {{"Tint", {1}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DescribeMaterial(&Shader)));
        Shader.ParameterDefaults = {{"Steps", {0.5}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(ResolveMaterialSource(source, &Shader)));
    }

    TEST_F(MaterialSourceTest, ShaderHintsAttachToParametersAndAreChecked)
    {
        using Kind          = ShaderParameterHint::Kind;
        Shader.ParameterHints = {{"Tint", {Kind::Color}}, {"Steps", {Kind::Toggle}}, {"Strength", {Kind::Range, 0, 4}}};
        auto described        = DescribeMaterial(&Shader);
        ASSERT_TRUE(std::holds_alternative<MaterialSchema>(described)) << std::get<AssetError>(described).Message;
        const auto& parameters = std::get<MaterialSchema>(described).Parameters;
        EXPECT_EQ(parameters[0].Hint->Type, Kind::Color);
        EXPECT_EQ(parameters[1].Hint->Max, 4.0);
        EXPECT_EQ(parameters[2].Hint->Type, Kind::Toggle);
        EXPECT_FALSE(parameters[3].Hint);
        Shader.ParameterHints = {{"Missing", {Kind::Toggle}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DescribeMaterial(&Shader)));
        Shader.ParameterHints = {{"Strength", {Kind::Color}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DescribeMaterial(&Shader)));
        // The engine PBR parameters carry hints too.
        const auto pbr = std::get<MaterialSchema>(DescribeMaterial(nullptr));
        EXPECT_EQ(pbr.Parameters[0].Hint->Type, Kind::Color);
    }

    // The sample shaders shipped with the editor follow the material contract and
    // their declared defaults resolve.
    TEST_F(MaterialSourceTest, EditorSampleShadersAreValidMaterialShaders)
    {
        const auto read = [](const std::string& name) {
            std::ifstream     file(std::string(KANS_TEST_ASSET_DIR) + "/../shaders/" + name);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        };
        struct Sample
        {
            std::string              Name;
            size_t                   Parameters;
            std::vector<std::string> Textures;
        };
        for (const auto& sample : {Sample {"Toon/Toon", 7, {"u_Albedo", "u_Ramp"}},
                                   Sample {"ToneCharacter/ToneCharacter", 13, {"u_Diffuse", "u_LightMap", "u_RampMap"}}})
        {
            SCOPED_TRACE(sample.Name);
            ShaderData shader;
            shader.Code = CompileTestMaterialShader(read(sample.Name + ".vert"), read(sample.Name + ".frag"));
            auto defaults = ParseShaderProgramDefaults(read(sample.Name + ".shaderprog"));
            ASSERT_TRUE(std::holds_alternative<ShaderParameterDefaults>(defaults));
            shader.ParameterDefaults = std::get<ShaderParameterDefaults>(defaults);
            auto hints               = ParseShaderProgramHints(read(sample.Name + ".shaderprog"));
            ASSERT_TRUE(std::holds_alternative<ShaderParameterHints>(hints));
            shader.ParameterHints = std::get<ShaderParameterHints>(hints);
            auto described           = DescribeMaterial(&shader);
            ASSERT_TRUE(std::holds_alternative<MaterialSchema>(described)) << std::get<AssetError>(described).Message;
            const auto& schema = std::get<MaterialSchema>(described);
            EXPECT_EQ(schema.Parameters.size(), sample.Parameters);
            EXPECT_EQ(schema.Textures, sample.Textures);
            MaterialSource source;
            source.Shader = ShaderID;
            auto resolved = ResolveMaterialSource(source, &shader);
            EXPECT_TRUE(std::holds_alternative<MaterialData>(resolved)) << std::get<AssetError>(resolved).Message;
        }
    }
} // namespace
