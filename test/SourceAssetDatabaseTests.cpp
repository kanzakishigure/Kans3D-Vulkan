#include <gtest/gtest.h>
#include "Kans3D/Asset/SourceAssetDatabase.h"

namespace {
using namespace Kans;

SourceAssetMetadata Record(const char *id, const char *path) {
  SourceAssetMetadata data;
  data.sourceAssetID = SourceAssetID::FromString(id);
  data.Path = AssetPath(path);
  return data;
}

TEST(SourceAssetDatabase, RegistrationSupportsBothReadOnlyQueries) {
  SourceAssetDatabase database;
  const auto data = Record("0123456789abcdef", "Textures/Wood.png");
  ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(data)));
  const auto &readOnly = database;
  EXPECT_TRUE(readOnly.Contains(data.sourceAssetID));
  EXPECT_TRUE(readOnly.Contains(data.Path));
  EXPECT_EQ(readOnly.FindByID(data.sourceAssetID).Path, data.Path);
  EXPECT_EQ(readOnly.FindIDByPath(data.Path), data.sourceAssetID);
  auto copy = readOnly.FindByID(data.sourceAssetID);
  copy.Path = AssetPath("Changed.png");
  EXPECT_EQ(readOnly.FindByID(data.sourceAssetID).Path, data.Path);
}

TEST(SourceAssetDatabase, InvalidRecordsDoNotCreatePartialEntries) {
  SourceAssetDatabase database;
  auto data = Record("0123456789abcdef", "../Outside.png");
  auto result = database.RegisterSource(data);
  ASSERT_TRUE(std::holds_alternative<AssetError>(result));
  EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidArgument);
  EXPECT_FALSE(database.Contains(data.sourceAssetID));
  data = Record("0", "Textures/Wood.png");
  result = database.RegisterSource(data);
  ASSERT_TRUE(std::holds_alternative<AssetError>(result));
  EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidArgument);
  EXPECT_FALSE(database.Contains(data.Path));
}

TEST(SourceAssetDatabase, DuplicateIdPreservesOriginalAndDoesNotReserveNewPath) {
  SourceAssetDatabase database;
  const auto original = Record("0123456789abcdef", "A.png");
  ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(original)));
  auto conflict = original;
  conflict.Path = AssetPath("B.png");
  const auto result = database.RegisterSource(conflict);
  ASSERT_TRUE(std::holds_alternative<AssetError>(result));
  EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::DuplicateID);
  EXPECT_EQ(database.FindByID(original.sourceAssetID).Path, original.Path);
  EXPECT_EQ(database.FindIDByPath(original.Path), original.sourceAssetID);
  EXPECT_FALSE(database.Contains(conflict.Path));
  conflict.sourceAssetID = SourceAssetID::FromString("fedcba9876543210");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(conflict)));
}

TEST(SourceAssetDatabase, DuplicateNormalizedPathDoesNotReserveNewId) {
  SourceAssetDatabase database;
  const auto original = Record("0123456789abcdef", "Textures/A.png");
  ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(original)));
  auto conflict = Record("fedcba9876543210", "Textures/./A.png");
  const auto result = database.RegisterSource(conflict);
  ASSERT_TRUE(std::holds_alternative<AssetError>(result));
  EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::DuplicatePath);
  EXPECT_FALSE(database.Contains(conflict.sourceAssetID));
  EXPECT_EQ(database.FindIDByPath(original.Path), original.sourceAssetID);
  EXPECT_EQ(database.FindByID(original.sourceAssetID).Path, original.Path);
  conflict.Path = AssetPath("Textures/B.png");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(conflict)));
}

TEST(SourceAssetDatabase, RegisteringSameRecordTwiceReportsDuplicateId) {
  SourceAssetDatabase database;
  const auto data = Record("0123456789abcdef", "A.png");
  ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(data)));
  const auto result = database.RegisterSource(data);
  ASSERT_TRUE(std::holds_alternative<AssetError>(result));
  EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::DuplicateID);
  EXPECT_EQ(database.FindIDByPath(data.Path), data.sourceAssetID);
}

TEST(SourceAssetDatabase, MissingAndInvalidQueriesReturnInvalidValues) {
  const SourceAssetDatabase database;
  const auto data = Record("0123456789abcdef", "A.png");
  EXPECT_FALSE(database.FindByID(data.sourceAssetID).IsValid());
  EXPECT_FALSE(database.FindIDByPath(data.Path).IsValid());
  EXPECT_FALSE(database.Contains(data.sourceAssetID));
  EXPECT_FALSE(database.Contains(data.Path));
  EXPECT_FALSE(database.FindByID(SourceAssetID{}).IsValid());
  EXPECT_FALSE(database.FindIDByPath(AssetPath{}).IsValid());
  EXPECT_FALSE(database.Contains(SourceAssetID{}));
  EXPECT_FALSE(database.Contains(AssetPath{}));
}
} // namespace
