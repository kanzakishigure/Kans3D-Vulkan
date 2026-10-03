#include <gtest/gtest.h>

#include "Kans3D/Asset/Asset.h"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>

namespace
{
    using Kans::AssetPath;

    TEST(AssetPath, DefaultConstructedPathIsInvalid)
    {
        const AssetPath path;

        EXPECT_FALSE(path.IsValid());
        EXPECT_TRUE(path.GetPath().empty());
        EXPECT_TRUE(path.ToString().empty());
        EXPECT_EQ(path.GetHash(), 0u);
    }

    TEST(AssetPath, RelativeLogicalPathIsValidWithoutExistingFile)
    {
        const AssetPath path("Future/NotCreated/NewTexture.png");

        ASSERT_TRUE(path.IsValid());
        EXPECT_EQ(path.ToString(), "Future/NotCreated/NewTexture.png");
        EXPECT_NE(path.GetHash(), 0u);
    }

    TEST(AssetPath, NormalizationProducesCanonicalLogicalPath)
    {
        const AssetPath path("Textures/./Environment//Wood.png");

        ASSERT_TRUE(path.IsValid());
        EXPECT_EQ(path.ToString(), "Textures/Environment/Wood.png");
        EXPECT_EQ(path.GetPath().generic_string(), path.ToString());
    }

    TEST(AssetPath, EmptyAndCurrentDirectoryPathsAreInvalid)
    {
        const AssetPath empty("");
        const AssetPath currentDirectory(".");

        EXPECT_FALSE(empty.IsValid());
        EXPECT_FALSE(currentDirectory.IsValid());
        EXPECT_EQ(empty.GetHash(), 0u);
        EXPECT_EQ(currentDirectory.GetHash(), 0u);
    }

    TEST(AssetPath, ParentTraversalPathsAreInvalid)
    {
        const AssetPath leadingParent("../Outside.png");
        const AssetPath internalParent("Textures/../Outside.png");
        const AssetPath escapingParent("Textures/../../Outside.png");

        EXPECT_FALSE(leadingParent.IsValid());
        EXPECT_FALSE(internalParent.IsValid());
        EXPECT_FALSE(escapingParent.IsValid());
    }

    TEST(AssetPath, AbsolutePathIsInvalid)
    {
        const std::filesystem::path absolutePath = std::filesystem::temp_directory_path() / "Kans3DAssetPathTest.asset";
        ASSERT_TRUE(absolutePath.is_absolute());

        const AssetPath path(absolutePath);

        EXPECT_FALSE(path.IsValid());
        EXPECT_TRUE(path.ToString().empty());
        EXPECT_EQ(path.GetHash(), 0u);
    }

    TEST(AssetPath, EquivalentInputsHaveEqualIdentityAndHash)
    {
        const AssetPath canonical("Textures/Environment/Wood.png");
        const AssetPath equivalent("Textures/./Environment//Wood.png");

        ASSERT_TRUE(canonical.IsValid());
        ASSERT_TRUE(equivalent.IsValid());
        EXPECT_EQ(canonical, equivalent);
        EXPECT_EQ(canonical.GetHash(), equivalent.GetHash());
        EXPECT_EQ(std::hash<AssetPath> {}(canonical), std::hash<AssetPath> {}(equivalent));
    }

    TEST(AssetPath, DifferentPathsRemainDistinctAndPreserveCase)
    {
        const AssetPath first("Textures/Wood.png");
        const AssetPath differentFile("Textures/Stone.png");
        const AssetPath differentCase("textures/Wood.png");

        EXPECT_NE(first, differentFile);
        EXPECT_NE(first, differentCase);
        EXPECT_EQ(first.ToString(), "Textures/Wood.png");
    }

    TEST(AssetPath, HashSupportsLookupAfterStringRoundTrip)
    {
        const AssetPath                     original("Models/Robot/Robot.fbx");
        const AssetPath                     restored(original.ToString());
        const std::unordered_set<AssetPath> paths = {original};

        ASSERT_TRUE(restored.IsValid());
        EXPECT_NE(paths.find(restored), paths.end());
    }
} // namespace
