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
        EXPECT_EQ(loaded.MetaVersion, 3u);
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
        Write("version: 4\nsource_id: 0123456789abcdef\n");
        const auto loadResult = SourceAssetMetaSerializer::Load(file, AssetPath("Textures/Wood.png"));
        ASSERT_TRUE(std::holds_alternative<AssetError>(loadResult));
        EXPECT_EQ(std::get<AssetError>(loadResult).Code, AssetErrorCode::UnsupportedVersion);

        SourceAssetMetadata metadata = ValidMetadata();
        metadata.MetaVersion         = 4;
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

    TEST_F(SourceAssetMetaSerializerTest, ConfigurationRoundTripPreservesStringValuesAndUnicodePath)
    {
        file                     = directory / std::filesystem::u8path(u8"纹理.png.kmeta");
        auto metadata            = ValidMetadata();
        metadata.Import.Importer = "TextureImporter";
        metadata.Import.Settings = {{"bool", "false"},
                                    {"null", "null"},
                                    {"empty", ""},
                                    {"text", u8"颜色\n设置"},
                                    {"binary", std::string("a\0b", 3)}};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SourceAssetMetaSerializer::Save(file, metadata)));
        auto loaded = SourceAssetMetaSerializer::Load(file, metadata.Path);
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loaded));
        EXPECT_EQ(std::get<SourceAssetMetadata>(loaded).Import.Importer, metadata.Import.Importer);
        EXPECT_EQ(std::get<SourceAssetMetadata>(loaded).Import.Settings, metadata.Import.Settings);
    }

    TEST_F(SourceAssetMetaSerializerTest, LegacySidecarUpgradesWithoutChangingIdentity)
    {
        Write("version: 1\nsource_id: 0123456789abcdef\n");
        auto loaded = SourceAssetMetaSerializer::Load(file, ValidMetadata().Path);
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loaded));
        auto metadata = std::get<SourceAssetMetadata>(loaded);
        EXPECT_EQ(metadata.MetaVersion, 1u);
        EXPECT_TRUE(metadata.Import.Settings.empty());
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SourceAssetMetaSerializer::Save(file, metadata)));
        loaded = SourceAssetMetaSerializer::Load(file, metadata.Path);
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loaded));
        EXPECT_EQ(std::get<SourceAssetMetadata>(loaded).sourceAssetID, metadata.sourceAssetID);
        EXPECT_EQ(std::get<SourceAssetMetadata>(loaded).MetaVersion, 3u);
    }

    TEST_F(SourceAssetMetaSerializerTest, LocalIDsRoundTripAndMalformedEntriesReject)
    {
        auto metadata     = ValidMetadata();
        metadata.LocalIDs = {{"mesh:4:Body", 0x1234}, {"model:root", 0xfffffffffffffffe}, {"中文:key", 7}};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SourceAssetMetaSerializer::Save(file, metadata)));
        auto loaded = SourceAssetMetaSerializer::Load(file, metadata.Path);
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loaded));
        EXPECT_EQ(std::get<SourceAssetMetadata>(loaded).LocalIDs, metadata.LocalIDs);

        const std::string header = "version: 3\nsource_id: 0123456789abcdef\nimport: {importer: '', settings: {}}\n";
        for (const auto& ids : {"local_ids: [a]\n",
                                "local_ids: {a: 0}\n",
                                "local_ids: {a: zz}\n",
                                "local_ids: {a: 1, a: 2}\n",
                                "local_ids: {'': 1}\n"})
        {
            Write(header + ids);
            EXPECT_TRUE(std::holds_alternative<AssetError>(SourceAssetMetaSerializer::Load(file, metadata.Path)))
                << ids;
        }
        Write(header);
        loaded = SourceAssetMetaSerializer::Load(file, metadata.Path);
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loaded));
        EXPECT_TRUE(std::get<SourceAssetMetadata>(loaded).LocalIDs.empty());
    }

    TEST_F(SourceAssetMetaSerializerTest, RejectsAmbiguousConfigurationAndMultipleDocuments)
    {
        const std::string header = "version: 2\nsource_id: 0123456789abcdef\n";
        for (const auto& suffix : {"",
                                   "import: {importer: '', settings: []}\n",
                                   "import: {importer: '', settings: {x: 1, x: 2}}\n",
                                   "import: {importer: '', settings: {x: [1]}}\n",
                                   "import: {importer: '', settings: {}, surprise: 1}\n",
                                   "import: {importer: '', settings: {}}\nversion: 2\n",
                                   "import: {importer: '', settings: {}}\n---\nother: 1\n"})
        {
            SCOPED_TRACE(suffix);
            Write(header + suffix);
            auto result = SourceAssetMetaSerializer::Load(file, ValidMetadata().Path);
            ASSERT_TRUE(std::holds_alternative<AssetError>(result));
            EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidData);
        }
    }

    TEST_F(SourceAssetMetaSerializerTest, InvalidConfigurationPreservesSavedSidecar)
    {
        auto metadata                     = ValidMetadata();
        metadata.Import.Settings["usage"] = "color";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(SourceAssetMetaSerializer::Save(file, metadata)));
        metadata.Import.Settings[""] = "invalid";
        EXPECT_TRUE(std::holds_alternative<AssetError>(SourceAssetMetaSerializer::Save(file, metadata)));
        auto loaded = SourceAssetMetaSerializer::Load(file, metadata.Path);
        ASSERT_TRUE(std::holds_alternative<SourceAssetMetadata>(loaded));
        EXPECT_EQ(std::get<SourceAssetMetadata>(loaded).Import.Settings.size(), 1u);
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            EXPECT_EQ(entry.path(), file);
    }

    TEST_F(SourceAssetMetaSerializerTest, RejectsLossyUnicodeAndConfigurationBudgetsBeforeSaving)
    {
        for (const auto& text : {std::string("\xff", 1),
                                 std::string("\xc0\xaf", 2),
                                 std::string("\xed\xa0\x80", 3),
                                 std::string("\xf4\x90\x80\x80", 4),
                                 std::string("\xef\xbf\xbf", 3),
                                 std::string("\xe2\x82", 2),
                                 std::string(Kans::SourceImportConfiguration::MaximumString + 1, 'a')})
        {
            auto metadata                     = ValidMetadata();
            metadata.Import.Settings["value"] = text;
            EXPECT_TRUE(std::holds_alternative<AssetError>(SourceAssetMetaSerializer::Save(file, metadata)));
            EXPECT_FALSE(std::filesystem::exists(file));
        }
        auto metadata = ValidMetadata();
        for (size_t i = 0; i < 17; ++i)
            metadata.Import.Settings[std::to_string(i)] = std::string(64 * 1024, 'a');
        EXPECT_TRUE(std::holds_alternative<AssetError>(SourceAssetMetaSerializer::Save(file, metadata)));
        metadata.Import.Settings.clear();
        for (size_t i = 0; i <= Kans::SourceImportConfiguration::MaximumSettings; ++i)
            metadata.Import.Settings[std::to_string(i)] = "a";
        EXPECT_TRUE(std::holds_alternative<AssetError>(SourceAssetMetaSerializer::Save(file, metadata)));
    }

    TEST_F(SourceAssetMetaSerializerTest, RejectsOversizedFileBeforeParsing)
    {
        Write("version: 1\nsource_id: 0123456789abcdef\n");
        std::filesystem::resize_file(file, 8 * 1024 * 1024 + 1);
        auto loaded = SourceAssetMetaSerializer::Load(file, ValidMetadata().Path);
        ASSERT_TRUE(std::holds_alternative<AssetError>(loaded));
        EXPECT_EQ(std::get<AssetError>(loaded).Code, AssetErrorCode::InvalidData);
    }

    TEST_F(SourceAssetMetaSerializerTest, SaveIoFailureReturnsError)
    {
        const auto result =
            SourceAssetMetaSerializer::Save(directory / "MissingDirectory" / "Wood.png.kmeta", ValidMetadata());
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::IoError);
    }

} // namespace
