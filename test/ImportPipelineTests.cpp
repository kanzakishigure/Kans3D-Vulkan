#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/AssetImporter.h"
#include "Kans3D/Asset/Importer/ModelImporter.h"
#include "Kans3D/Asset/Loader/Loader.h"

namespace
{
    using namespace Kans;
    class FakeImporter : public AssetImporterBase
    {
    public:
        ImportResult                           Result;
        uint32_t                               GetVersion() const override { return 1; }
        std::variant<ImportResult, AssetError> Import(const ImportContext&) const override { return Result; }
    };
    ImportContext Context()
    {
        ImportContext c;
        c.SourceID         = SourceAssetID::Generate();
        c.SourceFile       = std::filesystem::temp_directory_path() / "model.FBX";
        c.StagingDirectory = std::filesystem::temp_directory_path() / "staged";
        return c;
    }
    TEST(ImportPipeline, SelectsSourceFormatAndReturnsCompleteProductSet)
    {
        ImporterRegistry registry;
        auto             fake = std::make_shared<FakeImporter>();
        fake->Result.Products = {{1, AssetType::StaticMesh, "body.mesh", {}},
                                 {2, AssetType::Material, "body.material", {}}};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.Register(".fbx", fake)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(registry.Register(".FBX", fake)));
        AssetImporter importer(registry);
        auto          result = importer.Import(Context());
        ASSERT_TRUE(std::holds_alternative<ImportResult>(result));
        EXPECT_EQ(std::get<ImportResult>(result).Products.size(), 2u);
    }
    TEST(ImportPipeline, RejectsInvalidAndConflictingProducts)
    {
        ImporterRegistry registry;
        auto             fake = std::make_shared<FakeImporter>();
        ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.Register(".fbx", fake)));
        AssetImporter importer(registry);
        fake->Result.Products = {{1, AssetType::StaticMesh, "../bad", {}}};
        EXPECT_TRUE(std::holds_alternative<AssetError>(importer.Import(Context())));
        fake->Result.Products = {{1, AssetType::StaticMesh, "a", {}}, {1, AssetType::StaticMesh, "b", {}}};
        EXPECT_EQ(std::get<AssetError>(importer.Import(Context())).Code, AssetErrorCode::DuplicateID);
        fake->Result.Products[1].LocalID    = 2;
        fake->Result.Products[1].StagedFile = "./a";
        EXPECT_EQ(std::get<AssetError>(importer.Import(Context())).Code, AssetErrorCode::DuplicatePath);
    }
    TEST(ImportPipeline, MissingImporterAndUnimplementedModelReturnErrors)
    {
        ImporterRegistry registry;
        AssetImporter    importer(registry);
        EXPECT_EQ(std::get<AssetError>(importer.Import(Context())).Code, AssetErrorCode::NotFound);
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(registry.Register(".fbx", std::make_shared<ModelImporter>())));
        EXPECT_EQ(std::get<AssetError>(importer.Import(Context())).Code, AssetErrorCode::NotImplemented);
    }
    TEST(ImportPipeline, StaticMeshLoaderNeverFallsBackToSourceImport)
    {
        Loader<StaticMesh> loader;
        AssetLoadContext   input;
        EXPECT_EQ(std::get<AssetError>(loader.Load(input)).Code, AssetErrorCode::InvalidArgument);
        const std::byte bytes[] = {std::byte {0}};
        input.ID                = AssetID(SourceAssetID::Generate(), 1);
        input.Type              = AssetType::StaticMesh;
        input.Data              = bytes;
        input.Size              = sizeof(bytes);
        EXPECT_EQ(std::get<AssetError>(loader.Load(input)).Code, AssetErrorCode::NotImplemented);
    }
} // namespace
