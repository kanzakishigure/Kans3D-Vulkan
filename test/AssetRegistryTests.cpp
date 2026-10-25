#include <gtest/gtest.h>
#include "Kans3D/Asset/AssetRegistry.h"
#include "Kans3D/Asset/Product/ContentDigest.h"

namespace
{
    using namespace Kans;
    ProductRecord Record(SourceAssetID source, uint64_t local, const std::string& target = "default")
    {
        ProductRecord record;
        record.Metadata.assetID       = AssetID(source, local);
        record.Metadata.Type          = AssetType::StaticMesh;
        record.Target                 = target;
        record.Location.RelativeFile  = AssetPath("products/" + std::to_string(local) + ".mesh");
        record.Location.ByteSize      = 3;
        record.Location.ContentDigest = ComputeContentDigest({std::byte {'a'}, std::byte {'b'}, std::byte {'c'}});
        return record;
    }

    TEST(ContentDigest, MatchesKnownSha256VectorsIncludingPaddingAndMultipleBlocks)
    {
        const std::vector<std::pair<std::string, std::string>> vectors {
            {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
            {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
            {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
            {std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"}};
        for (const auto& value : vectors)
        {
            std::vector<std::byte> bytes;
            for (char c : value.first)
                bytes.push_back(static_cast<std::byte>(c));
            EXPECT_EQ(ComputeContentDigest(bytes), value.second);
            EXPECT_TRUE(IsContentDigest(value.second));
        }
        EXPECT_FALSE(IsContentDigest(std::string(64, 'g')));
        EXPECT_FALSE(IsContentDigest(std::string(63, 'a')));
    }

    TEST(AssetRegistry, ReplacesCompleteSourceSetAndPreservesOtherSourcesAndTargets)
    {
        const auto    a = SourceAssetID::Generate(), b = SourceAssetID::Generate();
        AssetRegistry registry;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            registry.ReplaceSourceProducts(a, "default", {Record(a, 1), Record(a, 2)})));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            registry.ReplaceSourceProducts(a, "mobile", {Record(a, 1, "mobile")})));
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(registry.ReplaceSourceProducts(b, "default", {Record(b, 1)})));
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(registry.ReplaceSourceProducts(a, "default", {Record(a, 3)})));
        EXPECT_FALSE(registry.Find(AssetID(a, 1)));
        EXPECT_FALSE(registry.Find(AssetID(a, 2)));
        EXPECT_TRUE(registry.Find(AssetID(a, 3)));
        EXPECT_TRUE(registry.Find(AssetID(a, 1), "mobile"));
        EXPECT_TRUE(registry.Find(AssetID(b, 1)));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.ReplaceSourceProducts(a, "default", {})));
        EXPECT_FALSE(registry.Find(AssetID(a, 3)));
    }

    TEST(AssetRegistry, InvalidReplacementPreservesAllRecordsAndMetadataIsACopy)
    {
        const auto    source = SourceAssetID::Generate();
        AssetRegistry registry;
        const auto    original = Record(source, 1);
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(registry.ReplaceSourceProducts(source, "default", {original})));
        const auto before = std::get<std::string>(EncodeAssetRegistry(registry));
        EXPECT_EQ(std::get<AssetError>(
                      registry.ReplaceSourceProducts(source, "default", {Record(source, 2), Record(source, 2)}))
                      .Code,
                  AssetErrorCode::DuplicateID);
        auto invalid                  = Record(source, 3);
        invalid.Location.RelativeFile = AssetPath("../outside");
        EXPECT_TRUE(std::holds_alternative<AssetError>(registry.ReplaceSourceProducts(source, "default", {invalid})));
        EXPECT_TRUE(std::holds_alternative<AssetError>(
            registry.ReplaceSourceProducts(source, "default", {Record(SourceAssetID::Generate(), 4)})));
        EXPECT_EQ(std::get<std::string>(EncodeAssetRegistry(registry)), before);
        auto metadata = registry.GetAssetMetadata(original.Metadata.assetID);
        ASSERT_NE(metadata, nullptr);
        metadata->Type = AssetType::Texture;
        EXPECT_EQ(registry.Find(original.Metadata.assetID)->Metadata.Type, AssetType::StaticMesh);
    }

    TEST(AssetRegistry, RuntimeCodecRoundTripsDeterministicallyWithoutToolState)
    {
        const auto source = SourceAssetID::Generate();
        auto       a = Record(source, 1), b = Record(source, 2);
        a.Metadata.Dependencies = {b.Metadata.assetID};
        auto built              = AssetRegistry::FromProducts({b, a});
        ASSERT_TRUE(std::holds_alternative<AssetRegistry>(built));
        auto encoded = EncodeAssetRegistry(std::get<AssetRegistry>(built));
        ASSERT_TRUE(std::holds_alternative<std::string>(encoded));
        const auto& bytes = std::get<std::string>(encoded);
        EXPECT_EQ(bytes.find("local_ids"), std::string::npos);
        EXPECT_EQ(bytes.find("fingerprint"), std::string::npos);
        auto decoded = DecodeAssetRegistry(bytes);
        ASSERT_TRUE(std::holds_alternative<AssetRegistry>(decoded));
        EXPECT_EQ(std::get<std::string>(EncodeAssetRegistry(std::get<AssetRegistry>(decoded))), bytes);
        EXPECT_EQ(std::get<AssetRegistry>(decoded).Find(a.Metadata.assetID)->Metadata.Dependencies,
                  a.Metadata.Dependencies);
        auto empty = EncodeAssetRegistry({});
        EXPECT_TRUE(std::holds_alternative<AssetRegistry>(DecodeAssetRegistry(std::get<std::string>(empty))));
    }

    TEST(AssetRegistry, RejectsInvalidIdentityTypeTargetSizeDigestAndDependencies)
    {
        const auto                 source   = SourceAssetID::Generate();
        const auto                 original = Record(source, 1);
        std::vector<ProductRecord> invalid;
        auto                       record = original;
        record.Metadata.assetID           = {};
        invalid.push_back(record);
        record               = original;
        record.Metadata.Type = static_cast<AssetType>(65535);
        invalid.push_back(record);
        record        = original;
        record.Target = "../outside";
        invalid.push_back(record);
        record                   = original;
        record.Location.ByteSize = 0;
        invalid.push_back(record);
        record                        = original;
        record.Location.ContentDigest = "bad";
        invalid.push_back(record);
        record                       = original;
        record.Metadata.Dependencies = {record.Metadata.assetID};
        invalid.push_back(record);
        record                       = original;
        record.Metadata.Dependencies = {{}};
        invalid.push_back(record);
        for (const auto& bad : invalid)
            EXPECT_TRUE(std::holds_alternative<AssetError>(AssetRegistry::FromProducts({bad})));
        record               = Record(source, 1, "mobile");
        record.Metadata.Type = AssetType::Texture;
        EXPECT_TRUE(std::holds_alternative<AssetError>(AssetRegistry::FromProducts({original, record})));
        for (const auto& text : {"[",
                                 "version: 1\nproducts: {}",
                                 "version: 1\nproducts: [{}]",
                                 "version: 1\nproducts: []\n---\nversion: 1\nproducts: []"})
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeAssetRegistry(text)));
        EXPECT_EQ(std::get<AssetError>(DecodeAssetRegistry("version: 2\nproducts: []")).Code,
                  AssetErrorCode::UnsupportedVersion);
    }
    TEST(AssetRegistryChanges, ReportsTypedContentChangesAndIgnoresStorageAndDependencyOrder)
    {
        const auto source  = SourceAssetID::Generate();
        auto       removed = Record(source, 1), modified = Record(source, 2), stable = Record(source, 3),
             added   = Record(source, 4);
        const auto x = AssetID(SourceAssetID::Generate(), 1), y = AssetID(SourceAssetID::Generate(), 2);
        stable.Metadata.Dependencies = {x, y};
        auto before                  = AssetRegistry::FromProducts({removed, modified, stable});
        ASSERT_TRUE(std::holds_alternative<AssetRegistry>(before));
        modified.Location.ContentDigest = ComputeContentDigest({std::byte {4}});
        modified.Location.ByteSize      = 1;
        stable.Location.RelativeFile    = AssetPath("relocated/stable.product");
        stable.Metadata.Dependencies    = {y, x};
        auto after                      = AssetRegistry::FromProducts({added, stable, modified});
        ASSERT_TRUE(std::holds_alternative<AssetRegistry>(after));
        auto changed = CompareAssetRegistries(std::get<AssetRegistry>(before), std::get<AssetRegistry>(after));
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(changed));
        const auto& delta = std::get<ProductChangeSet>(changed);
        EXPECT_EQ(delta.Added, std::vector<AssetID> {added.Metadata.assetID});
        EXPECT_EQ(delta.Modified, std::vector<AssetID> {modified.Metadata.assetID});
        EXPECT_EQ(delta.Removed, std::vector<AssetID> {removed.Metadata.assetID});
        auto references                  = stable;
        references.Metadata.Dependencies = {x};
        EXPECT_FALSE(SameProductContent(stable, references));
        auto type          = stable;
        type.Metadata.Type = AssetType::Model;
        EXPECT_FALSE(SameProductContent(stable, type));
        references.Metadata.Dependencies = {x, x};
        EXPECT_FALSE(SameProductContent(stable, references));
    }

    TEST(AssetRegistryChanges, TargetIsolationEmptySnapshotsAndStableIdentityOrder)
    {
        const auto source = SourceAssetID::Generate();
        auto       first = Record(source, 1), second = Record(source, 2);
        auto       mobile = Record(source, 1, "mobile");
        auto       before = AssetRegistry::FromProducts({mobile});
        auto       after  = AssetRegistry::FromProducts({second, first, mobile});
        ASSERT_TRUE(std::holds_alternative<AssetRegistry>(before));
        ASSERT_TRUE(std::holds_alternative<AssetRegistry>(after));
        auto delta = CompareAssetRegistries(std::get<AssetRegistry>(before), std::get<AssetRegistry>(after));
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(delta));
        EXPECT_EQ(std::get<ProductChangeSet>(delta).Added,
                  (std::vector<AssetID> {first.Metadata.assetID, second.Metadata.assetID}));
        auto isolated =
            CompareAssetRegistries(std::get<AssetRegistry>(before), std::get<AssetRegistry>(after), "mobile");
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(isolated));
        EXPECT_TRUE(std::get<ProductChangeSet>(isolated).Added.empty());
        EXPECT_TRUE(std::get<ProductChangeSet>(isolated).Modified.empty());
        EXPECT_TRUE(std::get<ProductChangeSet>(isolated).Removed.empty());
        EXPECT_TRUE(std::holds_alternative<AssetError>(CompareAssetRegistries({}, {}, "../target")));
    }
} // namespace
