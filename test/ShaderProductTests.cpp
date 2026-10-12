#include <limits>
#include <future>
#include <thread>
#include <gtest/gtest.h>
#include <shaderc/shaderc.hpp>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "Kans3D/Asset/Runtime/ShaderUniformBlock.h"
#include "Kans3D/Asset/Storage/ProductStore.h"
#include "AssetTestUtilities.h"

namespace
{
    using namespace Kans;
    ShaderModuleCode Compile(ShaderProductStage stage, const std::string& source)
    {
        shaderc::Compiler       compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        const auto kind   = stage == ShaderProductStage::Vertex   ? shaderc_vertex_shader :
                            stage == ShaderProductStage::Fragment ? shaderc_fragment_shader :
                                                                    shaderc_compute_shader;
        auto       result = compiler.CompileGlslToSpv(source, kind, "fixture.glsl", options);
        EXPECT_EQ(result.GetCompilationStatus(), shaderc_compilation_status_success) << result.GetErrorMessage();
        return {stage, "main", {result.cbegin(), result.cend()}};
    }
    ShaderProgramCode Graphics()
    {
        return {{Compile(ShaderProductStage::Vertex, R"(#version 450
layout(location=0) in vec3 position;
layout(location=0) out vec2 uv;
layout(set=0,binding=0) uniform Camera { mat4 vp; } camera;
void main(){ gl_Position=camera.vp*vec4(position,1); uv=position.xy; })"),
                 Compile(ShaderProductStage::Fragment, R"(#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D image[2];
void main(){ color=texture(image[1],uv); })")}};
    }
    TEST(ShaderProduct, ParameterDefaultsRoundTripAndVersionOneHasNone)
    {
        const ShaderParameterDefaults defaults {{"Tint", {1, 0.5, 0.25, 1}}, {"Steps", {3}}};
        auto encoded = EncodeShaderProduct(Graphics(), defaults);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        auto bytes   = std::get<std::vector<std::byte>>(encoded);
        auto decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        EXPECT_EQ(std::get<ShaderData>(decoded).ParameterDefaults, defaults);
        for (size_t size = bytes.size() - 20; size < bytes.size(); ++size)
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(bytes.data(), size)));

        // Version 1 Products end after the modules (no default or hint counts).
        auto legacy = std::get<std::vector<std::byte>>(EncodeShaderProduct(Graphics()));
        legacy.resize(legacy.size() - 20);
        legacy[4] = std::byte {1};
        auto old  = DecodeShaderProduct(legacy.data(), legacy.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(old));
        EXPECT_TRUE(std::get<ShaderData>(old).ParameterDefaults.empty());

        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeShaderProduct(Graphics(), {{"Bad", {}}})));
        EXPECT_TRUE(std::holds_alternative<AssetError>(
            EncodeShaderProduct(Graphics(), {{"Bad", {std::numeric_limits<double>::infinity()}}})));
    }
    TEST(ShaderProduct, VersionsTwoThroughFourRemainReadableAfterGroupsExtension)
    {
        // The v3 reader previously rejected v2, and extending the codec must not strand old Products.
        const auto current = std::get<std::vector<std::byte>>(EncodeShaderProduct(Graphics(), {{"Tint", {0.25}}}));
        for (unsigned version : {2u, 3u, 4u})
        {
            auto bytes = current;
            bytes.resize(bytes.size() - (version == 2 ? 16 : version == 3 ? 12 : 8));
            bytes[4]           = std::byte(version);
            const auto decoded = DecodeShaderProduct(bytes.data(), bytes.size());
            ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
            EXPECT_EQ(std::get<ShaderData>(decoded).ParameterDefaults.at("Tint"), (std::vector<double> {0.25}));
            EXPECT_TRUE(std::get<ShaderData>(decoded).Groups.empty());
        }
    }
    TEST(ShaderProduct, ParameterHintsRoundTrip)
    {
        const ShaderParameterHints hints {{"Flag", {ShaderParameterHint::Kind::Toggle}},
                                          {"Tint", {ShaderParameterHint::Kind::Color}},
                                          {"Gloss", {ShaderParameterHint::Kind::Range, -1, 2}}};
        auto encoded = EncodeShaderProduct(Graphics(), {{"Gloss", {0.5}}}, hints);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        const auto& bytes   = std::get<std::vector<std::byte>>(encoded);
        auto        decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        EXPECT_EQ(std::get<ShaderData>(decoded).ParameterHints, hints);
        EXPECT_TRUE(std::holds_alternative<AssetError>(
            EncodeShaderProduct(Graphics(), {}, {{"Bad", {ShaderParameterHint::Kind::Range, 1, 1}}})));
        EXPECT_TRUE(std::holds_alternative<AssetError>(
            EncodeShaderProduct(Graphics(), {}, {{"Bad", {ShaderParameterHint::Kind(9)}}})));
    }
    TEST(ShaderProduct, UniformReflectionOwnsNestedArrayAndRowMajorLayout)
    {
        ShaderProgramCode code {{Compile(ShaderProductStage::Vertex, R"(#version 450
struct Light { vec3 color; float intensity; };
layout(std140,row_major,set=0,binding=0) uniform Material {
mat2x3 transform; Light lights[2]; float weights[3]; vec4 color; uint flags;
} material;
void main(){gl_Position=material.color+vec4(material.lights[1].color,material.weights[2]);})")}};
        auto              encoded = EncodeShaderProduct(code);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        const auto& bytes   = std::get<std::vector<std::byte>>(encoded);
        auto        decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        const auto& members = std::get<ShaderData>(decoded).Reflection[0].Bindings[0].Members;
        ASSERT_EQ(members.size(), 5u);
        EXPECT_EQ(members[0].Name, "transform");
        EXPECT_EQ(members[0].Components, 3u);
        EXPECT_EQ(members[0].Columns, 2u);
        EXPECT_TRUE(members[0].RowMajor);
        EXPECT_EQ(members[0].MatrixStride, 16u);
        EXPECT_EQ(members[0].Bytes, 48u);
        EXPECT_EQ(members[1].Offset, 48u);
        EXPECT_EQ(members[1].ArrayCount, 2u);
        EXPECT_EQ(members[1].ArrayStride, 16u);
        EXPECT_EQ(members[1].Type, ShaderUniformType::Struct);
        ASSERT_EQ(members[1].Members.size(), 2u);
        EXPECT_EQ(members[1].Members[1].Offset, 12u);
        EXPECT_EQ(members[2].ArrayCount, 3u);
        EXPECT_EQ(members[2].ArrayStride, 16u);
        EXPECT_EQ(members[3].Offset, 128u);
        EXPECT_EQ(members[4].Type, ShaderUniformType::Unsigned);
        code.Modules.clear();
        EXPECT_EQ(members[1].Members[0].Name, "color");
    }
    TEST(ShaderProduct, EqualSizedUniformTypeMismatchRejectsButMemberNamesMayDiffer)
    {
        auto vertex   = Compile(ShaderProductStage::Vertex, R"(#version 450
layout(set=0,binding=0) uniform A {vec4 value;} a;
void main(){gl_Position=a.value;})");
        auto fragment = Compile(ShaderProductStage::Fragment, R"(#version 450
layout(set=0,binding=0) uniform B {ivec4 value;} b;
layout(location=0) out vec4 color;void main(){color=vec4(b.value);})");
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeShaderProduct({{vertex, fragment}})));
        fragment = Compile(ShaderProductStage::Fragment, R"(#version 450
layout(set=0,binding=0) uniform B {vec4 renamed;} b;
layout(location=0) out vec4 color;void main(){color=b.renamed;})");
        EXPECT_TRUE(std::holds_alternative<std::vector<std::byte>>(EncodeShaderProduct({{vertex, fragment}})));
    }
    TEST(ShaderProduct, MultidimensionalUniformArraysAreExplicitlyUnsupported)
    {
        auto vertex  = Compile(ShaderProductStage::Vertex, R"(#version 450
layout(std140,set=0,binding=0) uniform A {float values[2][3];} a;
void main(){gl_Position=vec4(a.values[1][2]);})");
        auto encoded = EncodeShaderProduct({{vertex}});
        ASSERT_TRUE(std::holds_alternative<AssetError>(encoded));
        EXPECT_EQ(std::get<AssetError>(encoded).Code, AssetErrorCode::NotImplemented);
    }
    TEST(ShaderProduct, UniformNestingAndMemberBudgetsRejectOversizedReflection)
    {
        std::string source = "#version 450\nstruct S0 {vec4 value;};\n";
        for (size_t i = 1; i <= 17; ++i)
            source += "struct S" + std::to_string(i) + " {S" + std::to_string(i - 1) + " child;};\n";
        source += "layout(std140,set=0,binding=0) uniform A {S17 root;} a;void main(){gl_Position=a.root";
        for (size_t i = 0; i < 17; ++i)
            source += ".child";
        source += ".value;}";
        auto nested = EncodeShaderProduct({{Compile(ShaderProductStage::Vertex, source)}});
        ASSERT_TRUE(std::holds_alternative<AssetError>(nested));
        EXPECT_NE(std::get<AssetError>(nested).Message.find("nesting"), std::string::npos);
        source = "#version 450\nlayout(std140,set=0,binding=0) uniform A {";
        for (size_t i = 0; i < 4097; ++i)
            source += "float f" + std::to_string(i) + ";";
        source += "} a;void main(){gl_Position=vec4(a.f4096);}";
        auto count = EncodeShaderProduct({{Compile(ShaderProductStage::Vertex, source)}});
        ASSERT_TRUE(std::holds_alternative<AssetError>(count));
        EXPECT_NE(std::get<AssetError>(count).Message.find("member count"), std::string::npos);
    }
    TEST(ShaderProduct, UniformPackingUsesNestedArraysMatrixLayoutAndCanonicalWords)
    {
        ShaderProgramCode                     code {{Compile(ShaderProductStage::Vertex, R"(#version 450
struct Light {vec3 color;int flags;};
layout(std140,row_major,set=0,binding=0) uniform A {
mat2x3 matrix;vec3 vectors[2];Light lights[2];int sign;uint count;
} a;void main(){gl_Position=vec4(a.lights[1].color,1);})")}};
        const std::vector<ShaderUniformWrite> writes {{{{0, 0}}, std::vector<float> {1, 2, 3, 4, 5, 6}},
                                                      {{{1, 1}}, std::vector<float> {7, 8, 9}},
                                                      {{{2, 1}, {0, 0}}, std::vector<float> {10, 11, 12}},
                                                      {{{2, 1}, {1, 0}}, std::vector<int32_t> {-3}},
                                                      {{{3, 0}}, std::vector<int32_t> {-2}},
                                                      {{{4, 0}}, std::vector<uint32_t> {0x12345678}}};
        auto                                  packed = PackShaderUniformBlock(code, 0, 0, writes);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(packed));
        const auto& bytes = std::get<std::vector<std::byte>>(packed);
        ASSERT_EQ(bytes.size(), 120u);
        const auto word = [&](size_t offset) {
            uint32_t value = 0;
            for (size_t i = 0; i < 4; ++i)
                value |= uint32_t(std::to_integer<uint8_t>(bytes[offset + i])) << (i * 8);
            return value;
        };
        EXPECT_EQ(word(0), 0x3f800000u);
        EXPECT_EQ(word(4), 0x40800000u);
        EXPECT_EQ(word(16), 0x40000000u);
        EXPECT_EQ(word(20), 0x40a00000u);
        EXPECT_EQ(word(32), 0x40400000u);
        EXPECT_EQ(word(36), 0x40c00000u);
        EXPECT_EQ(word(64), 0x40e00000u);
        EXPECT_EQ(word(96), 0x41200000u);
        EXPECT_EQ(word(108), 0xfffffffdu);
        EXPECT_EQ(word(112), 0xfffffffeu);
        EXPECT_EQ(word(116), 0x12345678u);
        EXPECT_EQ(word(8), 0u);
        EXPECT_EQ(word(48), 0u);
        EXPECT_EQ(word(76), 0u);
        ShaderProgramCode columnCode {{Compile(ShaderProductStage::Vertex, R"(#version 450
layout(std140,set=0,binding=0) uniform A {mat2x3 matrix;} a;
void main(){gl_Position=vec4(a.matrix[0],1);})")}};
        auto columns = PackShaderUniformBlock(columnCode, 0, 0, {{{{0, 0}}, std::vector<float> {1, 2, 3, 4, 5, 6}}});
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(columns));
        const auto& columnBytes = std::get<std::vector<std::byte>>(columns);
        ASSERT_EQ(columnBytes.size(), 32u);
        EXPECT_EQ(columnBytes[12], std::byte {0});
        EXPECT_EQ(columnBytes[18], std::byte {0x80});
        EXPECT_EQ(columnBytes[19], std::byte {0x40});
        EXPECT_EQ(columnBytes[28], std::byte {0});
    }
    TEST(ShaderProduct, UniformPackingRejectsBadPathsTypesShapesOverlapAndLimits)
    {
        ShaderProgramCode        code {{Compile(ShaderProductStage::Vertex, R"(#version 450
layout(std140,set=0,binding=0) uniform A {vec4 color;float values[2];} a;
void main(){gl_Position=a.color;})")}};
        const ShaderUniformWrite valid {{{0, 0}}, std::vector<float> {1, 2, 3, 4}};
        const auto               original = PackShaderUniformBlock(code, 0, 0, {valid});
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(original));
        for (const auto& write : std::vector<ShaderUniformWrite> {
                 {{}, std::vector<float> {1}},
                 {{{9, 0}}, std::vector<float> {1}},
                 {{{1, 2}}, std::vector<float> {1}},
                 {{{0, 0}, {0, 0}}, std::vector<float> {1}},
                 {{{0, 0}}, std::vector<int32_t> {1, 2, 3, 4}},
                 {{{0, 0}}, std::vector<float> {1}},
                 {{{0, 0}}, std::vector<float> {1, 2, 3, std::numeric_limits<float>::infinity()}}})
            EXPECT_TRUE(std::holds_alternative<AssetError>(PackShaderUniformBlock(code, 0, 0, {write})));
        EXPECT_TRUE(std::holds_alternative<AssetError>(PackShaderUniformBlock(code, 0, 0, {valid, valid})));
        EXPECT_TRUE(std::holds_alternative<AssetError>(PackShaderUniformBlock(code, 0, 0, {}, 1)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(PackShaderUniformBlock(code, 0, 1, {})));
        EXPECT_EQ(std::get<std::vector<std::byte>>(PackShaderUniformBlock(code, 0, 0, {valid})),
                  std::get<std::vector<std::byte>>(original));
        code.Modules[0].Words[0] = 0;
        EXPECT_TRUE(std::holds_alternative<AssetError>(PackShaderUniformBlock(code, 0, 0, {})));
    }
    TEST(ShaderProduct, ConcurrentUniformPackingOwnsIndependentZeroedBlocks)
    {
        ShaderProgramCode code {{Compile(ShaderProductStage::Vertex, R"(#version 450
layout(set=0,binding=0) uniform A {vec4 color;} a;void main(){gl_Position=a.color;})")}};
        std::vector<std::future<std::variant<std::vector<std::byte>, AssetError>>> jobs;
        for (size_t i = 0; i < 4; ++i)
            jobs.push_back(std::async(std::launch::async, [&code] {
                return PackShaderUniformBlock(code, 0, 0, {{{{0, 0}}, std::vector<float> {1, 2, 3, 4}}});
            }));
        std::vector<std::byte> expected;
        for (auto& job : jobs)
        {
            auto result = job.get();
            ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
            if (expected.empty())
                expected = std::get<std::vector<std::byte>>(result);
            else
                EXPECT_EQ(std::get<std::vector<std::byte>>(result), expected);
        }
        const auto zeros = PackShaderUniformBlock(code, 0, 0, {});
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(zeros));
        EXPECT_EQ(std::get<std::vector<std::byte>>(zeros), std::vector<std::byte>(16, std::byte {0}));
        EXPECT_NE(expected, std::get<std::vector<std::byte>>(zeros));
    }
    TEST(ShaderProduct, MaterialShaderAndTextureLoadAsIndependentAssets)
    {
        class MapProvider final : public IAssetProvider
        {
        public:
            std::map<std::string, std::vector<std::byte>>    Bytes;
            std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord& record) const override
            {
                const auto found = Bytes.find(record.Metadata.assetID.ToString());
                if (found == Bytes.end())
                    return AssetError {AssetErrorCode::NotFound, "Absent test Product"};
                return found->second;
            }
        };
        auto          provider = CreateRef<MapProvider>();
        const AssetID shader {SourceAssetID::Generate(), 1}, texture {SourceAssetID::Generate(), 1},
            material {SourceAssetID::Generate(), 1};
        TextureData image;
        image.Width = image.Height = 1;
        image.Mips                 = {{{std::byte {1}, std::byte {2}, std::byte {3}, std::byte {255}}}};
        MaterialData data;
        data.Shader                          = shader;
        data.Textures                        = {{1, 0, texture}};
        provider->Bytes[shader.ToString()]   = std::get<std::vector<std::byte>>(EncodeShaderProduct(Graphics()));
        provider->Bytes[texture.ToString()]  = std::get<std::vector<std::byte>>(EncodeTextureProduct(image));
        provider->Bytes[material.ToString()] = std::get<std::vector<std::byte>>(EncodeMaterialProduct(data));
        const auto record                    = [&](AssetID id, AssetType type, std::vector<AssetID> dependencies) {
            ProductRecord result;
            result.Metadata.assetID       = id;
            result.Metadata.Type          = type;
            result.Metadata.Dependencies  = std::move(dependencies);
            result.Target                 = "vulkan12";
            result.Location.RelativeFile  = AssetPath(id.ToString() + ".product");
            result.Location.ByteSize      = provider->Bytes.at(id.ToString()).size();
            result.Location.ContentDigest = ComputeContentDigest(provider->Bytes.at(id.ToString()));
            return result;
        };
        const auto registry = [&] {
            return CreateRef<AssetRegistry>(std::get<AssetRegistry>(
                AssetRegistry::FromProducts({record(shader, AssetType::Shader, {}),
                                             record(texture, AssetType::Texture, {}),
                                             record(material, AssetType::Material, {shader, texture})})));
        };
        AssetManagerSpecification spec;
        spec.Target       = "vulkan12";
        auto manager      = std::make_unique<AssetManager>(provider, registry(), std::move(spec));
        auto keptMaterial = std::get<Ref<const MaterialDataAsset>>(manager->Load<MaterialDataAsset>(material));
        auto keptShader   = std::get<Ref<const ShaderDataAsset>>(manager->Load<ShaderDataAsset>(shader));
        auto keptTexture  = std::get<Ref<const TextureAsset>>(manager->Load<TextureAsset>(texture));
        // Requesting a Product as the wrong type fails instead of decoding it.
        EXPECT_TRUE(std::holds_alternative<AssetError>(manager->Load<TextureAsset>(shader)));
        manager.reset();
        provider->Bytes.clear();
        EXPECT_EQ(keptMaterial->GetData().Shader, shader);
        EXPECT_EQ(keptShader->GetData().Code.Modules.size(), 2u);
        EXPECT_EQ(keptTexture->GetData().Mips[0].Pixels[0], std::byte {1});
    }
    TEST(ShaderProduct, MaterialPublicationRequiresPublishedDependenciesAndKeepsPriorGeneration)
    {
        namespace fs    = std::filesystem;
        const auto root = fs::temp_directory_path() / ("Kans3D-MaterialShader-" + SourceAssetID::Generate().ToString());
        fs::create_directory(root);
        struct Cleanup
        {
            fs::path Path;
            ~Cleanup()
            {
                std::error_code ignored;
                fs::remove_all(Path, ignored);
            }
        } cleanup {root};
        fs::create_directory(root / "stage");
        auto          store = std::get<Scope<ProductStore>>(ProductStore::Open(root / "store"));
        ImportContext context;
        context.SourceID         = SourceAssetID::Generate();
        context.SourceFile       = root / "source";
        context.StagingDirectory = root / "stage";
        MaterialData data;
        data.Shader             = {context.SourceID, 1};
        data.Textures           = {{1, 0, {context.SourceID, 3}}};
        data.Bindings           = MaterialBindingRecipe {};
        data.Bindings->Uniforms = {
            {0, 0, 0, {}, {{{{0, 0}}, std::vector<float> {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}}}};
        data.Bindings->Textures = {{0, 1, 0, 0}, {0, 1, 1, 0}};
        TextureData image;
        image.Width = image.Height = 1;
        image.Mips                 = {{{std::byte {1}, std::byte {2}, std::byte {3}, std::byte {255}}}};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(WriteProductFile(
            root / "stage/1.kshd", std::get<std::vector<std::byte>>(EncodeShaderProduct(Graphics())))));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            WriteProductFile(root / "stage/2.kmat", std::get<std::vector<std::byte>>(EncodeMaterialProduct(data)))));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            WriteProductFile(root / "stage/3.ktex", std::get<std::vector<std::byte>>(EncodeTextureProduct(image)))));
        ImportResult imported;
        imported.LocalIDs = {{"shader:root", 1}, {"material:root", 2}, {"texture:root", 3}};
        imported.Products = {{1, AssetType::Shader, "1.kshd", {}},
                             {2, AssetType::Material, "2.kmat", {data.Shader, data.Textures[0].Texture}},
                             {3, AssetType::Texture, "3.ktex", {}}};
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(
            PublishStaged(*store, context, imported, std::string(64, 'a'), "vulkan12")));
        const auto before = store->FindBuild(context.SourceID, "vulkan12");
        ASSERT_TRUE(before.has_value());
        context.PreviousLocalIDs = imported.LocalIDs;
        // A Shader that is not published cannot be referenced.
        data.Shader = {SourceAssetID::Generate(), 1};
        data.Textures.clear();
        data.Bindings->Textures.clear();
        imported.Products[1].Dependencies = {data.Shader};
        fs::remove(root / "stage/2.kmat");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            WriteProductFile(root / "stage/2.kmat", std::get<std::vector<std::byte>>(EncodeMaterialProduct(data)))));
        EXPECT_TRUE(std::holds_alternative<AssetError>(
            PublishStaged(*store, context, imported, std::string(64, 'b'), "vulkan12")));
        EXPECT_EQ(store->FindBuild(context.SourceID, "vulkan12")->Generation, before->Generation);
        store.reset();
        auto reopened = ProductStore::Open(root / "store");
        ASSERT_TRUE(std::holds_alternative<Scope<ProductStore>>(reopened));
        auto saved = std::get<Scope<ProductStore>>(reopened)->Snapshot()->Find({context.SourceID, 2}, "vulkan12");
        ASSERT_TRUE(saved.has_value());
        EXPECT_EQ(saved->Metadata.Dependencies, (std::vector<AssetID> {{context.SourceID, 1}, {context.SourceID, 3}}));
        fs::remove_all(root / "stage");
        auto provider = LooseFileAssetProvider::Open(root / "store");
        ASSERT_TRUE(std::holds_alternative<Ref<LooseFileAssetProvider>>(provider));
        AssetManagerSpecification spec;
        spec.Target = "vulkan12";
        AssetManager runtime(std::get<Ref<LooseFileAssetProvider>>(provider),
                             std::get<Scope<ProductStore>>(reopened)->Snapshot(),
                             std::move(spec));
        auto         loaded = runtime.Load<MaterialDataAsset>({context.SourceID, 2});
        ASSERT_TRUE(std::holds_alternative<Ref<const MaterialDataAsset>>(loaded));
        const auto& kept = std::get<Ref<const MaterialDataAsset>>(loaded)->GetData();
        ASSERT_TRUE(kept.Bindings.has_value());
        EXPECT_EQ(kept.Bindings->Textures.size(), 2u);
        auto loadedShader = runtime.Load<ShaderDataAsset>(kept.Shader);
        EXPECT_TRUE(std::holds_alternative<Ref<const ShaderDataAsset>>(loadedShader));
    }
    class Provider final : public IAssetProvider
    {
    public:
        std::vector<std::byte>                           Bytes;
        std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord&) const override
        {
            if (Bytes.empty())
                return AssetError {AssetErrorCode::NotFound, "Product absent"};
            return Bytes;
        }
    };
    TEST(ShaderProduct, RealCompiledStagesRoundTripReflectAndOwnLoaderData)
    {
        auto code    = Graphics();
        auto encoded = EncodeShaderProduct(code);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        auto bytes = std::get<std::vector<std::byte>>(std::move(encoded));
        EXPECT_EQ(bytes[0], std::byte {'K'});
        EXPECT_EQ(bytes[1], std::byte {'S'});
        auto decoded = DecodeShaderProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        const auto& data = std::get<ShaderData>(decoded);
        ASSERT_EQ(data.Reflection.size(), 2u);
        ASSERT_EQ(data.Reflection[0].Bindings.size(), 1u);
        EXPECT_EQ(data.Reflection[0].Bindings[0].Kind, ShaderBindingKind::UniformBuffer);
        EXPECT_EQ(data.Reflection[0].Bindings[0].BlockBytes, 64u);
        EXPECT_EQ(data.Reflection[0].Inputs[0].Components, 3u);
        EXPECT_EQ(data.Reflection[1].Bindings[0].Kind, ShaderBindingKind::CombinedImageSampler);
        EXPECT_EQ(data.Reflection[1].Bindings[0].Binding, 1u);
        EXPECT_EQ(data.Reflection[1].Bindings[0].Count, 2u);
        EXPECT_EQ(std::get<std::vector<std::byte>>(EncodeShaderProduct(data.Code)), bytes);
        const AssetID id {SourceAssetID::Generate(), 1};
        auto          loaded = Loader<ShaderDataAsset> {}.Load({id, AssetType::Shader, bytes.data(), bytes.size()});
        ASSERT_TRUE(std::holds_alternative<Ref<ShaderDataAsset>>(loaded));
        auto asset = std::get<Ref<ShaderDataAsset>>(std::move(loaded));
        bytes.clear();
        code.Modules.clear();
        EXPECT_EQ(asset->assetID, id);
        EXPECT_EQ(asset->GetData().Code.Modules[0].Words[0], 0x07230203u);
        EXPECT_EQ(asset->GetData().Reflection[1].Outputs[0].Components, 4u);
        EXPECT_TRUE(
            std::holds_alternative<AssetError>(Loader<ShaderDataAsset> {}.Load({id, AssetType::Texture, nullptr, 0})));
    }
    TEST(ShaderProduct, TruncationCountsFlagsVersionAndMalformedSpirvReject)
    {
        auto code  = Graphics();
        auto bytes = std::get<std::vector<std::byte>>(EncodeShaderProduct(code));
        for (size_t size = 0; size < bytes.size(); ++size)
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(bytes.data(), size))) << size;
        for (size_t offset : {size_t(0), size_t(8), size_t(12), size_t(16), size_t(20), size_t(32), size_t(36)})
        {
            auto corrupt = bytes;
            corrupt[offset] ^= std::byte {255};
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(corrupt.data(), corrupt.size())))
                << offset;
        }
        auto newer = bytes;
        newer[4]   = std::byte {7};
        EXPECT_EQ(std::get<AssetError>(DecodeShaderProduct(newer.data(), newer.size())).Code,
                  AssetErrorCode::UnsupportedVersion);
        auto trailing = bytes;
        trailing.push_back(std::byte {});
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeShaderProduct(trailing.data(), trailing.size())));
        code.Modules[0].Words[5] = 0;
        EXPECT_EQ(std::get<AssetError>(EncodeShaderProduct(code)).Code, AssetErrorCode::InvalidData);
    }
    TEST(ShaderProduct, EntryStageMixAndUnsupportedResourcesAreExplicit)
    {
        auto code                  = Graphics();
        code.Modules[0].EntryPoint = "missing";
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeShaderProduct(code)));
        code.Modules[0].EntryPoint = "main";
        code.Modules[1].Stage      = ShaderProductStage::Vertex;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeShaderProduct(code)));
        auto compute = Compile(ShaderProductStage::Compute, "#version 450\nlayout(local_size_x=1) in; void main(){}");
        EXPECT_TRUE(std::holds_alternative<std::vector<std::byte>>(EncodeShaderProduct({{compute}})));
        code = Graphics();
        code.Modules.push_back(compute);
        EXPECT_EQ(std::get<AssetError>(EncodeShaderProduct(code)).Code, AssetErrorCode::InvalidData);
        auto storage = Compile(ShaderProductStage::Compute, R"(#version 450
layout(local_size_x=1) in;
layout(set=0,binding=0) buffer Values { float value; } values;
void main(){ values.value=1; })");
        EXPECT_EQ(std::get<AssetError>(EncodeShaderProduct({{storage}})).Code, AssetErrorCode::NotImplemented);
    }
    TEST(ShaderProduct, RuntimeRegistryAndAsyncLoadNeedOnlyProductBytes)
    {
        auto provider   = CreateRef<Provider>();
        provider->Bytes = std::get<std::vector<std::byte>>(EncodeShaderProduct(Graphics()));
        ProductRecord record;
        record.Metadata.assetID       = {SourceAssetID::Generate(), 1};
        record.Metadata.Type          = AssetType::Shader;
        record.Target                 = "vulkan12";
        record.Location.RelativeFile  = AssetPath("shader.kshd");
        record.Location.ByteSize      = provider->Bytes.size();
        record.Location.ContentDigest = ComputeContentDigest(provider->Bytes);
        auto registry     = CreateRef<AssetRegistry>(std::get<AssetRegistry>(AssetRegistry::FromProducts({record})));
        auto encodedIndex = std::get<std::string>(EncodeAssetRegistry(*registry));
        registry          = CreateRef<AssetRegistry>(std::get<AssetRegistry>(DecodeAssetRegistry(encodedIndex)));
        AssetManagerSpecification spec;
        spec.Target                        = "vulkan12";
        auto                       manager = std::make_unique<AssetManager>(provider, registry, std::move(spec));
        Ref<const ShaderDataAsset> view;
        const auto                 deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!view && std::chrono::steady_clock::now() < deadline)
        {
            auto status = manager->Request<ShaderDataAsset>(record.Metadata.assetID);
            ASSERT_NE(status.State, AssetLoadState::Failed);
            view = status.Asset;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(view);
        EXPECT_EQ(view->GetData().Reflection.size(), 2u);
        manager->Unload(record.Metadata.assetID);
        provider->Bytes.clear();
        EXPECT_TRUE(std::holds_alternative<AssetError>(manager->Load<ShaderDataAsset>(record.Metadata.assetID)));
        manager.reset();
        EXPECT_EQ(view->GetData().Code.Modules.size(), 2u);
    }
    TEST(ShaderProduct, CrossStageDescriptorAndInterfaceDisagreementRejects)
    {
        auto code       = Graphics();
        code.Modules[1] = Compile(ShaderProductStage::Fragment, R"(#version 450
layout(location=0) out vec4 color;
layout(set=0,binding=0) uniform Other { vec4 value; } other;
void main(){color=other.value;})");
        EXPECT_EQ(std::get<AssetError>(EncodeShaderProduct(code)).Code, AssetErrorCode::InvalidData);
        code.Modules[1] = Compile(ShaderProductStage::Fragment, R"(#version 450
layout(location=0) in vec3 uv;
layout(location=0) out vec4 color;
void main(){color=vec4(uv,1);})");
        EXPECT_EQ(std::get<AssetError>(EncodeShaderProduct(code)).Code, AssetErrorCode::InvalidData);
    }
    TEST(ShaderProduct, PublicationReopenAndMalformedReplacementKeepLastGood)
    {
        namespace fs         = std::filesystem;
        const auto workspace = fs::temp_directory_path() / ("kans-shader-" + SourceAssetID::Generate().ToString());
        fs::create_directory(workspace);
        struct Cleanup
        {
            fs::path Path;
            ~Cleanup()
            {
                std::error_code error;
                fs::remove_all(Path, error);
            }
        } cleanup {workspace};
        auto          store = std::get<Scope<ProductStore>>(ProductStore::Open(workspace / "store"));
        ImportContext context;
        context.SourceID         = SourceAssetID::Generate();
        context.SourceFile       = workspace / "source.glsl";
        context.StagingDirectory = workspace / "stage1";
        fs::create_directory(context.StagingDirectory);
        ImportResult imported;
        imported.LocalIDs.emplace("shader:root", 1);
        imported.Products.push_back({1, AssetType::Shader, "1.kshd", {}});
        auto bytes = std::get<std::vector<std::byte>>(EncodeShaderProduct(Graphics()));
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(WriteProductFile(context.StagingDirectory / "1.kshd", bytes)));
        auto published = PublishStaged(*store, context, imported, ComputeContentDigest(bytes), "vulkan12");
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(published));
        const AssetID id {context.SourceID, 1};
        auto          original = store->Snapshot()->Find(id, "vulkan12");
        ASSERT_TRUE(original);
        store.reset();
        store = std::get<Scope<ProductStore>>(ProductStore::Open(workspace / "store"));
        EXPECT_EQ(store->Snapshot()->Find(id, "vulkan12")->Location.ContentDigest, original->Location.ContentDigest);
        context.PreviousLocalIDs   = store->FindSource(context.SourceID)->LocalIDs;
        context.StagingDirectory   = workspace / "stage2";
        fs::create_directory(context.StagingDirectory);
        bytes[0] = std::byte {};
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(WriteProductFile(context.StagingDirectory / "1.kshd", bytes)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(
            PublishStaged(*store, context, imported, ComputeContentDigest(bytes), "vulkan12")));
        EXPECT_EQ(store->Snapshot()->Find(id, "vulkan12")->Location.ContentDigest, original->Location.ContentDigest);
    }
    TEST(ShaderProduct, ConcurrentValidAndMalformedDecodeKeepOwnedResultsSeparate)
    {
        const auto bytes     = std::get<std::vector<std::byte>>(EncodeShaderProduct(Graphics()));
        auto       malformed = bytes;
        malformed[0] ^= std::byte {1};
        std::vector<std::future<bool>> jobs;
        for (unsigned i = 0; i < 8; ++i)
            jobs.push_back(std::async(std::launch::async, [&] {
                for (unsigned repeat = 0; repeat < 4; ++repeat)
                {
                    auto decoded = DecodeShaderProduct(bytes.data(), bytes.size());
                    if (!std::holds_alternative<ShaderData>(decoded))
                        return false;
                    const auto& data = std::get<ShaderData>(decoded);
                    if (data.Reflection.size() != 2 || data.Reflection[1].Bindings[0].Count != 2 ||
                        !std::holds_alternative<AssetError>(DecodeShaderProduct(malformed.data(), malformed.size())))
                        return false;
                }
                return true;
            }));
        for (auto& job : jobs)
            EXPECT_TRUE(job.get());
    }
} // namespace
