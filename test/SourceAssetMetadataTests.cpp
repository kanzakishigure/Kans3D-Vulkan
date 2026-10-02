#include <gtest/gtest.h>

#include "Kans3D/Asset/SourceAssetMetaData.h"

#include <filesystem>

namespace
{
using Kans::AssetPath;
using Kans::SourceAssetID;
using Kans::SourceAssetMetadata;

TEST(SourceAssetMetadata, DefaultRecordHasInvalidIdentityAndKnownDefaults)
{
    const SourceAssetMetadata metadata;

    EXPECT_FALSE(metadata.IsValid());
    EXPECT_FALSE(metadata.sourceAssetID.IsValid());
    EXPECT_FALSE(metadata.Path.IsValid());
    EXPECT_EQ(metadata.MetaVersion, 1u);
    EXPECT_FALSE(metadata.Exists);
    EXPECT_FALSE(metadata.Missing);
    EXPECT_EQ(metadata.FileSize, 0u);
    EXPECT_EQ(metadata.last_edit_time, std::filesystem::file_time_type{});
}

TEST(SourceAssetMetadata, ValidIdentityDoesNotRequireAFileOnDisk)
{
    SourceAssetMetadata metadata;
    metadata.sourceAssetID = SourceAssetID::FromString("0123456789abcdef");
    metadata.Path = AssetPath("Future/NotCreated.png");

    ASSERT_TRUE(metadata.IsValid());
    EXPECT_FALSE(metadata.Exists);
    EXPECT_FALSE(metadata.Missing);
}

TEST(SourceAssetMetadata, MissingSourceRetainsValidMetadataIdentity)
{
    SourceAssetMetadata metadata;
    metadata.sourceAssetID = SourceAssetID::FromString("0123456789abcdef");
    metadata.Path = AssetPath("Textures/Deleted.png");
    metadata.Missing = true;

    EXPECT_TRUE(metadata.IsValid());
    EXPECT_TRUE(metadata.Missing);
    EXPECT_FALSE(metadata.Exists);
}

TEST(SourceAssetMetadata, IdentityRequiresBothSourceIDAndAssetPath)
{
    SourceAssetMetadata metadata;
    metadata.Path = AssetPath("Textures/Wood.png");
    EXPECT_FALSE(metadata.IsValid());

    metadata.sourceAssetID = SourceAssetID::FromString("0123456789abcdef");
    metadata.Path = AssetPath("../Outside.png");
    EXPECT_FALSE(metadata.IsValid());

    metadata.Path = AssetPath("Textures/Wood.png");
    EXPECT_TRUE(metadata.IsValid());
}
} // namespace
