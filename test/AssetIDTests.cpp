#include <gtest/gtest.h>

#include "Kans3D/Asset/Asset.h"

#include <functional>
#include <string>
#include <unordered_set>

namespace
{
    using Kans::AssetID;
    using Kans::SourceAssetID;

    SourceAssetID TestSourceID() { return SourceAssetID::FromString("0123456789abcdef"); }

    TEST(AssetID, DefaultConstructedIDIsInvalid)
    {
        const AssetID id;

        EXPECT_FALSE(id.IsValid());
        EXPECT_FALSE(id.GetSourceID().IsValid());
        EXPECT_EQ(id.GetLocalID(), 0u);
        EXPECT_EQ(id.ToString(), "0000000000000000:0000000000000000");
    }

    TEST(AssetID, RequiresValidSourceAndNonZeroLocalID)
    {
        const SourceAssetID validSource = TestSourceID();
        const SourceAssetID invalidSource;

        ASSERT_TRUE(validSource.IsValid());
        EXPECT_FALSE(AssetID(invalidSource, 1).IsValid());
        EXPECT_FALSE(AssetID(validSource, 0).IsValid());
        EXPECT_TRUE(AssetID(validSource, 1).IsValid());
    }

    TEST(AssetID, IdentityUsesBothSourceAndLocalID)
    {
        const SourceAssetID sourceA = TestSourceID();
        const SourceAssetID sourceB = SourceAssetID::FromString("fedcba9876543210");

        const AssetID productA(sourceA, 7);
        const AssetID sameProduct(sourceA, 7);
        const AssetID differentProduct(sourceA, 8);
        const AssetID differentSource(sourceB, 7);

        EXPECT_EQ(productA, sameProduct);
        EXPECT_NE(productA, differentProduct);
        EXPECT_NE(productA, differentSource);
    }

    TEST(AssetID, EqualIDsProduceEqualHashes)
    {
        const SourceAssetID source = TestSourceID();
        const AssetID       first(source, 42);
        const AssetID       second(source, 42);

        EXPECT_EQ(first, second);
        EXPECT_EQ(std::hash<AssetID> {}(first), std::hash<AssetID> {}(second));
    }

    TEST(AssetID, StringRoundTripPreservesIdentityAndHash)
    {
        const AssetID     original(TestSourceID(), 0x2a);
        const std::string serialized = original.ToString();
        const AssetID     restored   = AssetID::FromString(serialized);

        ASSERT_TRUE(restored.IsValid());
        EXPECT_EQ(serialized, "0123456789abcdef:000000000000002a");
        EXPECT_EQ(restored, original);
        EXPECT_EQ(restored.GetSourceID(), original.GetSourceID());
        EXPECT_EQ(restored.GetLocalID(), original.GetLocalID());
        EXPECT_EQ(std::hash<AssetID> {}(restored), std::hash<AssetID> {}(original));
    }

    TEST(AssetID, InvalidStringsProduceTheCanonicalInvalidID)
    {
        const AssetID invalidInputs[] = {
            AssetID::FromString(""),
            AssetID::FromString("0123456789abcdef0000000000000001"),
            AssetID::FromString("0123456789abcdef-0000000000000001"),
            AssetID::FromString("gggggggggggggggg:0000000000000001"),
            AssetID::FromString("0000000000000000:0000000000000001"),
            AssetID::FromString("0123456789abcdef:gggggggggggggggg"),
            AssetID::FromString("0123456789abcdef:0000000000000000"),
        };

        const AssetID canonicalInvalid;
        for (const AssetID& id : invalidInputs)
        {
            EXPECT_FALSE(id.IsValid());
            EXPECT_EQ(id, canonicalInvalid);
            EXPECT_EQ(std::hash<AssetID> {}(id), std::hash<AssetID> {}(canonicalInvalid));
        }
    }

    TEST(AssetID, HashSupportsLookupAfterSerializationRoundTrip)
    {
        const AssetID                     original(TestSourceID(), 99);
        const AssetID                     restored = AssetID::FromString(original.ToString());
        const std::unordered_set<AssetID> ids      = {original};

        EXPECT_NE(ids.find(restored), ids.end());
    }
} // namespace
