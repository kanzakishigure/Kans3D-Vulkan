#include "../KansEditor/src/EditorAssetCachePath.h"
#include "Kans3D/Asset/AssetScanner.h"
#include "Kans3D/Asset/SourceAssetDatabaseSerializer.h"
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <yaml-cpp/yaml.h>

namespace {
using namespace Kans;
namespace fs = std::filesystem;

class SourceAssetDatabaseSerializerTest : public ::testing::Test {
protected:
  fs::path directory, root, cache;
  bool owned = false;
  void SetUp() override {
    directory = fs::temp_directory_path() /
                ("Kans3D-DBCache-" + SourceAssetID::Generate().ToString());
    owned = fs::create_directory(directory);
    ASSERT_TRUE(owned);
    root = directory / "Assets";
    fs::create_directory(root);
    cache = directory / "SourceAssetDatabase.cache.yaml";
  }
  void TearDown() override {
    if (owned) {
      std::error_code ec;
      fs::remove_all(directory, ec);
    }
  }
  SourceAssetMetadata Record(const char *id = "0123456789abcdef",
                             const char *path = "A.png") {
    SourceAssetMetadata data;
    data.sourceAssetID = SourceAssetID::FromString(id);
    data.Path = AssetPath(path);
    data.FileSize = 123456789;
    data.last_edit_time =
        fs::file_time_type(fs::file_time_type::duration(-12345678));
    data.Exists = true;
    return data;
  }
  void Write(const fs::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    out.close();
    ASSERT_TRUE(out.good());
  }
  std::string Read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
  }
  void Store(const SourceAssetDatabase &db) {
    const auto saved = SourceAssetDatabaseSerializer::Save(cache, root, db);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(saved));
  }
  void ExpectLoadError(AssetErrorCode code) {
    const auto loaded = SourceAssetDatabaseSerializer::Load(cache, root);
    ASSERT_TRUE(std::holds_alternative<AssetError>(loaded));
    EXPECT_EQ(std::get<AssetError>(loaded).Code, code);
  }
};

TEST_F(SourceAssetDatabaseSerializerTest,
       RoundTripRebuildsIndexesAndPreservesAllFields) {
  SourceAssetDatabase db;
  const auto original = Record();
  auto missing = Record("fedcba9876543210", "Missing.png");
  missing.Exists = false;
  missing.Missing = true;
  ASSERT_TRUE(
      std::holds_alternative<std::monostate>(db.RegisterSource(original)));
  ASSERT_TRUE(
      std::holds_alternative<std::monostate>(db.RegisterSource(missing)));
  Store(db);
  const auto result = SourceAssetDatabaseSerializer::Load(cache, root);
  ASSERT_TRUE(std::holds_alternative<SourceAssetDatabase>(result));
  const auto &loaded = std::get<SourceAssetDatabase>(result);
  ASSERT_EQ(loaded.GetAllSources().size(), 2u);
  for (const auto &expected : db.GetAllSources()) {
    EXPECT_EQ(loaded.FindIDByPath(expected.Path), expected.sourceAssetID);
    const auto actual = loaded.FindByID(expected.sourceAssetID);
    EXPECT_EQ(actual.Path, expected.Path);
    EXPECT_EQ(actual.MetaVersion, expected.MetaVersion);
    EXPECT_EQ(actual.Exists, expected.Exists);
    EXPECT_EQ(actual.Missing, expected.Missing);
    EXPECT_EQ(actual.FileSize, expected.FileSize);
    EXPECT_EQ(actual.last_edit_time, expected.last_edit_time);
  }
}

TEST_F(SourceAssetDatabaseSerializerTest,
       EmptyDatabaseAndDeterministicReplacement) {
  Store(SourceAssetDatabase{});
  auto empty = SourceAssetDatabaseSerializer::Load(cache, root);
  ASSERT_TRUE(std::holds_alternative<SourceAssetDatabase>(empty));
  EXPECT_TRUE(std::get<SourceAssetDatabase>(empty).GetAllSources().empty());
  SourceAssetDatabase first, second;
  const auto a = Record(), b = Record("fedcba9876543210", "B.png");
  ASSERT_TRUE(std::holds_alternative<std::monostate>(first.RegisterSource(a)));
  ASSERT_TRUE(std::holds_alternative<std::monostate>(first.RegisterSource(b)));
  ASSERT_TRUE(std::holds_alternative<std::monostate>(second.RegisterSource(b)));
  ASSERT_TRUE(std::holds_alternative<std::monostate>(second.RegisterSource(a)));
  Store(first);
  const auto text = Read(cache);
  Store(second);
  EXPECT_EQ(Read(cache), text);
}

TEST_F(SourceAssetDatabaseSerializerTest,
       RejectsMissingMalformedVersionAndClockMismatch) {
  ExpectLoadError(AssetErrorCode::NotFound);
  Write(cache, "version: [");
  ExpectLoadError(AssetErrorCode::InvalidData);
  Store(SourceAssetDatabase{});
  auto document = YAML::LoadFile(cache.string());
  document["version"] = 99;
  Write(cache, YAML::Dump(document));
  ExpectLoadError(AssetErrorCode::UnsupportedVersion);
  document["version"] = 1;
  document["clock_id"] = "another-file-clock";
  Write(cache, YAML::Dump(document));
  ExpectLoadError(AssetErrorCode::UnsupportedVersion);
}

TEST_F(SourceAssetDatabaseSerializerTest, RejectsCacheForAnotherAssetRoot) {
  Store(SourceAssetDatabase{});
  const auto other = directory / "OtherAssets";
  fs::create_directory(other);
  const auto result = SourceAssetDatabaseSerializer::Load(cache, other);
  ASSERT_TRUE(std::holds_alternative<AssetError>(result));
  EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidData);
}

TEST_F(SourceAssetDatabaseSerializerTest,
       RejectsConflictingRecordsWithoutReturningPartialDatabase) {
  SourceAssetDatabase db;
  ASSERT_TRUE(
      std::holds_alternative<std::monostate>(db.RegisterSource(Record())));
  Store(db);
  auto document = YAML::LoadFile(cache.string());
  auto duplicate = YAML::Clone(document["sources"][0]);
  duplicate["path"] = "B.png";
  document["sources"].push_back(duplicate);
  Write(cache, YAML::Dump(document));
  ExpectLoadError(AssetErrorCode::DuplicateID);
  document["sources"][1]["source_id"] = "fedcba9876543210";
  document["sources"][1]["path"] = "./A.png";
  Write(cache, YAML::Dump(document));
  ExpectLoadError(AssetErrorCode::DuplicatePath);
}

TEST_F(SourceAssetDatabaseSerializerTest, RejectsInvalidRecordFields) {
  SourceAssetDatabase db;
  ASSERT_TRUE(
      std::holds_alternative<std::monostate>(db.RegisterSource(Record())));
  Store(db);
  const auto original = Read(cache);
  for (const auto *field :
       {"source_id", "path", "file_size", "last_write_ticks"}) {
    auto document = YAML::Load(original);
    document["sources"][0].remove(field);
    Write(cache, YAML::Dump(document));
    ExpectLoadError(AssetErrorCode::InvalidData);
  }
  auto document = YAML::Load(original);
  document["sources"][0]["path"] = "../Outside.png";
  Write(cache, YAML::Dump(document));
  ExpectLoadError(AssetErrorCode::InvalidData);
  document = YAML::Load(original);
  document["sources"][0]["file_size"] = -1;
  Write(cache, YAML::Dump(document));
  ExpectLoadError(AssetErrorCode::InvalidData);
}

TEST_F(SourceAssetDatabaseSerializerTest,
       FailedSavePreservesExistingCacheAndCleansTemporaryFiles) {
  Store(SourceAssetDatabase{});
  const auto original = Read(cache);
  auto invalid = Record();
  invalid.Missing = true;
  SourceAssetDatabase db;
  ASSERT_TRUE(
      std::holds_alternative<std::monostate>(db.RegisterSource(invalid)));
  const auto rejected = SourceAssetDatabaseSerializer::Save(cache, root, db);
  ASSERT_TRUE(std::holds_alternative<AssetError>(rejected));
  EXPECT_EQ(Read(cache), original);
  const auto blocked = directory / "blocked";
  fs::create_directory(blocked);
  Write(blocked / "keep", "original");
  const auto failed =
      SourceAssetDatabaseSerializer::Save(blocked, root, SourceAssetDatabase{});
  ASSERT_TRUE(std::holds_alternative<AssetError>(failed));
  EXPECT_EQ(std::get<AssetError>(failed).Code, AssetErrorCode::IoError);
  EXPECT_EQ(Read(blocked / "keep"), "original");
  for (const auto &entry : fs::directory_iterator(directory))
    EXPECT_EQ(entry.path().filename().string().find(".tmp-"),
              std::string::npos);
}

TEST_F(SourceAssetDatabaseSerializerTest,
       RescanRestoresCacheWithoutChangingSourceIdentity) {
  Write(root / "A.custom", "old");
  auto first = AssetScanner::Scan(root);
  ASSERT_TRUE(std::holds_alternative<AssetScanResult>(first));
  const auto &before = std::get<AssetScanResult>(first);
  ASSERT_TRUE(before.Errors.empty());
  Store(before.Database);
  const auto id = before.Database.FindIDByPath(AssetPath("A.custom"));
  const auto sidecar = Read(root / "A.custom.kmeta");
  Write(root / "A.custom", "new content after shutdown");
  const auto cached = SourceAssetDatabaseSerializer::Load(cache, root);
  ASSERT_TRUE(std::holds_alternative<SourceAssetDatabase>(cached));
  EXPECT_EQ(std::get<SourceAssetDatabase>(cached).FindByID(id).FileSize, 3u);
  for (bool corrupt : {false, true}) {
    if (corrupt)
      Write(cache, "broken: [");
    else
      fs::remove(cache);
    ASSERT_TRUE(std::holds_alternative<AssetError>(
        SourceAssetDatabaseSerializer::Load(cache, root)));
    auto rescanned = AssetScanner::Scan(root);
    ASSERT_TRUE(std::holds_alternative<AssetScanResult>(rescanned));
    const auto &fresh = std::get<AssetScanResult>(rescanned);
    ASSERT_TRUE(fresh.Errors.empty());
    EXPECT_EQ(fresh.Database.FindIDByPath(AssetPath("A.custom")), id);
    EXPECT_EQ(fresh.Database.FindByID(id).FileSize,
              fs::file_size(root / "A.custom"));
    Store(fresh.Database);
    EXPECT_EQ(Read(root / "A.custom.kmeta"), sidecar);
  }
}

TEST(EditorAssetCachePath, UsesExecutableDirectory) {
  const auto result = GetEditorAssetCachePath();
  ASSERT_TRUE(std::holds_alternative<fs::path>(result));
  const auto path = std::get<fs::path>(result);
  EXPECT_TRUE(path.is_absolute());
  EXPECT_EQ(path.filename(), "SourceAssetDatabase.cache.yaml");
#if defined(__linux__)
  EXPECT_EQ(path.parent_path(),
            fs::read_symlink("/proc/self/exe").parent_path());
#endif
}
} // namespace
