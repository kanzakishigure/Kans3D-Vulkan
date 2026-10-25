#include <algorithm>
#include <array>
#include <future>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ImportFingerprint.h"
#include "Kans3D/Asset/Product/ContentDigest.h"

namespace
{
    using namespace Kans;
    ImportFingerprintInput Input()
    {
        ImportFingerprintInput input;
        input.Source          = {SourceAssetID::FromString("1"), std::string(64, '0')};
        input.Importer        = "TextureImporter";
        input.ImporterVersion = 1;
        input.Toolchain       = "stb-v2.26+Kans3D-BMP-v1;KTEX-v1";
        return input;
    }
    std::string Key(const ImportFingerprintInput& input)
    {
        auto result = ComputeImportFingerprint(input);
        if (const auto* error = std::get_if<AssetError>(&result))
            ADD_FAILURE() << error->Message;
        return std::holds_alternative<std::string>(result) ? std::get<std::string>(result) : std::string {};
    }
    TEST(ImportFingerprint, ChangesEveryDeclaredBuildInputWithoutChangingLogicalIdentity)
    {
        const auto input    = Input();
        const auto original = Key(input);
        EXPECT_TRUE(IsContentDigest(original));
        std::vector<ImportFingerprintInput> changed(11, input);
        changed[0].Source.ContentDigest[0] = '1';
        changed[1].Source.ContentDigest[63] = '1';
        changed[2].Source.ID               = SourceAssetID::FromString("2");
        changed[3].Importer                = "DifferentImporter";
        changed[4].ImporterVersion         = 2;
        changed[5].Toolchain += ";next-codec";
        changed[6].Target    = "windows-vulkan-debug";
        changed[7].Settings  = {{"usage", "normal"}};
        changed[8].Sources   = {{SourceAssetID::FromString("2"), std::string(64, '0')}};
        changed[9].Products  = {{AssetID(SourceAssetID::FromString("3"), 1), std::string(64, '0')}};
        changed[10].LocalIDs = {{"texture:root", 1}, {"deleted:key", 2}};
        for (const auto& value : changed)
            EXPECT_NE(Key(value), original);
        EXPECT_EQ(Key(input), original);
    }

    TEST(ImportFingerprint, CanonicalSchemaMatchesIndependentSha256Reference)
    {
        // Golden value from an independent Python implementation of the encoding:
        // length-prefixed strings and little-endian 64-bit numbers, schema 2.
        EXPECT_EQ(Key(Input()), "311f4d009d5837a49ac3dc826aee0dc6ace962583ebbcf61159e5885cd969de7");
    }

    TEST(ImportFingerprint, IdentityHistoryIncludesTombstonesAndRejectsAmbiguousAssignments)
    {
        auto input          = Input();
        input.LocalIDs      = {{"texture:root", 1}, {"removed:key", 2}};
        const auto original = Key(input);
        auto       changed  = input;
        changed.LocalIDs.erase("removed:key");
        EXPECT_NE(Key(changed), original);
        changed                          = input;
        changed.LocalIDs["texture:root"] = 3;
        EXPECT_NE(Key(changed), original);
        changed                          = input;
        changed.LocalIDs["texture:root"] = 2;
        EXPECT_EQ(std::get<AssetError>(ComputeImportFingerprint(changed)).Code, AssetErrorCode::DuplicateID);
        changed                          = input;
        changed.LocalIDs["texture:root"] = 0;
        EXPECT_TRUE(std::holds_alternative<AssetError>(ComputeImportFingerprint(changed)));
        changed = input;
        changed.LocalIDs.emplace(std::string("opaque\0key", 10), 3);
        EXPECT_TRUE(IsContentDigest(Key(changed)));
    }
    TEST(ImportFingerprint, SettingsAndDependencyEnumerationOrderDoNotAffectKey)
    {
        auto first     = Input();
        first.Settings = {{"z", "last"}, {"a", "first"}};
        first.Sources  = {{SourceAssetID::FromString("3"), std::string(64, '1')},
                          {SourceAssetID::FromString("2"), std::string(64, '2')}};
        first.Products = {{AssetID(SourceAssetID::FromString("5"), 2), std::string(64, '3')},
                          {AssetID(SourceAssetID::FromString("4"), 3), std::string(64, '4')},
                          {AssetID(SourceAssetID::FromString("5"), 1), std::string(64, '5')}};
        auto second    = first;
        std::reverse(second.Sources.begin(), second.Sources.end());
        std::reverse(second.Products.begin(), second.Products.end());
        second.Settings.clear();
        second.Settings.emplace("a", "first");
        second.Settings.emplace("z", "last");
        EXPECT_EQ(Key(first), Key(second));
        second.Sources[0].ContentDigest[0] = '0';
        EXPECT_NE(Key(first), Key(second));
        second                              = first;
        second.Products[0].ContentDigest[0] = '0';
        EXPECT_NE(Key(first), Key(second));
    }
    TEST(ImportFingerprint, LengthPrefixesPreventDelimiterAndBinaryValueAliases)
    {
        auto left = Input(), right = Input();
        left.Settings  = {{"a", "b:c"}};
        right.Settings = {{"a:b", "c"}};
        EXPECT_NE(Key(left), Key(right));
        left.Settings  = {{"name", std::string("a\0b", 3)}};
        right.Settings = {{"name", "a"}};
        EXPECT_NE(Key(left), Key(right));
        left.Settings = {{"中文设置", "颜色"}};
        EXPECT_TRUE(IsContentDigest(Key(left)));
    }
    TEST(ImportFingerprint, RejectsInvalidIdentitiesDigestsNamesTargetsAndDuplicateInputs)
    {
        std::vector<ImportFingerprintInput> invalid(10, Input());
        invalid[0].Source.ID            = {};
        invalid[1].Source.ContentDigest = "";
        invalid[2].Source.ContentDigest = std::string(64, 'A');
        invalid[3].Importer.clear();
        invalid[4].ImporterVersion = 0;
        invalid[5].Toolchain       = std::string("tool\0chain", 10);
        invalid[6].Target          = "../default";
        invalid[7].Settings        = {{"", "value"}};
        invalid[8].Sources         = {invalid[8].Source};
        invalid[9].Products        = {{AssetID(invalid[9].Source.ID, 1), std::string(64, '0')}};
        for (const auto& value : invalid)
            EXPECT_TRUE(std::holds_alternative<AssetError>(ComputeImportFingerprint(value)));
        auto duplicate    = Input();
        duplicate.Sources = {{SourceAssetID::FromString("2"), std::string(64, '0')}};
        duplicate.Sources.push_back(duplicate.Sources.front());
        EXPECT_EQ(std::get<AssetError>(ComputeImportFingerprint(duplicate)).Code, AssetErrorCode::DuplicateID);
        duplicate.Sources.clear();
        duplicate.Products = {{AssetID(SourceAssetID::FromString("2"), 1), std::string(64, '0')}};
        duplicate.Products.push_back(duplicate.Products.front());
        EXPECT_EQ(std::get<AssetError>(ComputeImportFingerprint(duplicate)).Code, AssetErrorCode::DuplicateID);
    }
    TEST(ImportFingerprint, CountStringAndTotalByteLimitsAreChecked)
    {
        auto input     = Input();
        input.Settings = {{"key", std::string(65537, 'x')}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(ComputeImportFingerprint(input)));
        input.Settings.clear();
        for (size_t i = 0; i < 4097; ++i)
            input.Settings.emplace(std::to_string(i), "x");
        EXPECT_TRUE(std::holds_alternative<AssetError>(ComputeImportFingerprint(input)));
        input.Settings.clear();
        for (size_t i = 0; i < 70; ++i)
            input.Settings.emplace(std::to_string(i), std::string(65536, 'x'));
        EXPECT_TRUE(std::holds_alternative<AssetError>(ComputeImportFingerprint(input)));
        input.Settings.clear();
        input.Products.resize(65537);
        EXPECT_TRUE(std::holds_alternative<AssetError>(ComputeImportFingerprint(input)));
    }
    TEST(ImportFingerprint, ConcurrentComputationsRemainIndependent)
    {
        const auto                              input    = Input();
        const auto                              expected = Key(input);
        std::array<std::future<std::string>, 8> work;
        for (auto& task : work)
            task = std::async(std::launch::async,
                              [input] { return std::get<std::string>(ComputeImportFingerprint(input)); });
        for (auto& task : work)
            EXPECT_EQ(task.get(), expected);
    }
} // namespace
