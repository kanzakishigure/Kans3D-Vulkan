#include <gtest/gtest.h>
#include "Kans3D/Asset/AssetScanner.h"
#include "Kans3D/Asset/SourceAssetMetaSerializer.h"

#include <fstream>
#include <iterator>

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;

    class AssetScannerTest : public ::testing::Test
    {
    protected:
        fs::path workspace;
        fs::path root;
        bool     owned = false;

        void SetUp() override
        {
            workspace = fs::temp_directory_path() / ("Kans3D-Scanner-" + SourceAssetID::Generate().ToString());
            owned     = fs::create_directory(workspace);
            ASSERT_TRUE(owned);
            root = workspace / "Assets";
            ASSERT_TRUE(fs::create_directory(root));
        }
        void TearDown() override
        {
            if (owned)
            {
                std::error_code ec;
                fs::permissions(root, fs::perms::owner_all, fs::perm_options::add, ec);
                fs::remove_all(workspace, ec);
            }
        }
        void Write(const fs::path& path, const std::string& text = "data")
        {
            fs::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary);
            out << text;
            out.close();
            ASSERT_TRUE(out.good());
        }
        std::string Read(const fs::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        }
        void Meta(const char* source, const char* id)
        {
            SourceAssetMetadata data;
            data.sourceAssetID = SourceAssetID::FromString(id);
            data.Path          = AssetPath(source);
            const auto result  = SourceAssetMetaSerializer::Save(fs::path((root / source).string() + ".kmeta"), data);
            ASSERT_TRUE(std::holds_alternative<std::monostate>(result));
        }
    };

    TEST_F(AssetScannerTest, CreatesIdentityAndPreservesItAcrossFreshScans)
    {
        Write(root / "Nested" / "Wood.custom", "123456");
        auto first = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetScanResult>(first));
        const auto& snapshot = std::get<AssetScanResult>(first);
        ASSERT_TRUE(snapshot.Errors.empty());
        const AssetPath path("Nested/Wood.custom");
        const auto      id = snapshot.Database.FindIDByPath(path);
        ASSERT_TRUE(id.IsValid());
        const auto data = snapshot.Database.FindByID(id);
        EXPECT_TRUE(data.Exists);
        EXPECT_FALSE(data.Missing);
        EXPECT_EQ(data.FileSize, 6u);
        EXPECT_EQ(data.last_edit_time, fs::last_write_time(root / path.GetPath()));
        const auto text = Read(root / "Nested/Wood.custom.kmeta");
        Write(root / path.GetPath(), "updated");
        auto second = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetScanResult>(second));
        const auto& rescanned = std::get<AssetScanResult>(second);
        EXPECT_TRUE(rescanned.Errors.empty());
        EXPECT_EQ(rescanned.Database.FindIDByPath(path), id);
        EXPECT_EQ(rescanned.Database.FindByID(id).FileSize, 7u);
        EXPECT_EQ(snapshot.Database.FindByID(id).FileSize, 6u);
        EXPECT_EQ(Read(root / "Nested/Wood.custom.kmeta"), text);
    }

    TEST_F(AssetScannerTest, ExcludesSidecarsHiddenAndGeneratedContent)
    {
        const std::vector<std::string> ignored = {".hidden",
                                                  ".hiddenDir/file.png",
                                                  "Cache/a.png",
                                                  "cache/b.png",
                                                  "Nested/Intermediate/a.png",
                                                  "Products/a.png",
                                                  "orphan.kmeta",
                                                  "other.KMETA",
                                                  "file.png.kmeta.tmp-old"};
        for (const auto& name : ignored)
            Write(root / name);
        Write(root / "Normal/file.bin");
        auto result = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetScanResult>(result));
        const auto& scan = std::get<AssetScanResult>(result);
        EXPECT_TRUE(scan.Errors.empty());
        EXPECT_TRUE(scan.Database.Contains(AssetPath("Normal/file.bin")));
        for (const auto& name : ignored)
        {
            EXPECT_FALSE(scan.Database.Contains(AssetPath(name)));
            EXPECT_FALSE(fs::exists(root / (name + ".kmeta")));
        }
    }

    TEST_F(AssetScannerTest, InvalidSidecarsRemainUntouchedAndOtherFilesContinue)
    {
        Write(root / "bad.png");
        Write(root / "future.png");
        Write(root / "good.png");
        const std::string malformed = "version: [\n";
        const std::string future    = "version: 99\nsource_id: 0123456789abcdef\n";
        Write(root / "bad.png.kmeta", malformed);
        Write(root / "future.png.kmeta", future);
        Meta("good.png", "fedcba9876543210");
        auto result = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetScanResult>(result));
        const auto& scan = std::get<AssetScanResult>(result);
        ASSERT_EQ(scan.Errors.size(), 2u);
        EXPECT_EQ(scan.Errors[0].Code, AssetErrorCode::InvalidData);
        EXPECT_EQ(scan.Errors[1].Code, AssetErrorCode::UnsupportedVersion);
        EXPECT_FALSE(scan.Database.Contains(AssetPath("bad.png")));
        EXPECT_FALSE(scan.Database.Contains(AssetPath("future.png")));
        EXPECT_EQ(scan.Database.FindIDByPath(AssetPath("good.png")), SourceAssetID::FromString("fedcba9876543210"));
        EXPECT_EQ(Read(root / "bad.png.kmeta"), malformed);
        EXPECT_EQ(Read(root / "future.png.kmeta"), future);
    }

    TEST_F(AssetScannerTest, DuplicateIdentityExcludesEveryConflictingSource)
    {
        Write(root / "A.png");
        Write(root / "B.png");
        Meta("A.png", "0123456789abcdef");
        Meta("B.png", "0123456789abcdef");
        const auto before = Read(root / "B.png.kmeta");
        auto       result = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetScanResult>(result));
        const auto& scan = std::get<AssetScanResult>(result);
        ASSERT_EQ(scan.Errors.size(), 2u);
        for (const auto& error : scan.Errors)
            EXPECT_EQ(error.Code, AssetErrorCode::DuplicateID);
        EXPECT_FALSE(scan.Database.Contains(AssetPath("A.png")));
        EXPECT_FALSE(scan.Database.Contains(AssetPath("B.png")));
        EXPECT_FALSE(scan.Database.Contains(SourceAssetID::FromString("0123456789abcdef")));
        EXPECT_EQ(Read(root / "B.png.kmeta"), before);
    }

    TEST_F(AssetScannerTest, EmptyRootSucceedsAndInvalidRootsReturnOuterErrors)
    {
        auto empty = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetScanResult>(empty));
        EXPECT_TRUE(std::get<AssetScanResult>(empty).Errors.empty());
        const auto absent = AssetScanner::Scan(root / "missing");
        ASSERT_TRUE(std::holds_alternative<AssetError>(absent));
        EXPECT_EQ(std::get<AssetError>(absent).Code, AssetErrorCode::NotFound);
        const auto invalid = AssetScanner::Scan({});
        ASSERT_TRUE(std::holds_alternative<AssetError>(invalid));
        EXPECT_EQ(std::get<AssetError>(invalid).Code, AssetErrorCode::InvalidArgument);
        Write(root / "file");
        const auto file = AssetScanner::Scan(root / "file");
        ASSERT_TRUE(std::holds_alternative<AssetError>(file));
        EXPECT_EQ(std::get<AssetError>(file).Code, AssetErrorCode::InvalidArgument);
    }

    TEST_F(AssetScannerTest, SidecarDirectoryIsReportedWithoutRegisteringSource)
    {
        Write(root / "blocked.png");
        fs::create_directory(root / "blocked.png.kmeta");
        const auto result = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetScanResult>(result));
        const auto& scan = std::get<AssetScanResult>(result);
        ASSERT_EQ(scan.Errors.size(), 1u);
        EXPECT_EQ(scan.Errors.front().Code, AssetErrorCode::InvalidData);
        EXPECT_FALSE(scan.Database.Contains(AssetPath("blocked.png")));
        EXPECT_TRUE(fs::is_directory(root / "blocked.png.kmeta"));
    }

    TEST_F(AssetScannerTest, DoesNotFollowSourceOrSidecarSymlinks)
    {
        Write(workspace / "Outside/data.png");
        std::error_code ec;
        fs::create_directory_symlink(workspace / "Outside", root / "linked", ec);
        if (ec)
            GTEST_SKIP() << "Symlinks unavailable: " << ec.message();
        fs::create_symlink(workspace / "Outside/data.png", root / "alias.png");
        Write(root / "regular.png");
        fs::create_symlink(workspace / "nonexistent", root / "regular.png.kmeta");
        const auto result = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetScanResult>(result));
        const auto& scan = std::get<AssetScanResult>(result);
        ASSERT_EQ(scan.Errors.size(), 1u);
        EXPECT_EQ(scan.Errors.front().Code, AssetErrorCode::InvalidData);
        EXPECT_FALSE(scan.Database.Contains(AssetPath("linked/data.png")));
        EXPECT_FALSE(scan.Database.Contains(AssetPath("alias.png")));
        EXPECT_FALSE(scan.Database.Contains(AssetPath("regular.png")));
        EXPECT_FALSE(fs::exists(workspace / "Outside/data.png.kmeta"));
        EXPECT_TRUE(fs::is_symlink(root / "regular.png.kmeta"));
        EXPECT_FALSE(fs::exists(workspace / "nonexistent"));
    }

    TEST_F(AssetScannerTest, InaccessibleRootIsAnOuterError)
    {
        fs::permissions(root, fs::perms::none);
        std::error_code        ec;
        fs::directory_iterator probe(root, ec);
        if (!ec)
            GTEST_SKIP() << "Current account bypasses directory permissions";
        const auto result = AssetScanner::Scan(root);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::IoError);
    }
} // namespace
