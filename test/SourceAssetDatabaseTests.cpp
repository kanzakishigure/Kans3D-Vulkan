#include <gtest/gtest.h>
#include "Kans3D/Asset/SourceAssetDatabase.h"

namespace
{
    using namespace Kans;

    SourceAssetMetadata Record(const char* id, const char* path)
    {
        SourceAssetMetadata data;
        data.sourceAssetID = SourceAssetID::FromString(id);
        data.Path          = AssetPath(path);
        return data;
    }

    TEST(SourceAssetDatabase, RegistrationSupportsBothReadOnlyQueries)
    {
        SourceAssetDatabase database;
        const auto          data = Record("0123456789abcdef", "Textures/Wood.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(data)));
        const auto& readOnly = database;
        EXPECT_TRUE(readOnly.Contains(data.sourceAssetID));
        EXPECT_TRUE(readOnly.Contains(data.Path));
        EXPECT_EQ(readOnly.FindByID(data.sourceAssetID).Path, data.Path);
        EXPECT_EQ(readOnly.FindIDByPath(data.Path), data.sourceAssetID);
        auto copy = readOnly.FindByID(data.sourceAssetID);
        copy.Path = AssetPath("Changed.png");
        EXPECT_EQ(readOnly.FindByID(data.sourceAssetID).Path, data.Path);
    }

    TEST(SourceAssetDatabase, InvalidRecordsDoNotCreatePartialEntries)
    {
        SourceAssetDatabase database;
        auto                data   = Record("0123456789abcdef", "../Outside.png");
        auto                result = database.RegisterSource(data);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidArgument);
        EXPECT_FALSE(database.Contains(data.sourceAssetID));
        data   = Record("0", "Textures/Wood.png");
        result = database.RegisterSource(data);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::InvalidArgument);
        EXPECT_FALSE(database.Contains(data.Path));
    }

    TEST(SourceAssetDatabase, DuplicateIdPreservesOriginalAndDoesNotReserveNewPath)
    {
        SourceAssetDatabase database;
        const auto          original = Record("0123456789abcdef", "A.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(original)));
        auto conflict     = original;
        conflict.Path     = AssetPath("B.png");
        const auto result = database.RegisterSource(conflict);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::DuplicateID);
        EXPECT_EQ(database.FindByID(original.sourceAssetID).Path, original.Path);
        EXPECT_EQ(database.FindIDByPath(original.Path), original.sourceAssetID);
        EXPECT_FALSE(database.Contains(conflict.Path));
        conflict.sourceAssetID = SourceAssetID::FromString("fedcba9876543210");
        EXPECT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(conflict)));
    }

    TEST(SourceAssetDatabase, DuplicateNormalizedPathDoesNotReserveNewId)
    {
        SourceAssetDatabase database;
        const auto          original = Record("0123456789abcdef", "Textures/A.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(original)));
        auto       conflict = Record("fedcba9876543210", "Textures/./A.png");
        const auto result   = database.RegisterSource(conflict);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::DuplicatePath);
        EXPECT_FALSE(database.Contains(conflict.sourceAssetID));
        EXPECT_EQ(database.FindIDByPath(original.Path), original.sourceAssetID);
        EXPECT_EQ(database.FindByID(original.sourceAssetID).Path, original.Path);
        conflict.Path = AssetPath("Textures/B.png");
        EXPECT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(conflict)));
    }

    TEST(SourceAssetDatabase, RegisteringSameRecordTwiceReportsDuplicateId)
    {
        SourceAssetDatabase database;
        const auto          data = Record("0123456789abcdef", "A.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(data)));
        const auto result = database.RegisterSource(data);
        ASSERT_TRUE(std::holds_alternative<AssetError>(result));
        EXPECT_EQ(std::get<AssetError>(result).Code, AssetErrorCode::DuplicateID);
        EXPECT_EQ(database.FindIDByPath(data.Path), data.sourceAssetID);
    }

    TEST(SourceAssetDatabase, UpdatePreservesIdentityAndIndexes)
    {
        SourceAssetDatabase database;
        auto                data = Record("0123456789abcdef", "A.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(data)));
        data.FileSize       = 42;
        data.last_edit_time = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(123));
        data.Missing        = true;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.UpdateSource(data)));
        const auto updated = database.FindByID(data.sourceAssetID);
        EXPECT_EQ(updated.FileSize, data.FileSize);
        EXPECT_EQ(updated.last_edit_time, data.last_edit_time);
        EXPECT_TRUE(updated.Missing);
        EXPECT_FALSE(updated.Exists);
        EXPECT_EQ(updated.Path, data.Path);
        EXPECT_EQ(database.FindIDByPath(data.Path), data.sourceAssetID);
        EXPECT_EQ(database.GetAllSources().size(), 1u);
        data.Missing = false;
        data.Exists  = true;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.UpdateSource(data)));
        EXPECT_TRUE(database.FindByID(data.sourceAssetID).Exists);
        EXPECT_FALSE(database.FindByID(data.sourceAssetID).Missing);
    }

    TEST(SourceAssetDatabase, PathChangeAndUnregistrationReleaseBothIndexes)
    {
        SourceAssetDatabase database;
        auto                data = Record("0123456789abcdef", "A.png");
        data.FileSize            = 42;
        data.Missing             = true;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(data)));
        const AssetPath destination("Folder/./B.png");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.ChangeSourcePath(data.sourceAssetID, destination)));
        EXPECT_FALSE(database.Contains(data.Path));
        EXPECT_EQ(database.FindIDByPath(destination), data.sourceAssetID);
        EXPECT_EQ(database.FindByID(data.sourceAssetID).Path, destination);
        EXPECT_EQ(database.FindByID(data.sourceAssetID).FileSize, 42u);
        EXPECT_TRUE(database.FindByID(data.sourceAssetID).Missing);
        EXPECT_TRUE(std::holds_alternative<std::monostate>(database.ChangeSourcePath(data.sourceAssetID, destination)));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.UnregisterSource(data.sourceAssetID)));
        EXPECT_FALSE(database.Contains(destination));
        EXPECT_FALSE(database.Contains(data.sourceAssetID));
        EXPECT_TRUE(database.GetAllSources().empty());
        data.sourceAssetID = SourceAssetID::FromString("fedcba9876543210");
        data.Path          = destination;
        EXPECT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(data)));
    }

    TEST(SourceAssetDatabase, FailedMutationsPreserveAllRecordsAndPathReservations)
    {
        SourceAssetDatabase database;
        const auto          original = Record("0123456789abcdef", "A.png");
        auto                occupied = Record("fedcba9876543210", "B.png");
        occupied.Missing             = true;
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(original)));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(database.RegisterSource(occupied)));
        const auto unknown     = SourceAssetID::FromString("1111111111111111");
        const auto expectError = [](const auto& result, AssetErrorCode code) {
            ASSERT_TRUE(std::holds_alternative<AssetError>(result));
            EXPECT_EQ(std::get<AssetError>(result).Code, code);
        };
        auto update = original;
        update.Path = occupied.Path;
        expectError(database.UpdateSource(update), AssetErrorCode::InvalidArgument);
        update.Path = AssetPath("../outside");
        expectError(database.UpdateSource(update), AssetErrorCode::InvalidArgument);
        update        = original;
        update.Exists = update.Missing = true;
        expectError(database.UpdateSource(update), AssetErrorCode::InvalidArgument);
        update               = original;
        update.sourceAssetID = unknown;
        expectError(database.UpdateSource(update), AssetErrorCode::NotFound);
        update.sourceAssetID = {};
        expectError(database.UpdateSource(update), AssetErrorCode::InvalidArgument);
        expectError(database.ChangeSourcePath(original.sourceAssetID, occupied.Path), AssetErrorCode::DuplicatePath);
        expectError(database.ChangeSourcePath(original.sourceAssetID, AssetPath {}), AssetErrorCode::InvalidArgument);
        expectError(database.ChangeSourcePath({}, original.Path), AssetErrorCode::InvalidArgument);
        expectError(database.ChangeSourcePath(unknown, AssetPath("C.png")), AssetErrorCode::NotFound);
        expectError(database.UnregisterSource({}), AssetErrorCode::InvalidArgument);
        expectError(database.UnregisterSource(unknown), AssetErrorCode::NotFound);
        EXPECT_EQ(database.GetAllSources().size(), 2u);
        for (const auto& expected : {original, occupied})
        {
            EXPECT_EQ(database.FindIDByPath(expected.Path), expected.sourceAssetID);
            const auto actual = database.FindByID(expected.sourceAssetID);
            EXPECT_EQ(actual.Path, expected.Path);
            EXPECT_EQ(actual.Exists, expected.Exists);
            EXPECT_EQ(actual.Missing, expected.Missing);
            EXPECT_EQ(actual.FileSize, expected.FileSize);
        }
        EXPECT_FALSE(database.Contains(unknown));
        EXPECT_FALSE(database.Contains(AssetPath("C.png")));
    }

    TEST(SourceAssetDatabase, MissingAndInvalidQueriesReturnInvalidValues)
    {
        const SourceAssetDatabase database;
        const auto                data = Record("0123456789abcdef", "A.png");
        EXPECT_FALSE(database.FindByID(data.sourceAssetID).IsValid());
        EXPECT_FALSE(database.FindIDByPath(data.Path).IsValid());
        EXPECT_FALSE(database.Contains(data.sourceAssetID));
        EXPECT_FALSE(database.Contains(data.Path));
        EXPECT_FALSE(database.FindByID(SourceAssetID {}).IsValid());
        EXPECT_FALSE(database.FindIDByPath(AssetPath {}).IsValid());
        EXPECT_FALSE(database.Contains(SourceAssetID {}));
        EXPECT_FALSE(database.Contains(AssetPath {}));
    }
} // namespace
