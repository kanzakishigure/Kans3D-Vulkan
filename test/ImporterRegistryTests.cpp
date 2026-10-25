#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ImporterRegistry.h"
#include "Kans3D/Asset/Loader/Loader.h"
#include "Kans3D/Asset/Runtime/MeshData.h"

namespace
{
    using namespace Kans;

    class FakeImporter : public AssetImporterBase
    {
    public:
        uint32_t                               GetVersion() const override { return 1; }
        std::variant<ImportResult, AssetError> Import(const ImportContext&) const override { return ImportResult {}; }
    };

    TEST(ImporterRegistry, ResolvesDefaultsCaseInsensitivelyAndNamedAlternatives)
    {
        ImporterRegistry registry;
        auto             primary = std::make_shared<FakeImporter>();
        auto             other   = std::make_shared<FakeImporter>();
        ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.Register("Primary", {".FBX", ".obj"}, primary)));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.Register("Other", {".fbx"}, other, false)));

        auto selected = registry.Resolve("models/Ship.fbx");
        ASSERT_TRUE(std::holds_alternative<RegisteredImporter>(selected));
        EXPECT_EQ(std::get<RegisteredImporter>(selected).Name, "Primary");
        EXPECT_EQ(std::get<RegisteredImporter>(selected).Importer, primary);
        auto named = registry.Resolve("Ship.FBX", "Other");
        ASSERT_TRUE(std::holds_alternative<RegisteredImporter>(named));
        EXPECT_EQ(std::get<RegisteredImporter>(named).Importer, other);
        EXPECT_TRUE(registry.Supports("a.OBJ"));
        EXPECT_FALSE(registry.Supports("a.txt"));
    }

    TEST(ImporterRegistry, RejectsUnknownFormatsNamesAndConflictingRegistrations)
    {
        ImporterRegistry registry;
        auto             fake = std::make_shared<FakeImporter>();
        ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.Register("Fake", {".fbx"}, fake)));
        EXPECT_EQ(std::get<AssetError>(registry.Resolve("a.png")).Code, AssetErrorCode::NotFound);
        EXPECT_EQ(std::get<AssetError>(registry.Resolve("a.fbx", "Missing")).Code, AssetErrorCode::NotFound);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.Register("PngOnly", {".png"}, fake, false)));
        EXPECT_EQ(std::get<AssetError>(registry.Resolve("a.fbx", "PngOnly")).Code, AssetErrorCode::InvalidArgument);

        // Duplicate names, a second default for an extension, bad extensions or names.
        EXPECT_TRUE(std::holds_alternative<AssetError>(registry.Register("Fake", {".obj"}, fake)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(registry.Register("Second", {".FBX"}, fake)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(registry.Register("Bad", {"fbx"}, fake)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(registry.Register("Bad", {".a/b"}, fake)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(registry.Register("bad name", {".x"}, fake)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(registry.Register("Null", {".x"}, nullptr)));
        // A rejected registration claims nothing.
        EXPECT_FALSE(registry.Supports("a.obj"));
    }

    TEST(ImporterRegistry, DefaultRegistryCoversBuiltInFormats)
    {
        auto created = CreateDefaultImporterRegistry();
        ASSERT_TRUE(std::holds_alternative<ImporterRegistry>(created));
        const auto& registry = std::get<ImporterRegistry>(created);
        for (const char* file :
             {"a.obj", "a.gltf", "a.glb", "a.fbx", "a.png", "a.jpg", "a.hdr", "a.vert", "a.shaderprog", "a.material"})
            EXPECT_TRUE(registry.Supports(file)) << file;
        EXPECT_EQ(std::get<RegisteredImporter>(registry.Resolve("a.png")).Name, "TextureImporter");
    }

    TEST(ImporterRegistry, MeshLoaderNeverFallsBackToSourceImport)
    {
        Loader<MeshAsset> loader;
        AssetLoadContext  input;
        EXPECT_EQ(std::get<AssetError>(loader.Load(input)).Code, AssetErrorCode::InvalidArgument);
        const std::byte bytes[] = {std::byte {0}};
        input.ID                = AssetID(SourceAssetID::Generate(), 1);
        input.Type              = AssetType::StaticMesh;
        input.Data              = bytes;
        input.Size              = sizeof(bytes);
        EXPECT_EQ(std::get<AssetError>(loader.Load(input)).Code, AssetErrorCode::InvalidData);
    }
} // namespace
