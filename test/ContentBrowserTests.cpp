#include <fstream>
#include <thread>
#include <gtest/gtest.h>
#include "../KansEditor/src/Panels/ContentBrowser/AssetFileOperations.h"
#include "../KansEditor/src/Panels/ContentBrowser/BrowserModel.h"
#include "Kans3D/Core/Log/Log.h"

namespace
{
    using namespace Kans;
    using namespace Kans::Browser;

    void Write(const fs::path& file, const std::string& text)
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
    }

    class TempDirectory : public ::testing::Test
    {
    protected:
        fs::path Root;
        void     SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            Root = fs::temp_directory_path() / ("kans-browser-" + SourceAssetID::Generate().ToString());
            fs::create_directories(Root);
        }
        void TearDown() override
        {
            std::error_code ignored;
            fs::remove_all(Root, ignored);
        }
    };

    Entry Make(const std::string& name, bool directory, uint64_t size = 0)
    {
        Entry entry;
        entry.Path      = fs::path("/x") / name;
        entry.Name      = name;
        entry.Directory = directory;
        entry.Extension = directory ? "" : Lowercase(fs::path(name).extension().string());
        entry.Kind      = KindOf(entry.Path, directory);
        entry.Size      = size;
        return entry;
    }

    std::vector<std::string> Names(const std::vector<Entry>& entries)
    {
        std::vector<std::string> names;
        for (const auto& entry : entries)
            names.push_back(entry.Name);
        return names;
    }

    TEST(BrowserModel, SortsFoldersFirstByKeyAndDirection)
    {
        std::vector<Entry> entries = {Make("b.png", false, 30), Make("Zeta", true), Make("a.fbx", false, 50),
                                      Make("C.material", false, 10), Make("alpha", true)};
        SortEntries(entries, SortKey::Name, true);
        EXPECT_EQ(Names(entries), (std::vector<std::string> {"alpha", "Zeta", "a.fbx", "b.png", "C.material"}));
        SortEntries(entries, SortKey::Size, false);
        EXPECT_EQ(Names(entries), (std::vector<std::string> {"Zeta", "alpha", "a.fbx", "b.png", "C.material"}));
        SortEntries(entries, SortKey::Type, true);
        EXPECT_EQ(Names(entries), (std::vector<std::string> {"alpha", "Zeta", "a.fbx", "b.png", "C.material"}));
    }

    TEST(BrowserModel, FiltersByTextAndKind)
    {
        Filter filter;
        EXPECT_FALSE(filter.IsActive());
        filter.Text = "CLOTH";
        EXPECT_TRUE(filter.Matches(Make("clothes_Light.tga", false)));
        EXPECT_FALSE(filter.Matches(Make("hair.tga", false)));
        filter.Text                                       = "";
        filter.Kinds[static_cast<int>(AssetKind::Texture)] = false;
        EXPECT_TRUE(filter.IsActive());
        EXPECT_FALSE(filter.Matches(Make("hair.tga", false)));
        EXPECT_TRUE(filter.Matches(Make("GY.fbx", false)));
        EXPECT_EQ(KindOf("x.SHADERPROG", false), AssetKind::Shader);
        EXPECT_EQ(KindOf("x.kans", false), AssetKind::Scene);
    }

    TEST(BrowserModel, NamesPathsAndHiddenFiles)
    {
        EXPECT_TRUE(ValidName("New Folder"));
        for (const char* bad : {"", ".", "..", "a/b", "a\\b", "a:b", "trail.", " lead", "tab\t"})
            EXPECT_FALSE(ValidName(bad)) << bad;
        EXPECT_TRUE(IsWithin("/a/b/c", "/a/b"));
        EXPECT_TRUE(IsWithin("/a/b", "/a/b"));
        EXPECT_FALSE(IsWithin("/a/bc", "/a/b"));
        EXPECT_TRUE(IsHidden("x.png.kmeta"));
        EXPECT_TRUE(IsHidden("x.png.kmeta.tmp-1234"));
        EXPECT_TRUE(IsHidden(".git"));
        EXPECT_FALSE(IsHidden("x.png"));
    }

    TEST(BrowserModel, SelectionClickCtrlShiftAndRetain)
    {
        const std::vector<fs::path> order = {"a", "b", "c", "d"};
        Selection                   selection;
        selection.Click("b", false, false, order);
        selection.Click("d", false, true, order);
        EXPECT_EQ(selection.Ordered(order), (std::vector<fs::path> {"b", "c", "d"}));
        selection.Click("c", true, false, order);
        EXPECT_EQ(selection.Ordered(order), (std::vector<fs::path> {"b", "d"}));
        selection.Click("a", false, false, order);
        EXPECT_EQ(selection.Ordered(order), (std::vector<fs::path> {"a"}));
        selection.SetAll(order);
        selection.Retain({"a", "c"});
        EXPECT_EQ(selection.Ordered(order), (std::vector<fs::path> {"a", "c"}));
    }

    TEST_F(TempDirectory, DirectoryCacheHidesSidecarsAndSeesChanges)
    {
        Write(Root / "a.png", "1");
        Write(Root / "a.png.kmeta", "id");
        fs::create_directory(Root / "sub");
        DirectoryCache cache(std::chrono::milliseconds(0));
        EXPECT_EQ(Names(cache.List(Root)), (std::vector<std::string> {"sub", "a.png"}));
        EXPECT_TRUE(cache.HasSubdirectories(Root));
        EXPECT_FALSE(cache.HasSubdirectories(Root / "sub"));
        const auto generation = cache.Generation();
        Write(Root / "b.png", "2");
        EXPECT_EQ(Names(cache.List(Root)), (std::vector<std::string> {"sub", "a.png", "b.png"}));
        EXPECT_GT(cache.Generation(), generation);
        EXPECT_EQ(UniqueChildPath(Root, "a", ".png"), Root / "a (1).png");
    }

    class AssetFileOperationsTest : public TempDirectory
    {
    protected:
        Scope<EditorAssetImportService> Service;
        AssetFileOperations             Operations;
        fs::path                        Assets;

        void SetUp() override
        {
            TempDirectory::SetUp();
            Assets = Root / "assets";
            Write(Assets / "tex" / "a.png", "png");
            Write(Assets / "tex" / "b.png", "png2");
            Write(Assets / "c.material", "material");
            ProjectConfig config;
            config.Name          = "Browser";
            config.RootDirectory = Root;
            auto project         = Project::Open(config);
            ASSERT_TRUE(std::holds_alternative<Scope<Project>>(project));
            Service = std::get<Scope<EditorAssetImportService>>(
                EditorAssetImportService::Create(std::get<Scope<Project>>(std::move(project))));
            Operations.SetService(Service.get());
            Assets = Operations.AssetRoot();
        }
        void TearDown() override
        {
            Service.reset();
            TempDirectory::TearDown();
        }
        SourceAssetID ID(const fs::path& file) const
        {
            const auto source = Operations.FindSource(file);
            return source ? source->sourceAssetID : SourceAssetID {};
        }
    };

    TEST_F(AssetFileOperationsTest, RenameAndMoveKeepIdentity)
    {
        const auto id = ID(Assets / "tex" / "a.png");
        ASSERT_TRUE(id.IsValid());
        auto renamed = Operations.Rename(Assets / "tex" / "a.png", "renamed.png");
        ASSERT_TRUE(std::holds_alternative<fs::path>(renamed)) << std::get<AssetError>(renamed).Message;
        EXPECT_EQ(ID(Assets / "tex" / "renamed.png"), id);
        EXPECT_TRUE(fs::exists(Assets / "tex" / "renamed.png.kmeta"));

        // Folders move with their sidecars; every source keeps its identity.
        const auto other  = ID(Assets / "tex" / "b.png");
        auto       folder = Operations.CreateFolder(Assets, "Textures");
        ASSERT_TRUE(std::holds_alternative<fs::path>(folder));
        auto moved = Operations.Move({Assets / "tex"}, std::get<fs::path>(folder));
        ASSERT_TRUE(std::holds_alternative<std::vector<fs::path>>(moved)) << std::get<AssetError>(moved).Message;
        EXPECT_EQ(ID(Assets / "Textures" / "tex" / "renamed.png"), id);
        EXPECT_EQ(ID(Assets / "Textures" / "tex" / "b.png"), other);
        auto folderRename = Operations.Rename(Assets / "Textures", "Images");
        ASSERT_TRUE(std::holds_alternative<fs::path>(folderRename));
        EXPECT_EQ(ID(Assets / "Images" / "tex" / "b.png"), other);

        // Invalid moves are rejected.
        EXPECT_TRUE(std::holds_alternative<AssetError>(Operations.Move({Assets / "Images"}, Assets / "Images" / "tex")));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Operations.Rename(Assets / "c.material", "a/b")));
        Write(Assets / "d.material", "other");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Service->RefreshSources()));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Operations.Rename(Assets / "c.material", "d.material")));
        EXPECT_TRUE(std::holds_alternative<AssetError>(Operations.Rename(Root / "outside.txt", "x.txt")));
    }

    TEST_F(AssetFileOperationsTest, CopiesAreNewSourcesWithTheOriginalImportSettings)
    {
        const auto original = Operations.FindSource(Assets / "tex" / "a.png");
        ASSERT_TRUE(original);
        SourceImportConfiguration configuration;
        configuration.Settings["usage"] = "data";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            Service->SetSourceImportConfiguration(original->sourceAssetID, configuration)));
        auto copied = Operations.Copy({Assets / "tex" / "a.png"}, Assets / "tex");
        ASSERT_TRUE(std::holds_alternative<std::vector<fs::path>>(copied)) << std::get<AssetError>(copied).Message;
        ASSERT_EQ(std::get<std::vector<fs::path>>(copied), (std::vector<fs::path> {Assets / "tex" / "a (1).png"}));
        const auto copy = Operations.FindSource(Assets / "tex" / "a (1).png");
        ASSERT_TRUE(copy);
        EXPECT_NE(copy->sourceAssetID, original->sourceAssetID);
        EXPECT_EQ(copy->Import.Settings.at("usage"), "data");

        auto folder = Operations.Copy({Assets / "tex"}, Assets);
        ASSERT_TRUE(std::holds_alternative<std::vector<fs::path>>(folder)) << std::get<AssetError>(folder).Message;
        ASSERT_EQ(std::get<std::vector<fs::path>>(folder), (std::vector<fs::path> {Assets / "tex (1)"}));
        EXPECT_FALSE(fs::exists(Assets / "tex (1)" / "a.png.kmeta") &&
                     ID(Assets / "tex (1)" / "a.png") == original->sourceAssetID);
        EXPECT_TRUE(std::holds_alternative<AssetError>(Operations.Copy({Assets / "tex"}, Assets / "tex")));
        const auto nested = Operations.FindSource(Assets / "tex (1)" / "a.png");
        ASSERT_TRUE(nested);
        EXPECT_NE(nested->sourceAssetID, original->sourceAssetID);
        EXPECT_EQ(nested->Import.Settings.at("usage"), "data");
    }

    TEST_F(AssetFileOperationsTest, DeleteRemovesSourcesAndPreviewCountsFiles)
    {
        auto preview = Operations.PlanDelete({Assets / "tex", Assets / "c.material"});
        ASSERT_TRUE(std::holds_alternative<DeletionPreview>(preview));
        EXPECT_EQ(std::get<DeletionPreview>(preview).Files, 3u);
        EXPECT_EQ(std::get<DeletionPreview>(preview).Folders, 1u);
        EXPECT_TRUE(std::get<DeletionPreview>(preview).Dependents.empty());
        const auto id = ID(Assets / "tex" / "a.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Operations.Delete({Assets / "tex", Assets / "c.material"})));
        EXPECT_FALSE(fs::exists(Assets / "tex"));
        EXPECT_FALSE(fs::exists(Assets / "c.material.kmeta"));
        auto sources = Service->GetSources();
        ASSERT_TRUE(std::holds_alternative<std::vector<SourceAssetMetadata>>(sources));
        for (const auto& source : std::get<std::vector<SourceAssetMetadata>>(sources))
            EXPECT_NE(source.sourceAssetID, id);
    }
} // namespace
