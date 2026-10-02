#include <gtest/gtest.h>

#include "Kans3D/Asset/Asset.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace
{
using Kans::SourceAssetID;

TEST(SourceAssetID, DefaultConstructedIDIsInvalid)
{
    const SourceAssetID id;

    EXPECT_FALSE(id.IsValid());
    EXPECT_EQ(id.GetUUID(), 0u);
    EXPECT_EQ(id.GetHash(), 0u);
    EXPECT_EQ(id.ToString(), "0000000000000000");
}

TEST(SourceAssetID, GenerateProducesCanonicalValidID)
{
    const SourceAssetID id = SourceAssetID::Generate();
    const std::string text = id.ToString();

    ASSERT_TRUE(id.IsValid());
    ASSERT_EQ(text.size(), 16u);
    EXPECT_TRUE(std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c) != 0 || (c >= 'a' && c <= 'f');
    }));
}

TEST(SourceAssetID, StringRoundTripPreservesIdentityAndHash)
{
    const SourceAssetID original = SourceAssetID::Generate();
    const SourceAssetID restored = SourceAssetID::FromString(original.ToString());

    ASSERT_TRUE(restored.IsValid());
    EXPECT_EQ(restored, original);
    EXPECT_EQ(restored.GetUUID(), original.GetUUID());
    EXPECT_EQ(restored.GetHash(), original.GetHash());
    EXPECT_EQ(restored.ToString(), original.ToString());
}

TEST(SourceAssetID, EquivalentInputStringsProduceEqualHashes)
{
    const SourceAssetID shortForm = SourceAssetID::FromString("1");
    const SourceAssetID canonicalForm =
        SourceAssetID::FromString("0000000000000001");
    const SourceAssetID upperCase = SourceAssetID::FromString("ABCDEF");
    const SourceAssetID lowerCase = SourceAssetID::FromString("abcdef");

    ASSERT_TRUE(shortForm.IsValid());
    ASSERT_TRUE(canonicalForm.IsValid());
    EXPECT_EQ(shortForm, canonicalForm);
    EXPECT_EQ(shortForm.GetHash(), canonicalForm.GetHash());

    ASSERT_TRUE(upperCase.IsValid());
    ASSERT_TRUE(lowerCase.IsValid());
    EXPECT_EQ(upperCase, lowerCase);
    EXPECT_EQ(upperCase.GetHash(), lowerCase.GetHash());
}

TEST(SourceAssetID, InvalidStringsProduceTheCanonicalInvalidID)
{
    const SourceAssetID invalidInputs[] = {
        SourceAssetID::FromString(""),
        SourceAssetID::FromString("0"),
        SourceAssetID::FromString("0000000000000000"),
        SourceAssetID::FromString("not-a-source-id"),
        SourceAssetID::FromString("10000000000000000"),
    };

    const SourceAssetID canonicalInvalid;
    for (const SourceAssetID& id : invalidInputs)
    {
        EXPECT_FALSE(id.IsValid());
        EXPECT_EQ(id, canonicalInvalid);
        EXPECT_EQ(id.GetHash(), canonicalInvalid.GetHash());
    }
}

TEST(SourceAssetID, HashSupportsLookupAfterSerializationRoundTrip)
{
    const SourceAssetID original = SourceAssetID::Generate();
    const SourceAssetID restored = SourceAssetID::FromString(original.ToString());
    const std::unordered_set<SourceAssetID> ids = {original};

    EXPECT_NE(ids.find(restored), ids.end());
}
} // namespace
