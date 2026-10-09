#include <algorithm>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ShaderImporter.h"
#include "Kans3D/Asset/Importer/ShaderProgramSource.h"
#include "Kans3D/Asset/Loader/Loader.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Project/Project.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;
    class ShaderImporterTest : public ::testing::Test
    {
    protected:
        fs::path      Directory;
        ImportContext Context;
        SourceAssetID IncludeID = SourceAssetID::Generate();
        void          SetUp() override
        {
            Directory = fs::temp_directory_path() / ("Kans3D-ShaderImport-" + SourceAssetID::Generate().ToString());
            ASSERT_TRUE(fs::create_directory(Directory));
            fs::create_directories(Directory / "assets" / "includes");
            fs::create_directory(Directory / "stage");
            Context.SourceID         = SourceAssetID::Generate();
            Context.AssetRoot        = Directory / "assets";
            Context.SourceFile       = Context.AssetRoot / "test.vert";
            Context.StagingDirectory = Directory / "stage";
            Context.Target           = "vulkan12";
            Context.SourceInputs.emplace(Context.AssetRoot / "includes" / "color.glsl", IncludeID);
            Text(Context.AssetRoot / "includes" / "color.glsl", "vec4 position(){return vec4(1);}\n");
            Text(Context.SourceFile,
                 "#version 450\n#extension GL_GOOGLE_include_directive : require\n"
                 "#include \"includes/color.glsl\"\nvoid main(){gl_Position=position();}\n");
        }
        void TearDown() override
        {
            std::error_code ignored;
            fs::remove_all(Directory, ignored);
        }
        void Text(const fs::path& file, const std::string& text)
        {
            std::ofstream output(file, std::ios::binary);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.close();
            ASSERT_TRUE(output.good());
        }
        ImportResult Import()
        {
            auto result = ShaderImporter {}.Import(Context);
            if (const auto* error = std::get_if<AssetError>(&result))
            {
                ADD_FAILURE() << error->Message;
                return {};
            }
            return std::get<ImportResult>(std::move(result));
        }
        void Program()
        {
            Context.SourceInputs.emplace(Context.SourceFile, SourceAssetID::Generate());
            Text(Context.SourceFile,
                 "#version 450\nlayout(location=0) out vec2 uv;void main(){gl_Position=vec4(1);uv=vec2(1);}");
            Text(Context.AssetRoot / "test.frag",
                 "#version 450\nlayout(location=0) in vec2 uv;layout(location=0) out vec4 color;void "
                 "main(){color=vec4(uv,0,1);}");
            Context.SourceInputs.emplace(Context.AssetRoot / "test.frag", SourceAssetID::Generate());
            Context.SourceFile = Context.AssetRoot / "program.shaderprog";
            Text(Context.SourceFile, "KansShaderProgram 1\nfragment=test.frag\nvertex=test.vert\n");
        }
    };
    TEST_F(ShaderImporterTest, RealIncludeCompileStableIdentityAndLocationIndependentBytes)
    {
        const auto first = Import();
        ASSERT_EQ(first.Products.size(), 1u);
        EXPECT_TRUE(first.Products[0].Dependencies.empty());
        auto bytes = ReadProductFile(Context.StagingDirectory / first.Products[0].StagedFile);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(bytes));
        const auto& value   = std::get<std::vector<std::byte>>(bytes);
        auto        decoded = DecodeShaderProduct(value.data(), value.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        EXPECT_EQ(std::get<ShaderData>(decoded).Code.Modules[0].Stage, ShaderProductStage::Vertex);
        Context.PreviousLocalIDs = first.LocalIDs;
        fs::rename(Context.AssetRoot, Directory / "relocated");
        Context.AssetRoot  = Directory / "relocated";
        Context.SourceFile = Context.AssetRoot / "test.vert";
        Context.SourceInputs.clear();
        Context.SourceInputs.emplace(Context.AssetRoot / "includes" / "color.glsl", IncludeID);
        fs::remove(Context.StagingDirectory / first.Products[0].StagedFile);
        const auto second = Import();
        ASSERT_EQ(second.Products.size(), 1u);
        EXPECT_EQ(second.LocalIDs, first.LocalIDs);
        EXPECT_EQ(
            std::get<std::vector<std::byte>>(ReadProductFile(Context.StagingDirectory / second.Products[0].StagedFile)),
            value);
    }
    TEST_F(ShaderImporterTest, UndeclaredEscapingSystemAndRecursiveIncludesFailWithoutProducts)
    {
        Context.SourceInputs.clear();
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
        Context.SourceInputs.emplace(Context.AssetRoot / "includes" / "color.glsl", IncludeID);
        Text(Context.AssetRoot / "includes" / "color.glsl", "#include \"color.glsl\"\n");
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        Text(Context.AssetRoot / "includes" / "color.glsl", "#include \"../../secret.glsl\"\n");
        Text(Directory / "secret.glsl", "vec4 position(){return vec4(1);}");
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        Text(Context.SourceFile,
             "#version 450\n#extension GL_GOOGLE_include_directive : require\n#include <includes/color.glsl>\nvoid "
             "main(){}\n");
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
    }
    TEST_F(ShaderImporterTest, EngineIncludesAreBuiltInAndNotInputs)
    {
        Context.SourceInputs.clear();
        Text(Context.SourceFile,
             "#version 450\n#include <Kans3D/Engine.glsl>\n#include <Kans3D/VertexInput.glsl>\n"
             "void main(){gl_Position=u_Frame.ViewProjection*u_Object.Transform*vec4(a_Position,1);}\n");
        auto discovered = ShaderImporter {}.DiscoverInputs(Context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(discovered)) << std::get<AssetError>(discovered).Message;
        EXPECT_TRUE(std::get<ImportInputs>(discovered).SourceFiles.empty());
        EXPECT_EQ(Import().Products.size(), 1u);
        for (const auto& entry : fs::directory_iterator(Context.StagingDirectory))
            fs::remove(entry.path());
        Text(Context.SourceFile, "#version 450\n#include <Kans3D/Missing.glsl>\nvoid main(){}\n");
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
    }
    TEST_F(ShaderImporterTest, SettingsTargetMalformedTextAndStageAreRejected)
    {
        Context.Target   = "default";
        auto unsupported = ShaderImporter {}.Import(Context);
        ASSERT_TRUE(std::holds_alternative<AssetError>(unsupported));
        EXPECT_EQ(std::get<AssetError>(unsupported).Code, AssetErrorCode::NotImplemented);
        Context.Target = "vulkan12";
        Context.Settings.emplace("ignored", "true");
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        Context.Settings.clear();
        Text(Context.SourceFile, std::string("a\0b", 3));
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        Text(Context.SourceFile, std::string(1024 * 1024 + 1, 'a'));
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        for (const auto& extension : {".frag", ".comp"})
        {
            Context.SourceFile = Context.AssetRoot / (std::string("test") + extension);
            Text(Context.SourceFile,
                 extension == std::string(".comp") ?
                     "#version 450\nlayout(local_size_x=1) in;void main(){}" :
                     "#version 450\nlayout(location=0) out vec4 color;void main(){color=vec4(1);}");
            const auto result = Import();
            ASSERT_EQ(result.Products.size(), 1u);
            fs::remove(Context.StagingDirectory / result.Products[0].StagedFile);
        }
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
    }
    TEST_F(ShaderImporterTest, DiscoveryReportsNestedIncludesAndImportNeedsThemDeclared)
    {
        Text(Context.AssetRoot / "base.glsl", "vec4 position(){return vec4(1);}\n");
        Text(Context.AssetRoot / "includes" / "color.glsl", "#include \"../base.glsl\"\n");
        auto discovered = ShaderImporter {}.DiscoverInputs(Context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(discovered)) << std::get<AssetError>(discovered).Message;
        auto files = std::get<ImportInputs>(discovered).SourceFiles;
        std::sort(files.begin(), files.end());
        EXPECT_EQ(
            files,
            (std::vector<fs::path> {Context.AssetRoot / "base.glsl", Context.AssetRoot / "includes" / "color.glsl"}));
        // The nested include is not declared yet.
        auto undeclared = ShaderImporter {}.Import(Context);
        ASSERT_TRUE(std::holds_alternative<AssetError>(undeclared));
        EXPECT_EQ(std::get<AssetError>(undeclared).Code, AssetErrorCode::NotFound);
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
        Context.SourceInputs.emplace(Context.AssetRoot / "base.glsl", SourceAssetID::Generate());
        EXPECT_EQ(Import().Products.size(), 1u);
    }
    TEST_F(ShaderImporterTest, ProjectRebuildsOnIncludeChangeAndFailedCompileKeepsLastGood)
    {
        ProjectConfig config;
        config.Name          = "Shader build test";
        config.RootDirectory = Directory;
        auto opened          = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto        project  = std::get<std::unique_ptr<Project>>(std::move(opened));
        const auto& database = project->GetSourceAssetDatabase();
        const auto  source   = database.FindIDByPath(AssetPath("test.vert"));
        const auto  include  = database.FindIDByPath(AssetPath("includes/color.glsl"));
        ASSERT_TRUE(source.IsValid());
        ASSERT_TRUE(include.IsValid());
        AssetBuildOptions options;
        options.Target = "vulkan12";
        auto first     = project->BuildAsset(source, options);
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(first)) << std::get<AssetError>(first).Message;
        const auto original = std::get<AssetBuildGraphResult>(first).Root;
        auto       cached   = project->BuildAsset(source, options);
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(cached));
        EXPECT_EQ(std::get<AssetBuildGraphResult>(cached).Root.Disposition, AssetBuildDisposition::UpToDate);

        Text(Context.AssetRoot / "includes" / "color.glsl", "vec4 position(){return vec4(2);}\n");
        auto changed = project->BuildAsset(source, options);
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(changed));
        EXPECT_EQ(std::get<AssetBuildGraphResult>(changed).Root.Disposition, AssetBuildDisposition::Published);
        EXPECT_NE(std::get<AssetBuildGraphResult>(changed).Root.Fingerprint, original.Fingerprint);
        const auto published = project->GetRuntimeAssetRegistry()->GetAllProducts();

        Text(Context.AssetRoot / "includes" / "color.glsl", "this is invalid GLSL");
        EXPECT_TRUE(std::holds_alternative<AssetError>(project->BuildAsset(source, options)));
        const auto kept = project->GetRuntimeAssetRegistry()->GetAllProducts();
        ASSERT_EQ(kept.size(), 1u);
        EXPECT_EQ(kept[0].Location.ContentDigest, published[0].Location.ContentDigest);

        const auto productDirectory = project->GetProductDirectory();
        project.reset();
        auto reopened = ProductStore::Open(productDirectory);
        ASSERT_TRUE(std::holds_alternative<Scope<ProductStore>>(reopened));
        const auto saved = std::get<Scope<ProductStore>>(reopened)->FindBuild(source, options.Target);
        ASSERT_TRUE(saved.has_value());
        EXPECT_EQ(saved->SourceInputs, std::vector<SourceAssetID> {include});
        ASSERT_EQ(saved->Products.size(), 1u);
        EXPECT_EQ(saved->Products[0].Metadata.Type, AssetType::Shader);
    }
    TEST_F(ShaderImporterTest, ConcurrentCompilersOwnIndependentIncludesAndProducts)
    {
        std::vector<std::future<std::variant<ImportResult, AssetError>>> jobs;
        for (size_t i = 0; i < 4; ++i)
        {
            auto context             = Context;
            context.StagingDirectory = Directory / ("parallel-" + std::to_string(i));
            fs::create_directory(context.StagingDirectory);
            jobs.push_back(std::async(std::launch::async, [context] { return ShaderImporter {}.Import(context); }));
        }
        std::vector<std::byte> expected;
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            auto result = jobs[i].get();
            ASSERT_TRUE(std::holds_alternative<ImportResult>(result));
            const auto& imported = std::get<ImportResult>(result);
            ASSERT_EQ(imported.Products.size(), 1u);
            auto bytes =
                ReadProductFile(Directory / ("parallel-" + std::to_string(i)) / imported.Products[0].StagedFile);
            ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(bytes));
            if (!i)
                expected = std::get<std::vector<std::byte>>(bytes);
            else
                EXPECT_EQ(std::get<std::vector<std::byte>>(bytes), expected);
        }
    }
    TEST_F(ShaderImporterTest, CompleteProgramCompilesCanonicalStagesIndependentOfListOrder)
    {
        Program();
        const auto result = Import();
        ASSERT_EQ(result.Products.size(), 1u);
        auto bytes = ReadProductFile(Context.StagingDirectory / result.Products[0].StagedFile);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(bytes));
        const auto& data    = std::get<std::vector<std::byte>>(bytes);
        auto        decoded = DecodeShaderProduct(data.data(), data.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        const auto& shader = std::get<ShaderData>(decoded);
        ASSERT_EQ(shader.Code.Modules.size(), 2u);
        EXPECT_EQ(shader.Code.Modules[0].Stage, ShaderProductStage::Vertex);
        EXPECT_EQ(shader.Code.Modules[1].Stage, ShaderProductStage::Fragment);
        EXPECT_EQ(shader.Reflection[1].Inputs[0].Components, 2u);
        Context.PreviousLocalIDs = result.LocalIDs;
        fs::remove(Context.StagingDirectory / result.Products[0].StagedFile);
        Text(Context.SourceFile, "KansShaderProgram 1\r\nvertex=test.vert\r\nfragment=test.frag\r\n");
        const auto reordered = Import();
        ASSERT_EQ(reordered.Products.size(), 1u);
        EXPECT_EQ(reordered.LocalIDs, result.LocalIDs);
        EXPECT_EQ(std::get<std::vector<std::byte>>(
                      ReadProductFile(Context.StagingDirectory / reordered.Products[0].StagedFile)),
                  data);
    }
    TEST(ShaderProgramSource, RejectsMalformedVersionStagesPathsAndBudgets)
    {
        for (const auto& text : {"",
                                 "KansShaderProgram 2\ncompute=a.comp",
                                 "KansShaderProgram 1\n",
                                 "KansShaderProgram 1\nvertex=a.vert",
                                 "KansShaderProgram 1\ncompute=a.comp\nvertex=b.vert",
                                 "KansShaderProgram 1\ncompute=a.comp\ncompute=b.comp",
                                 "KansShaderProgram 1\nunknown=a",
                                 "KansShaderProgram 1\ncompute=../a",
                                 "KansShaderProgram 1\ncompute=/a",
                                 "KansShaderProgram 1\ncompute=C:/a",
                                 "KansShaderProgram 1\ncompute=folder\\a"})
            EXPECT_TRUE(std::holds_alternative<AssetError>(ParseShaderProgramSource(text))) << text;
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseShaderProgramSource(std::string(65537, 'a'))));
        EXPECT_TRUE(std::holds_alternative<std::vector<ShaderStageSource>>(
            ParseShaderProgramSource("KansShaderProgram 1\ncompute=main.comp\n")));
    }
    TEST(ShaderProgramSource, ParsesParameterDefaultsCommentsAndBlankLines)
    {
        const std::string text = "KansShaderProgram 1\n# stages\nvertex=a.vert\n\nfragment=a.frag\n"
                                 "default Tint = 1 0.5 0.25 1\ndefault Flag = true\n";
        auto stages = ParseShaderProgramSource(text);
        ASSERT_TRUE(std::holds_alternative<std::vector<ShaderStageSource>>(stages));
        EXPECT_EQ(std::get<std::vector<ShaderStageSource>>(stages).size(), 2u);
        auto defaults = ParseShaderProgramDefaults(text);
        ASSERT_TRUE(std::holds_alternative<ShaderParameterDefaults>(defaults));
        EXPECT_EQ(std::get<ShaderParameterDefaults>(defaults),
                  (ShaderParameterDefaults {{"Tint", {1, 0.5, 0.25, 1}}, {"Flag", {1}}}));
        for (const char* bad : {"default Tint 1\n",
                                "default = 1\n",
                                "default A B = 1\n",
                                "default A = x\n",
                                "default A =\n",
                                "default A = 1\ndefault A = 2\n"})
            EXPECT_TRUE(std::holds_alternative<AssetError>(ParseShaderProgramDefaults(bad))) << bad;
    }
    TEST_F(ShaderImporterTest, ProgramDefaultsAreStoredInTheProduct)
    {
        Program();
        Text(Context.SourceFile,
             "KansShaderProgram 1\nfragment=test.frag\nvertex=test.vert\ndefault Strength = 0.5\n");
        const auto imported = Import();
        ASSERT_EQ(imported.Products.size(), 1u);
        auto bytes = ReadProductFile(Context.StagingDirectory / imported.Products[0].StagedFile);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(bytes));
        const auto& value   = std::get<std::vector<std::byte>>(bytes);
        auto        decoded = DecodeShaderProduct(value.data(), value.size());
        ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
        EXPECT_EQ(std::get<ShaderData>(decoded).ParameterDefaults,
                  (ShaderParameterDefaults {{"Strength", {0.5}}}));
    }
    TEST_F(ShaderImporterTest, ProgramMissingStageAndInterfaceFailureLeaveStageEmpty)
    {
        Program();
        const auto external = Context.SourceInputs.at(Context.AssetRoot / "test.frag");
        Context.SourceInputs.erase(Context.AssetRoot / "test.frag");
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
        Context.SourceInputs.emplace(Context.AssetRoot / "test.frag", external);
        Text(Context.AssetRoot / "test.frag",
             "#version 450\nlayout(location=0) in vec3 uv;layout(location=0) out vec4 color;void "
             "main(){color=vec4(uv,1);}");
        EXPECT_TRUE(std::holds_alternative<AssetError>(ShaderImporter {}.Import(Context)));
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
    }
    TEST_F(ShaderImporterTest, ProgramProjectPublicationLoadsCompleteCpuShader)
    {
        Program();
        ProjectConfig config;
        config.Name          = "Shader program test";
        config.RootDirectory = Directory;
        auto opened          = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto              project = std::get<std::unique_ptr<Project>>(std::move(opened));
        const auto&       sources = project->GetSourceAssetDatabase();
        const auto        id      = sources.FindIDByPath(AssetPath("program.shaderprog"));
        AssetBuildOptions options;
        options.Target = "vulkan12";
        auto built     = project->BuildAsset(id, options);
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(built)) << std::get<AssetError>(built).Message;
        const auto& registry = std::get<AssetBuildGraphResult>(built).Root.Registry;
        ASSERT_EQ(registry->GetAllProducts().size(), 1u);
        const auto record = registry->GetAllProducts()[0];
        auto       read   = ReadRegisteredProduct(project->GetProductDirectory(), record);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(read));
        const auto& bytes = std::get<std::vector<std::byte>>(read);
        auto        loaded =
            Loader<ShaderDataAsset> {}.Load({record.Metadata.assetID, AssetType::Shader, bytes.data(), bytes.size()});
        ASSERT_TRUE(std::holds_alternative<Ref<ShaderDataAsset>>(loaded));
        EXPECT_EQ(std::get<Ref<ShaderDataAsset>>(loaded)->GetData().Code.Modules.size(), 2u);
    }
    TEST_F(ShaderImporterTest, ProgramDiscoveryReportsStagesAndIncludesWithoutStaging)
    {
        Program();
        auto context = Context;
        context.StagingDirectory.clear();
        context.SourceInputs.clear();
        auto discovered = ShaderImporter {}.DiscoverInputs(context);
        ASSERT_TRUE(std::holds_alternative<ImportInputs>(discovered)) << std::get<AssetError>(discovered).Message;
        auto files = std::get<ImportInputs>(discovered).SourceFiles;
        std::sort(files.begin(), files.end());
        EXPECT_EQ(files, (std::vector<fs::path> {Context.AssetRoot / "test.frag", Context.AssetRoot / "test.vert"}));
        EXPECT_TRUE(fs::is_empty(Context.StagingDirectory));
        fs::remove(Context.AssetRoot / "test.frag");
        EXPECT_EQ(std::get<AssetError>(ShaderImporter {}.DiscoverInputs(context)).Code, AssetErrorCode::NotFound);
    }
} // namespace
