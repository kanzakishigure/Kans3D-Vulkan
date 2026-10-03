#include <gtest/gtest.h>

#include "Kans3D/Asset/SourceAssetMetaSerializer.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace
{

    using Kans::AssetError;
    using Kans::AssetErrorCode;
    using Kans::AssetPath;
    using Kans::SourceAssetID;
    using Kans::SourceAssetMetadata;
    using Kans::SourceAssetMetaSerializer;

    class SourceAssetMetaSerializerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            directory =
                std::filesystem::temp_directory_path() / ("Kans3D-KMeta-" + SourceAssetID::Generate().ToString());
            createdDirectory = std::filesystem::create_directory(directory);
            ASSERT_TRUE(createdDirectory);
            file = directory / "Wood.png.kmeta";
        }

        void TearDown() override
        {
            if (createdDirectory)
                std::filesystem::remove_all(directory);
        }

        void Write(const std::string& contents)
        {
            std::ofstream output(file);
            output << contents;
        }

        SourceAssetMetadata ValidMetadata() const
        {
            SourceAssetMetadata metadata;
            metadata.sourceAssetID = SourceAssetID::FromString("0123456789abcdef");
            metadata.Path          = AssetPath("Textures/Wood.png");
            return metadata;
        }

        std::filesystem::path directory;
        std::filesystem::path file;
        bool                  createdDirectory = false;
    };

    TEST_F(SourceAssetMetaSerializerTest, SaveAndLoadIdentity)
    {
        SourceAssetMetadata saved = ValidMetadata();
        saved.Exists              = true;
        saved.FileSize            = 1234;

        const auto saveResult = SourceAssetMetaSerializer::Save(file, saved);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(saveResult));

        const auto loadResult = SourceAssetMetaSerializer::Load(file, saved.Path);
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loadResult));
        const auto& loaded = std::get<SourceAssetMetadata>(loadResult);
        EXPECT_EQ(loaded.MetaVersion, 1u);
        EXPECT_EQ(loaded.sourceAssetID, saved.sourceAssetID);
        EXPECT_EQ(loaded.Path, saved.Path);
        EXPECT_FALSE(loaded.Exists);
        EXPECT_EQ(loaded.FileSize, 0u);
    }

    TEST_F(SourceAssetMetaSerializerTest, SaveReplacesAnExistingMetaFile)
    {
        Write("not: valid metadata\n");
        const SourceAssetMetadata metadata = ValidMetadata();

        const auto saveResult = SourceAssetMetaSerializer::Save(file, metadata);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(saveResult));
        const auto loadResult = SourceAssetMetaSerializer::Load(file, metadata.Path);
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loadResult));
        EXPECT_EQ(std::get<SourceAssetMetadata>(loadResult).sourceAssetID, metadata.sourceAssetID);
    }

    TEST_F(SourceAssetMetaSerializerTest, RejectsInvalidMetadataWithoutReplacingFile)
    {
        Write("version: 1\nsource_id: 0123456789abcdef\n");
        const SourceAssetMetadata invalid;

        const auto saveResult = SourceAssetMetaSerializer::Save(file, invalid);
        ASSERT_TRUE(std::holds_alternative<AssetError>(saveResult));
        EXPECT_EQ(std::get<AssetError>(saveResult).Code, AssetErrorCode::InvalidArgument);
        EXPECT_NE(std::get<AssetError>(saveResult).Message.find("Invalid source asset metadata"), std::string::npos);

        const auto loadResult = SourceAssetMetaSerializer::Load(file, AssetPath("Textures/Wood.png"));
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loadResult));
        EXPECT_EQ(std::get<SourceAssetMetadata>(loadResult).sourceAssetID.ToString(), "0123456789abcdef");
    }

    TEST_F(SourceAssetMetaSerializerTest, RejectsUnsupportedVersion)
    {
        Write("version: 2\nsource_id: 0123456789abcdef\n");
        const auto loadResult = SourceAssetMetaSerializer::Load(file, AssetPath("Textures/Wood.png"));
        ASSERT_TRUE(std::holds_alternative<AssetError>(loadResult));
        EXPECT_EQ(std::get<AssetError>(loadResult).Code, AssetErrorCode::UnsupportedVersion);

        SourceAssetMetadata metadata = ValidMetadata();
        metadata.MetaVersion         = 2;
        const auto saveResult        = SourceAssetMetaSerializer::Save(file, metadata);
        ASSERT_TRUE(std::holds_alternative<AssetError>(saveResult));
        EXPECT_EQ(std::get<AssetError>(saveResult).Code, AssetErrorCode::UnsupportedVersion);
    }

    TEST_F(SourceAssetMetaSerializerTest, RejectsMalformedOrMissingFields)
    {
        const AssetPath sourcePath("Textures/Wood.png");

        Write("version: 1\n");
        auto result = SourceAssetMetaSerializer::Load(file, sourcePath);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidData);
        Write("version: 1\nsource_id: not-an-id\n");
        result = SourceAssetMetaSerializer::Load(file, sourcePath);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidData);
        Write("version: [1]\nsource_id: 0123456789abcdef\n");
        result = SourceAssetMetaSerializer::Load(file, sourcePath);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidData);
        Write("version: 1\nsource_id: 123\n");
        result = SourceAssetMetaSerializer::Load(file, sourcePath);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidData);
        Write("version: [\n");
        result = SourceAssetMetaSerializer::Load(file, sourcePath);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidData);
    }

    TEST_F(SourceAssetMetaSerializerTest, RejectsInvalidLogicalSourcePath)
    {
        Write("version: 1\nsource_id: 0123456789abcdef\n");
        const auto result = SourceAssetMetaSerializer::Load(file, AssetPath("../Outside.png"));
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidArgument);
    }

    TEST_F(SourceAssetMetaSerializerTest, MissingFileReturnsError)
    {
        const auto result = SourceAssetMetaSerializer::Load(file, AssetPath("Textures/Wood.png"));
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::NotFound);
    }

    TEST_F(SourceAssetMetaSerializerTest, SaveIoFailureReturnsError)
    {
        const auto result =
            SourceAssetMetaSerializer::Save(directory / "MissingDirectory" / "Wood.png.kmeta", ValidMetadata());
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::IoError);
    }

} // namespace
