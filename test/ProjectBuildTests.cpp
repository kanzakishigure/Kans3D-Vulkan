#include <fstream>
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/Asset/Storage/AssetProvider.h"
#include "Kans3D/Project/Project.h"
#include "AssetTestUtilities.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;

    // Uncompressed 32-bit TGA filled with one color.
    std::string Tga(uint8_t r, uint8_t g, uint8_t b, uint16_t width = 2, uint16_t height = 2)
    {
        std::string header(18, '\0');
        header[2]  = 2;
        header[12] = char(width & 255);
        header[13] = char(width >> 8);
        header[14] = char(height & 255);
        header[15] = char(height >> 8);
        header[16] = 32;
        header[17] = 8;
        for (int i = 0; i < width * height; ++i)
            header += {char(b), char(g), char(r), char(255)};
        return header;
    }

    class ProjectBuildTest : public ::testing::Test
    {
    protected:
        fs::path                 Root;
        std::unique_ptr<Project> Owner;

        void SetUp() override
        {
            Root = fs::temp_directory_path() / ("kans-project-" + SourceAssetID::Generate().ToString());
            fs::create_directories(Root / "assets");
        }
        void TearDown() override
        {
            Owner.reset();
            std::error_code ignored;
            fs::remove_all(Root, ignored);
        }
        void Write(const std::string& file, const std::string& text)
        {
            const auto path = Root / "assets" / fs::u8path(file);
            fs::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        }
        std::string Read(const fs::path& file)
        {
            std::ifstream input(file, std::ios::binary);
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }
        Project& Open()
        {
            Owner.reset();
            ProjectConfig config;
            config.Name          = "Build test";
            config.RootDirectory = Root;
            auto opened          = Project::Open(config);
            if (auto* error = std::get_if<AssetError>(&opened))
                ADD_FAILURE() << error->Message;
            Owner = std::get<std::unique_ptr<Project>>(std::move(opened));
            return *Owner;
        }
        SourceAssetID ID(const std::string& file) const
        {
            return Owner->GetSourceAssetDatabase().FindIDByPath(AssetPath(fs::u8path(file)));
        }
        AssetBuildGraphResult Build(const std::string& file, AssetBuildOptions options = {})
        {
            auto built = Owner->BuildAsset(ID(file), options);
            if (auto* error = std::get_if<AssetError>(&built))
            {
                ADD_FAILURE() << file << ": " << error->Message;
                return {};
            }
            return std::get<AssetBuildGraphResult>(std::move(built));
        }
        std::vector<ProductRecord> Products(const std::string& file, AssetType type = AssetType::None) const
        {
            std::vector<ProductRecord> result;
            for (const auto& record : Owner->GetRuntimeAssetRegistry()->GetAllProducts())
                if (record.Metadata.assetID.GetSourceID() == ID(file) &&
                    (type == AssetType::None || record.Metadata.Type == type))
                    result.push_back(record);
            return result;
        }
        void TexturedModel()
        {
            Write("diffuse.tga", Tga(200, 10, 10));
            Write("normal.tga", Tga(128, 128, 255));
            Write("model.mtl", "newmtl Surface\nmap_Kd diffuse.tga\nmap_Bump normal.tga\n");
            Write("model.obj", "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\no Body\nusemtl Surface\nf 1 2 3\n");
        }
    };

    TEST_F(ProjectBuildTest, BuildIsUpToDateUntilSourceSettingsOrForceChange)
    {
        Write("image.tga", Tga(1, 2, 3));
        Open();
        const auto first = Build("image.tga");
        EXPECT_EQ(first.Root.Disposition, AssetBuildDisposition::Published);
        ASSERT_EQ(Products("image.tga").size(), 1u);
        EXPECT_EQ(Build("image.tga").Root.Disposition, AssetBuildDisposition::UpToDate);

        Write("image.tga", Tga(4, 5, 6));
        EXPECT_EQ(Build("image.tga").Root.Disposition, AssetBuildDisposition::Published);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            Owner->SetSourceImportConfiguration(ID("image.tga"), {"", {{"generate_mips", "false"}}})));
        EXPECT_EQ(Build("image.tga").Root.Disposition, AssetBuildDisposition::Published);
        AssetBuildOptions force;
        force.Force = true;
        EXPECT_EQ(Build("image.tga", force).Root.Disposition, AssetBuildDisposition::Published);
        AssetBuildOptions other;
        other.Target = "other";
        EXPECT_EQ(Build("image.tga", other).Root.Disposition, AssetBuildDisposition::Published);
        EXPECT_EQ(Build("image.tga").Root.Disposition, AssetBuildDisposition::UpToDate);
        // Import work directories are cleaned up.
        EXPECT_TRUE(fs::is_empty(Owner->GetConfig().CacheDirectory / "ImportWork"));
    }

    TEST_F(ProjectBuildTest, ModelBuildsTexturesFirstAndImportsNormalMapsAsData)
    {
        TexturedModel();
        Open();
        const auto built = Build("model.obj");
        ASSERT_EQ(built.Dependencies.size(), 2u);
        EXPECT_EQ(built.Root.Source, ID("model.obj"));
        // A texture used as a bump/normal map gets non-color import settings.
        EXPECT_EQ(Owner->GetSourceAssetDatabase().FindByID(ID("normal.tga")).Import.Settings,
                  (std::map<std::string, std::string> {{"usage", "normal"}}));
        // An opaque material's color texture keeps alpha as data rather than coverage.
        EXPECT_EQ(Owner->GetSourceAssetDatabase().FindByID(ID("diffuse.tga")).Import.Settings,
                  (std::map<std::string, std::string> {{"alpha_mode", "opaque"}}));

        auto provider = LooseFileAssetProvider::Open(Owner->GetProductDirectory());
        ASSERT_TRUE(std::holds_alternative<Ref<LooseFileAssetProvider>>(provider));
        auto manager =
            MakeAssetManager(std::get<Ref<LooseFileAssetProvider>>(provider), Owner->GetRuntimeAssetRegistry());
        std::set<AssetID> textures;
        for (const auto& record : Products("model.obj", AssetType::Material))
        {
            auto material = manager->Load<MaterialDataAsset>(record.Metadata.assetID);
            ASSERT_TRUE(std::holds_alternative<Ref<const MaterialDataAsset>>(material));
            for (const auto& binding : std::get<Ref<const MaterialDataAsset>>(material)->GetData().Textures)
                textures.insert(binding.Texture);
        }
        EXPECT_EQ(textures,
                  (std::set<AssetID> {Products("diffuse.tga")[0].Metadata.assetID,
                                      Products("normal.tga")[0].Metadata.assetID}));
        manager.reset();

        // Editing the .mtl rebuilds the model; the textures stay up to date.
        Write("model.mtl", "newmtl Surface\nKd 0.5 0.5 0.5\nmap_Kd diffuse.tga\nmap_Bump normal.tga\n");
        const auto rebuilt = Build("model.obj");
        EXPECT_EQ(rebuilt.Root.Disposition, AssetBuildDisposition::Published);
        for (const auto& dependency : rebuilt.Dependencies)
            EXPECT_EQ(dependency.Disposition, AssetBuildDisposition::UpToDate);
    }

    TEST_F(ProjectBuildTest, IdentityLivesInTheSidecarAndSurvivesLosingTheCache)
    {
        TexturedModel();
        Open();
        Build("model.obj");
        std::set<AssetID> before;
        for (const auto& record : Owner->GetRuntimeAssetRegistry()->GetAllProducts())
            before.insert(record.Metadata.assetID);
        const auto sidecar = YAML::LoadFile((Root / "assets" / "model.obj.kmeta").string());
        EXPECT_EQ(sidecar["version"].as<int>(), 3);
        EXPECT_GE(sidecar["local_ids"].size(), 3u);

        Owner.reset();
        fs::remove_all(Root / "Cache");
        Open();
        EXPECT_TRUE(Owner->GetRuntimeAssetRegistry()->GetAllProducts().empty());
        Build("model.obj");
        std::set<AssetID> after;
        for (const auto& record : Owner->GetRuntimeAssetRegistry()->GetAllProducts())
            after.insert(record.Metadata.assetID);
        EXPECT_EQ(after, before);
    }

    TEST_F(ProjectBuildTest, MaterialReferencesBuildTheirProducersFirst)
    {
        Write("image.tga", Tga(9, 9, 9));
        Write("material.vert",
              "#version 450\nlayout(set=0,binding=0) uniform Parameters {vec4 color;} p;\n"
              "void main(){gl_Position=p.color;}\n");
        Open();
        AssetBuildOptions options;
        options.Target       = "vulkan12";
        const auto texture   = Build("image.tga", options);
        const auto shader    = Build("material.vert", options);
        const auto textureID = Products("image.tga")[0].Metadata.assetID;
        const auto shaderID  = Products("material.vert")[0].Metadata.assetID;
        Write("surface.material",
              "magic: KansMaterial\nversion: 1\nname: Surface\nbase_color: [0.25,0.5,0.75,1]\nmetallic: 0.4\n"
              "roughness: 0.6\nshader: " +
                  shaderID.ToString() + "\ntextures: [{semantic: 1, uv: 0, asset: '" + textureID.ToString() +
                  "'}]\nuniforms: [{set: 0, binding: 0, element: 0, parameters: [{parameter: base_color, path: "
                  "[[0,0]]}], values: []}]\nsamplers: []\n");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Owner->RefreshAssets()));
        const auto material = Build("surface.material", options);
        EXPECT_EQ(material.Dependencies.size(), 2u);
        for (const auto& dependency : material.Dependencies)
            EXPECT_EQ(dependency.Disposition, AssetBuildDisposition::UpToDate);
        const auto products = Products("surface.material");
        ASSERT_EQ(products.size(), 1u);
        EXPECT_EQ(std::set<AssetID>(products[0].Metadata.Dependencies.begin(), products[0].Metadata.Dependencies.end()),
                  (std::set<AssetID> {textureID, shaderID}));

        // Changing a referenced Product rebuilds the Material.
        Write("image.tga", Tga(10, 10, 10));
        EXPECT_EQ(Build("surface.material", options).Root.Disposition, AssetBuildDisposition::Published);
        // A reference to a Product nobody produces fails.
        Write("broken.material",
              "magic: KansMaterial\nversion: 1\nname: B\nbase_color: [1,1,1,1]\nmetallic: 0\n"
              "roughness: 1\nshader: " +
                  AssetID(SourceAssetID::Generate(), 1).ToString() + "\ntextures: []\nuniforms: []\nsamplers: []\n");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Owner->RefreshAssets()));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Owner->BuildAsset(ID("broken.material"), options)));
    }

    TEST_F(ProjectBuildTest, FailedImportKeepsThePublishedProducts)
    {
        Write("image.tga", Tga(1, 2, 3));
        Open();
        Build("image.tga");
        const auto before = Products("image.tga");
        Write("image.tga", "not an image");
        EXPECT_TRUE(std::holds_alternative<AssetError>(Owner->BuildAsset(ID("image.tga"), {})));
        const auto after = Products("image.tga");
        ASSERT_EQ(after.size(), 1u);
        EXPECT_EQ(after[0].Location.ContentDigest, before[0].Location.ContentDigest);
    }

    // Importer used to exercise coordinator behavior.
    class ScriptedImporter : public AssetImporterBase
    {
    public:
        std::function<void(const ImportContext&)>         OnImport;
        std::function<ImportInputs(const ImportContext&)> Inputs;
        uint32_t                                          GetVersion() const override { return 1; }
        std::variant<ImportInputs, AssetError>            DiscoverInputs(const ImportContext& context) const override
        {
            return Inputs ? Inputs(context) : ImportInputs {};
        }
        std::variant<ImportResult, AssetError> Import(const ImportContext& context) const override
        {
            if (OnImport)
                OnImport(context);
            auto         ids = AllocateLocalIDs(context.PreviousLocalIDs, {"texture:root"});
            ImportResult result;
            result.LocalIDs = std::get<LocalIDMap>(ids);
            return result;
        }
    };

    TEST_F(ProjectBuildTest, SourceEditedDuringImportFailsWithoutPublishing)
    {
        Write("data.custom", "first");
        Open();
        auto importer      = std::make_shared<ScriptedImporter>();
        importer->OnImport = [](const ImportContext& context) {
            std::ofstream(context.SourceFile, std::ios::app) << " edited";
        };
        ImporterRegistry importers;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(importers.Register("Scripted", {".custom"}, importer)));
        auto built = Owner->BuildAsset(ID("data.custom"), {}, importers);
        ASSERT_TRUE(std::holds_alternative<AssetError>(built));
        EXPECT_NE(std::get<AssetError>(built).Message.find("changed during import"), std::string::npos);
        EXPECT_FALSE(Owner->GetProductStore().FindBuild(ID("data.custom"), "default"));
        importer->OnImport = nullptr;
        EXPECT_TRUE(std::holds_alternative<AssetBuildGraphResult>(Owner->BuildAsset(ID("data.custom"), {}, importers)));
    }

    TEST_F(ProjectBuildTest, ReferenceCyclesAndUnknownInputsAreReported)
    {
        Write("a.custom", "a");
        Write("b.custom", "b");
        Open();
        auto importer    = std::make_shared<ScriptedImporter>();
        importer->Inputs = [](const ImportContext& context) {
            ImportInputs inputs;
            const auto   other = context.SourceFile.filename() == "a.custom" ? "b.custom" : "a.custom";
            inputs.ProductSources.push_back(context.SourceFile.parent_path() / other);
            return inputs;
        };
        ImporterRegistry importers;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(importers.Register("Scripted", {".custom"}, importer)));
        auto cycle = Owner->BuildAsset(ID("a.custom"), {}, importers);
        ASSERT_TRUE(std::holds_alternative<AssetError>(cycle));
        EXPECT_NE(std::get<AssetError>(cycle).Message.find("cycle"), std::string::npos);

        importer->Inputs = [this](const ImportContext&) {
            ImportInputs inputs;
            inputs.SourceFiles.push_back(Root / "outside.txt");
            return inputs;
        };
        EXPECT_EQ(std::get<AssetError>(Owner->BuildAsset(ID("a.custom"), {}, importers)).Code,
                  AssetErrorCode::NotFound);
    }

    TEST_F(ProjectBuildTest, MovingKeepsIdentityAndPublishedProducts)
    {
        Write("image.tga", Tga(1, 2, 3));
        fs::create_directories(Root / "assets" / "textures");
        Open();
        Build("image.tga");
        const auto id      = ID("image.tga");
        const auto product = Products("image.tga")[0];
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(Owner->MoveSourceAsset(id, AssetPath("textures/moved.tga"))));
        EXPECT_FALSE(fs::exists(Root / "assets" / "image.tga.kmeta"));
        EXPECT_TRUE(fs::exists(Root / "assets" / "textures" / "moved.tga.kmeta"));
        EXPECT_EQ(ID("textures/moved.tga"), id);
        // The build key does not depend on the path.
        EXPECT_EQ(Build("textures/moved.tga").Root.Disposition, AssetBuildDisposition::UpToDate);
        EXPECT_EQ(Products("textures/moved.tga")[0].Metadata.assetID, product.Metadata.assetID);

        Write("taken.tga", Tga(0, 0, 0));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Owner->RefreshAssets()));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Owner->MoveSourceAsset(id, AssetPath("taken.tga"))));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Owner->MoveSourceAsset(id, AssetPath("missing/dir.tga"))));
        Open();
        EXPECT_EQ(ID("textures/moved.tga"), id);
    }

    TEST_F(ProjectBuildTest, MoveInterruptedBetweenRenamesIsFinishedOnOpen)
    {
        Write("image.tga", Tga(1, 2, 3));
        Open();
        const auto id = ID("image.tga");
        Owner.reset();
        // Simulate a crash after the source rename but before the sidecar rename.
        fs::rename(Root / "assets" / "image.tga", Root / "assets" / "renamed.tga");
        std::ofstream(Root / "Cache" / "source-move.yaml") << "from: image.tga\nto: renamed.tga\n";
        Open();
        EXPECT_EQ(ID("renamed.tga"), id);
        EXPECT_FALSE(fs::exists(Root / "Cache" / "source-move.yaml"));
        EXPECT_FALSE(Owner->GetSourceAssetDatabase().FindByID(id).Missing);
    }

    TEST_F(ProjectBuildTest, DeletingUnpublishesProductsRemovesFilesAndReportsDependents)
    {
        TexturedModel();
        Open();
        Build("model.obj");
        const auto texture = ID("diffuse.tga");
        auto       plan    = Owner->PlanSourceAssetDeletion(texture);
        ASSERT_TRUE(std::holds_alternative<SourceAssetDeletionPlan>(plan));
        ASSERT_EQ(std::get<SourceAssetDeletionPlan>(plan).Dependents.size(), 1u);
        EXPECT_EQ(std::get<SourceAssetDeletionPlan>(plan).Dependents[0].sourceAssetID, ID("model.obj"));

        auto deleted = Owner->DeleteSourceAsset(texture);
        ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(deleted));
        for (const auto& record : std::get<Ref<const AssetRegistry>>(deleted)->GetAllProducts())
            EXPECT_NE(record.Metadata.assetID.GetSourceID(), texture);
        EXPECT_FALSE(fs::exists(Root / "assets" / "diffuse.tga"));
        EXPECT_FALSE(fs::exists(Root / "assets" / "diffuse.tga.kmeta"));
        EXPECT_FALSE(Owner->GetSourceAssetDatabase().Contains(texture));
        EXPECT_FALSE(Products("model.obj").empty());
        // Rebuilding the model drops the missing texture with a warning.
        const auto rebuilt = Build("model.obj");
        ASSERT_EQ(rebuilt.Root.Warnings.size(), 1u);
        EXPECT_NE(rebuilt.Root.Warnings[0].find("diffuse.tga"), std::string::npos);
        for (const auto& record : Products("model.obj", AssetType::Material))
            for (AssetID dependency : record.Metadata.Dependencies)
                EXPECT_NE(dependency.GetSourceID(), texture);
    }

    TEST_F(ProjectBuildTest, UnreadableSidecarsAreWarningsNotFailures)
    {
        Write("good.tga", Tga(1, 1, 1));
        Write("bad.tga", Tga(2, 2, 2));
        Write("bad.tga.kmeta", "version: [not a number");
        Open();
        EXPECT_TRUE(ID("good.tga").IsValid());
        EXPECT_FALSE(ID("bad.tga").IsValid());
        ASSERT_FALSE(Owner->GetAssetWarnings().empty());
        EXPECT_NE(Owner->GetAssetWarnings()[0].Message.find("bad.tga"), std::string::npos);
        // The broken sidecar is left untouched for the user to fix.
        EXPECT_EQ(Read(Root / "assets" / "bad.tga.kmeta"), "version: [not a number");
    }

    TEST_F(ProjectBuildTest, AProjectCanBeOpenedOnlyOnceAtATime)
    {
        Open();
        ProjectConfig config;
        config.Name          = "Second";
        config.RootDirectory = Root;
        EXPECT_TRUE(std::holds_alternative<AssetError>(Project::Open(config)));
        Owner.reset();
        EXPECT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(Project::Open(config)));
    }
} // namespace
