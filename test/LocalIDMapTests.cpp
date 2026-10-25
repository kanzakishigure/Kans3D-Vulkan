#include <limits>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/LocalIDMap.h"

namespace
{
    using namespace Kans;

    TEST(LocalIDMap, FirstAllocationIsIndependentOfTraversalOrder)
    {
        auto a = AllocateLocalIDs({}, {"mesh:Weapon", "mesh:Body"});
        auto b = AllocateLocalIDs({}, {"mesh:Body", "mesh:Weapon"});
        ASSERT_TRUE(std::holds_alternative<LocalIDMap>(a));
        ASSERT_TRUE(std::holds_alternative<LocalIDMap>(b));
        EXPECT_EQ(std::get<LocalIDMap>(a), std::get<LocalIDMap>(b));
        EXPECT_NE(std::get<LocalIDMap>(a).at("mesh:Body"), 0u);
    }

    TEST(LocalIDMap, ReimportAdditionDeletionAndRestorationPreserveIdentity)
    {
        const LocalIDMap original {{"mesh:Body", 4}, {"mesh:Weapon", 9}};
        auto             changed = AllocateLocalIDs(original, {"mesh:New", "mesh:Body"});
        ASSERT_TRUE(std::holds_alternative<LocalIDMap>(changed));
        const auto& ids = std::get<LocalIDMap>(changed);
        EXPECT_EQ(ids.at("mesh:Body"), 4u);
        EXPECT_EQ(ids.at("mesh:Weapon"), 9u);
        EXPECT_GT(ids.at("mesh:New"), 9u);
        auto restored = AllocateLocalIDs(ids, {"mesh:Weapon", "mesh:Body", "mesh:New"});
        ASSERT_TRUE(std::holds_alternative<LocalIDMap>(restored));
        EXPECT_EQ(std::get<LocalIDMap>(restored), ids);
        EXPECT_EQ(original.size(), 2u);
    }

    TEST(LocalIDMap, RejectsAmbiguousAndInvalidKeysWithoutChangingPreviousState)
    {
        const LocalIDMap original {{"mesh:Body", 4}};
        EXPECT_EQ(std::get<AssetError>(AllocateLocalIDs(original, {"mesh:A", "mesh:A"})).Code,
                  AssetErrorCode::DuplicateID);
        for (const auto& key :
             {std::string(), std::string("bad\nkey"), std::string("bad\0key", 7), std::string(4097, 'a')})
            EXPECT_EQ(std::get<AssetError>(AllocateLocalIDs(original, {key})).Code, AssetErrorCode::InvalidArgument);
        EXPECT_EQ(original.at("mesh:Body"), 4u);
        EXPECT_EQ(original.size(), 1u);
    }

    TEST(LocalIDMap, RejectsInvalidPreviousMappingsAndExhaustion)
    {
        EXPECT_EQ(std::get<AssetError>(AllocateLocalIDs({{"mesh:A", 0}}, {})).Code, AssetErrorCode::InvalidData);
        EXPECT_EQ(std::get<AssetError>(AllocateLocalIDs({{"mesh:A", 3}, {"mesh:B", 3}}, {})).Code,
                  AssetErrorCode::DuplicateID);
        const LocalIDMap full {{"mesh:A", std::numeric_limits<uint64_t>::max()}};
        EXPECT_TRUE(std::holds_alternative<LocalIDMap>(AllocateLocalIDs(full, {"mesh:A"})));
        EXPECT_EQ(std::get<AssetError>(AllocateLocalIDs(full, {"mesh:B"})).Code, AssetErrorCode::InvalidData);
    }

    TEST(LocalIDMap, ManifestRoundTripPreservesSourceUnicodeAndReservedKeys)
    {
        const LocalIDManifest manifest {SourceAssetID::Generate(), {{u8"mesh:身体", 4}, {"deleted:key", 17}}};
        auto                  encoded = EncodeLocalIDManifest(manifest);
        ASSERT_TRUE(std::holds_alternative<std::string>(encoded));
        auto decoded = DecodeLocalIDManifest(std::get<std::string>(encoded), manifest.Source);
        ASSERT_TRUE(std::holds_alternative<LocalIDManifest>(decoded));
        EXPECT_EQ(std::get<LocalIDManifest>(decoded).IDs, manifest.IDs);
        EXPECT_EQ(std::get<LocalIDManifest>(decoded).Source, manifest.Source);
        auto reencoded = EncodeLocalIDManifest(std::get<LocalIDManifest>(decoded));
        ASSERT_TRUE(std::holds_alternative<std::string>(reencoded));
        EXPECT_EQ(std::get<std::string>(reencoded), std::get<std::string>(encoded));
        EXPECT_EQ(
            std::get<AssetError>(DecodeLocalIDManifest(std::get<std::string>(encoded), SourceAssetID::Generate())).Code,
            AssetErrorCode::InvalidData);
    }

    TEST(LocalIDMap, EmptyManifestRoundTripAndInvalidIdentity)
    {
        const auto source  = SourceAssetID::Generate();
        const auto encoded = EncodeLocalIDManifest({source, {}});
        ASSERT_TRUE(std::holds_alternative<std::string>(encoded));
        EXPECT_TRUE(
            std::holds_alternative<LocalIDManifest>(DecodeLocalIDManifest(std::get<std::string>(encoded), source)));
        EXPECT_EQ(std::get<AssetError>(EncodeLocalIDManifest({{}, {}})).Code, AssetErrorCode::InvalidArgument);
        EXPECT_EQ(std::get<AssetError>(DecodeLocalIDManifest("", source)).Code, AssetErrorCode::InvalidData);
        EXPECT_EQ(std::get<AssetError>(DecodeLocalIDManifest("{}", {})).Code, AssetErrorCode::InvalidArgument);
    }

    TEST(LocalIDMap, ManifestRejectsUnsupportedVersionsDuplicateIDsAndMalformedFields)
    {
        const auto source = SourceAssetID::Generate();
        const auto prefix = "version: 1\nsource_id: '" + source.ToString() + "'\nentries: ";
        for (const char* entries :
             {"[{key: a, local_id: 2}, {key: a, local_id: 3}]", "[{key: a, local_id: 2}, {key: b, local_id: 2}]"})
            EXPECT_EQ(std::get<AssetError>(DecodeLocalIDManifest(prefix + entries, source)).Code,
                      AssetErrorCode::DuplicateID);
        for (const char* entries : {"{}",
                                    "[{}]",
                                    "[{key: '', local_id: 1}]",
                                    "[{key: a, local_id: 0}]",
                                    "[{key: a, local_id: -1}]",
                                    "[{key: a, local_id: 18446744073709551616}]"})
            EXPECT_EQ(std::get<AssetError>(DecodeLocalIDManifest(prefix + entries, source)).Code,
                      AssetErrorCode::InvalidData);
        EXPECT_EQ(std::get<AssetError>(
                      DecodeLocalIDManifest("version: 2\nsource_id: '" + source.ToString() + "'\nentries: []", source))
                      .Code,
                  AssetErrorCode::UnsupportedVersion);
        EXPECT_EQ(std::get<AssetError>(DecodeLocalIDManifest("[", source)).Code, AssetErrorCode::InvalidData);
    }
} // namespace
